#include "rtmidi_android_usb.h"

#include <dlfcn.h>
#include <errno.h>
#include <linux/usbdevice_fs.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>

#define USB_CLASS_AUDIO 1
#define USB_SUBCLASS_MIDISTREAMING 3
#define USB_ENDPOINT_IN 0x80
#define USB_ENDPOINT_XFER_BULK 2
#define USB_ENDPOINT_XFER_INT 3
#define MAX_DEVICES 32
#define READ_SIZE 512

typedef struct {
  int address;
  int type;
  int size;
} Endpoint;

typedef struct {
  jobject device;
  int iface;
  Endpoint in;
  Endpoint out;
} Found;

typedef struct {
  unsigned char *data;
  size_t len;
  size_t cap;
} Sysex;

typedef struct Device {
  struct Device *next;
  char path[128];
  jobject device;
  jobject connection;
  jobject iface;
  int iface_index;
  Endpoint in;
  Endpoint out;
  atomic_int fd;
  atomic_int stop;
  int refs;
  int has_thread;
  pthread_t thread;
  pthread_mutex_t cb_lock;
  RtMidiAndroidUsbCallback cb;
  void *user;
  Sysex sysex[16];
} Device;

struct RtMidiAndroidUsbPort {
  Device *dev;
  int input;
};

static JavaVM *g_vm;
static jobject g_context;
static Device *g_devices;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static const unsigned char cin_length[16] = { 0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1 };

static double now_seconds(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int sdk_level(void)
{
  char value[PROP_VALUE_MAX] = { 0 };
  __system_property_get("ro.build.version.sdk", value);
  return atoi(value);
}

static void find_vm(void)
{
  typedef jint (*GetCreatedJavaVMs)(JavaVM **, jsize, jsize *);
  GetCreatedJavaVMs fn = (GetCreatedJavaVMs)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
  if (!fn) {
    void *lib = dlopen("libnativehelper.so", RTLD_NOW);
    if (lib) fn = (GetCreatedJavaVMs)dlsym(lib, "JNI_GetCreatedJavaVMs");
  }
  JavaVM *vm = NULL;
  jsize count = 0;
  if (fn && fn(&vm, 1, &count) == JNI_OK && count > 0) g_vm = vm;
}

static JNIEnv *attach(int *attached)
{
  JNIEnv *env = NULL;
  *attached = 0;
  if (!g_vm) find_vm();
  if (!g_vm) return NULL;
  jint r = (*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6);
  if (r == JNI_EDETACHED) {
    if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK) return NULL;
    *attached = 1;
  } else if (r != JNI_OK) {
    return NULL;
  }
  return env;
}

static void detach(int attached)
{
  if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}

static int failed(JNIEnv *env)
{
  if (!(*env)->ExceptionCheck(env)) return 0;
  (*env)->ExceptionClear(env);
  return 1;
}

static jmethodID method(JNIEnv *env, jobject obj, const char *name, const char *sig)
{
  jclass cls = (*env)->GetObjectClass(env, obj);
  jmethodID m = (*env)->GetMethodID(env, cls, name, sig);
  (*env)->DeleteLocalRef(env, cls);
  if (failed(env)) return NULL;
  return m;
}

static jobject context(JNIEnv *env)
{
  if (g_context) return (*env)->NewLocalRef(env, g_context);
  jclass cls = (*env)->FindClass(env, "android/app/ActivityThread");
  if (failed(env) || !cls) return NULL;
  jmethodID m = (*env)->GetStaticMethodID(env, cls, "currentApplication", "()Landroid/app/Application;");
  jobject app = (!failed(env) && m) ? (*env)->CallStaticObjectMethod(env, cls, m) : NULL;
  (*env)->DeleteLocalRef(env, cls);
  if (failed(env)) return NULL;
  return app;
}

static jobject usb_manager(JNIEnv *env)
{
  jobject ctx = context(env);
  if (!ctx) return NULL;
  jmethodID m = method(env, ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  jstring name = (*env)->NewStringUTF(env, "usb");
  jobject mgr = m ? (*env)->CallObjectMethod(env, ctx, m, name) : NULL;
  (*env)->DeleteLocalRef(env, name);
  (*env)->DeleteLocalRef(env, ctx);
  if (failed(env)) return NULL;
  return mgr;
}

static int call_int(JNIEnv *env, jobject obj, const char *name)
{
  jmethodID m = method(env, obj, name, "()I");
  if (!m) return -1;
  int v = (*env)->CallIntMethod(env, obj, m);
  return failed(env) ? -1 : v;
}

static jobject call_object_int(JNIEnv *env, jobject obj, const char *name, const char *sig, int arg)
{
  jmethodID m = method(env, obj, name, sig);
  if (!m) return NULL;
  jobject v = (*env)->CallObjectMethod(env, obj, m, arg);
  return failed(env) ? NULL : v;
}

static int string_method(JNIEnv *env, jobject obj, const char *name, char *buf, size_t len)
{
  jmethodID m = method(env, obj, name, "()Ljava/lang/String;");
  if (!m) return -1;
  jstring s = (jstring)(*env)->CallObjectMethod(env, obj, m);
  if (failed(env) || !s) return -1;
  const char *chars = (*env)->GetStringUTFChars(env, s, NULL);
  if (chars) {
    strncpy(buf, chars, len - 1);
    buf[len - 1] = 0;
    (*env)->ReleaseStringUTFChars(env, s, chars);
  }
  (*env)->DeleteLocalRef(env, s);
  return chars ? 0 : -1;
}

static int midi_interface(JNIEnv *env, jobject device, Found *found)
{
  int count = call_int(env, device, "getInterfaceCount");
  for (int i = 0; i < count; i++) {
    jobject iface = call_object_int(env, device, "getInterface", "(I)Landroid/hardware/usb/UsbInterface;", i);
    if (!iface) continue;
    int ok = call_int(env, iface, "getInterfaceClass") == USB_CLASS_AUDIO &&
             call_int(env, iface, "getInterfaceSubclass") == USB_SUBCLASS_MIDISTREAMING;
    if (ok) {
      found->iface = i;
      found->in.address = -1;
      found->out.address = -1;
      int endpoints = call_int(env, iface, "getEndpointCount");
      for (int j = 0; j < endpoints; j++) {
        jobject ep = call_object_int(env, iface, "getEndpoint", "(I)Landroid/hardware/usb/UsbEndpoint;", j);
        if (!ep) continue;
        int type = call_int(env, ep, "getType");
        if (type == USB_ENDPOINT_XFER_BULK || type == USB_ENDPOINT_XFER_INT) {
          Endpoint *e = call_int(env, ep, "getDirection") == USB_ENDPOINT_IN ? &found->in : &found->out;
          if (e->address < 0) {
            e->address = call_int(env, ep, "getAddress");
            e->type = type;
            e->size = call_int(env, ep, "getMaxPacketSize");
          }
        }
        (*env)->DeleteLocalRef(env, ep);
      }
    }
    (*env)->DeleteLocalRef(env, iface);
    if (ok) return 0;
  }
  return -1;
}

static int find_ports(JNIEnv *env, jobject mgr, int input, Found *out, int max)
{
  jmethodID m = method(env, mgr, "getDeviceList", "()Ljava/util/HashMap;");
  jobject map = m ? (*env)->CallObjectMethod(env, mgr, m) : NULL;
  if (failed(env) || !map) return 0;
  jmethodID values_m = method(env, map, "values", "()Ljava/util/Collection;");
  jobject values = values_m ? (*env)->CallObjectMethod(env, map, values_m) : NULL;
  (*env)->DeleteLocalRef(env, map);
  if (failed(env) || !values) return 0;
  jmethodID array_m = method(env, values, "toArray", "()[Ljava/lang/Object;");
  jobjectArray devices = array_m ? (jobjectArray)(*env)->CallObjectMethod(env, values, array_m) : NULL;
  (*env)->DeleteLocalRef(env, values);
  if (failed(env) || !devices) return 0;
  int n = 0;
  jsize count = (*env)->GetArrayLength(env, devices);
  for (jsize i = 0; i < count && n < max; i++) {
    jobject device = (*env)->GetObjectArrayElement(env, devices, i);
    Found f;
    if (midi_interface(env, device, &f) == 0 && (input ? f.in.address : f.out.address) >= 0) {
      f.device = device;
      out[n++] = f;
    } else {
      (*env)->DeleteLocalRef(env, device);
    }
  }
  (*env)->DeleteLocalRef(env, devices);
  return n;
}

static void release_found(JNIEnv *env, Found *found, int n)
{
  for (int i = 0; i < n; i++) (*env)->DeleteLocalRef(env, found[i].device);
}

static void request_permission(JNIEnv *env, jobject mgr, jobject device)
{
  jobject ctx = context(env);
  if (!ctx) return;
  int sdk = sdk_level();
  jclass intent_cls = (*env)->FindClass(env, "android/content/Intent");
  jclass pending_cls = (*env)->FindClass(env, "android/app/PendingIntent");
  if (failed(env) || !intent_cls || !pending_cls) goto done;
  jmethodID ctor = (*env)->GetMethodID(env, intent_cls, "<init>", "(Ljava/lang/String;)V");
  jmethodID set_package = (*env)->GetMethodID(env, intent_cls, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;");
  jmethodID get_broadcast = (*env)->GetStaticMethodID(env, pending_cls, "getBroadcast",
      "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;");
  jmethodID package_name = method(env, ctx, "getPackageName", "()Ljava/lang/String;");
  jmethodID request = method(env, mgr, "requestPermission", "(Landroid/hardware/usb/UsbDevice;Landroid/app/PendingIntent;)V");
  if (failed(env) || !ctor || !set_package || !get_broadcast || !package_name || !request) goto done;
  jstring action = (*env)->NewStringUTF(env, "com.rtmidi.USB_PERMISSION");
  jobject intent = (*env)->NewObject(env, intent_cls, ctor, action);
  jobject pkg = (*env)->CallObjectMethod(env, ctx, package_name);
  if (!failed(env) && intent && pkg) {
    jobject same = (*env)->CallObjectMethod(env, intent, set_package, pkg);
    if (same) (*env)->DeleteLocalRef(env, same);
    jint flags = sdk >= 31 ? 0x02000000 : 0;
    jobject pending = (*env)->CallStaticObjectMethod(env, pending_cls, get_broadcast, ctx, 0, intent, flags);
    if (!failed(env) && pending) {
      (*env)->CallVoidMethod(env, mgr, request, device, pending);
      failed(env);
      (*env)->DeleteLocalRef(env, pending);
    }
  }
  failed(env);
  if (pkg) (*env)->DeleteLocalRef(env, pkg);
  if (intent) (*env)->DeleteLocalRef(env, intent);
  (*env)->DeleteLocalRef(env, action);
done:
  failed(env);
  if (intent_cls) (*env)->DeleteLocalRef(env, intent_cls);
  if (pending_cls) (*env)->DeleteLocalRef(env, pending_cls);
  (*env)->DeleteLocalRef(env, ctx);
}

static int connect_device(JNIEnv *env, Device *d)
{
  jobject mgr = usb_manager(env);
  if (!mgr) return -1;
  int result = -1;
  jobject conn = NULL;
  jobject iface = NULL;
  jmethodID has = method(env, mgr, "hasPermission", "(Landroid/hardware/usb/UsbDevice;)Z");
  jmethodID open = method(env, mgr, "openDevice", "(Landroid/hardware/usb/UsbDevice;)Landroid/hardware/usb/UsbDeviceConnection;");
  if (!has || !open) goto done;
  int requested = 0;
  for (;;) {
    if (atomic_load(&d->stop)) goto done;
    jboolean granted = (*env)->CallBooleanMethod(env, mgr, has, d->device);
    if (failed(env)) goto done;
    if (granted) break;
    if (!requested) {
      request_permission(env, mgr, d->device);
      requested = 1;
    }
    usleep(100000);
  }
  conn = (*env)->CallObjectMethod(env, mgr, open, d->device);
  if (failed(env) || !conn) goto done;
  iface = call_object_int(env, d->device, "getInterface", "(I)Landroid/hardware/usb/UsbInterface;", d->iface_index);
  jmethodID claim = method(env, conn, "claimInterface", "(Landroid/hardware/usb/UsbInterface;Z)Z");
  jmethodID close = method(env, conn, "close", "()V");
  if (!iface || !claim || !close) goto done;
  if (!(*env)->CallBooleanMethod(env, conn, claim, iface, JNI_TRUE) || failed(env)) {
    (*env)->CallVoidMethod(env, conn, close);
    failed(env);
    goto done;
  }
  int fd = call_int(env, conn, "getFileDescriptor");
  d->connection = (*env)->NewGlobalRef(env, conn);
  d->iface = (*env)->NewGlobalRef(env, iface);
  atomic_store(&d->fd, fd);
  result = fd >= 0 ? 0 : -1;
done:
  if (iface) (*env)->DeleteLocalRef(env, iface);
  if (conn) (*env)->DeleteLocalRef(env, conn);
  (*env)->DeleteLocalRef(env, mgr);
  return result;
}

static void deliver(Device *d, const unsigned char *msg, size_t len, double t)
{
  pthread_mutex_lock(&d->cb_lock);
  if (d->cb) d->cb(t, msg, len, d->user);
  pthread_mutex_unlock(&d->cb_lock);
}

static void sysex_append(Sysex *s, const unsigned char *bytes, size_t n)
{
  if (s->len + n > s->cap) {
    size_t cap = s->cap ? s->cap * 2 : 256;
    while (cap < s->len + n) cap *= 2;
    unsigned char *data = (unsigned char *)realloc(s->data, cap);
    if (!data) return;
    s->data = data;
    s->cap = cap;
  }
  memcpy(s->data + s->len, bytes, n);
  s->len += n;
}

static void parse(Device *d, const unsigned char *buf, int n, double t)
{
  for (int i = 0; i + 3 < n; i += 4) {
    const unsigned char *p = buf + i;
    int cin = p[0] & 0x0F;
    Sysex *s = &d->sysex[p[0] >> 4];
    if (cin < 2) continue;
    if (cin == 4 || cin == 6 || cin == 7 || (cin == 5 && s->len > 0)) {
      sysex_append(s, p + 1, cin == 4 ? 3 : (size_t)(cin - 4));
      if (cin != 4) {
        deliver(d, s->data, s->len, t);
        s->len = 0;
      }
      continue;
    }
    deliver(d, p + 1, cin_length[cin], t);
  }
}

static void read_loop(Device *d)
{
  unsigned char buf[READ_SIZE];
  int fd = atomic_load(&d->fd);
  int size = d->in.size > 0 && d->in.size <= READ_SIZE ? d->in.size : 64;
  struct usbdevfs_urb urb;
  for (;;) {
    memset(&urb, 0, sizeof(urb));
    urb.type = d->in.type == USB_ENDPOINT_XFER_INT ? USBDEVFS_URB_TYPE_INTERRUPT : USBDEVFS_URB_TYPE_BULK;
    urb.endpoint = (unsigned char)d->in.address;
    urb.buffer = buf;
    urb.buffer_length = size;
    if (ioctl(fd, USBDEVFS_SUBMITURB, &urb) < 0) return;
    for (;;) {
      struct usbdevfs_urb *reaped = NULL;
      if (atomic_load(&d->stop)) {
        ioctl(fd, USBDEVFS_DISCARDURB, &urb);
        ioctl(fd, USBDEVFS_REAPURB, &reaped);
        return;
      }
      struct pollfd pfd = { fd, POLLOUT, 0 };
      int ready = poll(&pfd, 1, 50);
      if (ready < 0 && errno != EINTR) return;
      if (ready <= 0) continue;
      if (pfd.revents & POLLHUP) return;
      if (ioctl(fd, USBDEVFS_REAPURBNDELAY, &reaped) == 0) break;
      if (errno != EAGAIN) return;
    }
    if (urb.status == 0) parse(d, buf, urb.actual_length, now_seconds());
    else if (urb.status == -ENODEV || urb.status == -ESHUTDOWN || urb.status == -EPROTO) return;
  }
}

static void *device_thread(void *arg)
{
  Device *d = (Device *)arg;
  int attached;
  JNIEnv *env = attach(&attached);
  if (!env) return NULL;
  int connected = connect_device(env, d) == 0;
  detach(attached);
  if (connected && d->in.address >= 0) read_loop(d);
  return NULL;
}

static void free_device(JNIEnv *env, Device *d)
{
  atomic_store(&d->stop, 1);
  if (d->has_thread) pthread_join(d->thread, NULL);
  if (env && d->connection) {
    jmethodID release = method(env, d->connection, "releaseInterface", "(Landroid/hardware/usb/UsbInterface;)Z");
    if (release && d->iface) (*env)->CallBooleanMethod(env, d->connection, release, d->iface);
    failed(env);
    jmethodID close = method(env, d->connection, "close", "()V");
    if (close) (*env)->CallVoidMethod(env, d->connection, close);
    failed(env);
  }
  if (env) {
    if (d->connection) (*env)->DeleteGlobalRef(env, d->connection);
    if (d->iface) (*env)->DeleteGlobalRef(env, d->iface);
    if (d->device) (*env)->DeleteGlobalRef(env, d->device);
  }
  for (int i = 0; i < 16; i++) free(d->sysex[i].data);
  pthread_mutex_destroy(&d->cb_lock);
  free(d);
}

void rtmidi_android_usb_set_java_context(JavaVM *vm, jobject ctx)
{
  pthread_mutex_lock(&g_lock);
  g_vm = vm;
  int attached;
  JNIEnv *env = attach(&attached);
  if (env) {
    if (g_context) (*env)->DeleteGlobalRef(env, g_context);
    g_context = ctx ? (*env)->NewGlobalRef(env, ctx) : NULL;
  }
  detach(attached);
  pthread_mutex_unlock(&g_lock);
}

unsigned int rtmidi_android_usb_port_count(int input)
{
  int attached;
  JNIEnv *env = attach(&attached);
  if (!env) return 0;
  Found found[MAX_DEVICES];
  int n = 0;
  jobject mgr = usb_manager(env);
  if (mgr) {
    n = find_ports(env, mgr, input, found, MAX_DEVICES);
    release_found(env, found, n);
    (*env)->DeleteLocalRef(env, mgr);
  }
  detach(attached);
  return (unsigned int)n;
}

int rtmidi_android_usb_port_name(int input, unsigned int portNumber, char *buf, size_t len)
{
  int attached;
  JNIEnv *env = attach(&attached);
  if (!env || len == 0) return -1;
  int result = -1;
  Found found[MAX_DEVICES];
  jobject mgr = usb_manager(env);
  if (mgr) {
    int n = find_ports(env, mgr, input, found, MAX_DEVICES);
    if (portNumber < (unsigned int)n) {
      result = string_method(env, found[portNumber].device, "getProductName", buf, len);
      if (result != 0) result = string_method(env, found[portNumber].device, "getDeviceName", buf, len);
    }
    release_found(env, found, n);
    (*env)->DeleteLocalRef(env, mgr);
  }
  detach(attached);
  return result;
}

RtMidiAndroidUsbPort *rtmidi_android_usb_open(int input, unsigned int portNumber, RtMidiAndroidUsbCallback callback, void *userData)
{
  RtMidiAndroidUsbPort *port = NULL;
  pthread_mutex_lock(&g_lock);
  int attached;
  JNIEnv *env = attach(&attached);
  jobject mgr = env ? usb_manager(env) : NULL;
  if (!mgr) goto done;
  Found found[MAX_DEVICES];
  int n = find_ports(env, mgr, input, found, MAX_DEVICES);
  char path[128];
  if (portNumber >= (unsigned int)n || string_method(env, found[portNumber].device, "getDeviceName", path, sizeof(path)) != 0) {
    release_found(env, found, n);
    goto done;
  }
  Device *d = g_devices;
  while (d && strcmp(d->path, path) != 0) d = d->next;
  if (d && input && d->cb) {
    release_found(env, found, n);
    goto done;
  }
  if (!d) {
    d = (Device *)calloc(1, sizeof(Device));
    if (!d) {
      release_found(env, found, n);
      goto done;
    }
    strcpy(d->path, path);
    d->device = (*env)->NewGlobalRef(env, found[portNumber].device);
    d->iface_index = found[portNumber].iface;
    d->in = found[portNumber].in;
    d->out = found[portNumber].out;
    atomic_init(&d->fd, -1);
    atomic_init(&d->stop, 0);
    pthread_mutex_init(&d->cb_lock, NULL);
    if (pthread_create(&d->thread, NULL, device_thread, d) != 0) {
      release_found(env, found, n);
      free_device(env, d);
      goto done;
    }
    d->has_thread = 1;
    d->next = g_devices;
    g_devices = d;
  }
  release_found(env, found, n);
  port = (RtMidiAndroidUsbPort *)calloc(1, sizeof(RtMidiAndroidUsbPort));
  if (!port) goto done;
  port->dev = d;
  port->input = input;
  d->refs++;
  if (input) {
    pthread_mutex_lock(&d->cb_lock);
    d->cb = callback;
    d->user = userData;
    pthread_mutex_unlock(&d->cb_lock);
  }
done:
  if (mgr) (*env)->DeleteLocalRef(env, mgr);
  if (env) detach(attached);
  pthread_mutex_unlock(&g_lock);
  return port;
}

void rtmidi_android_usb_close(RtMidiAndroidUsbPort *port)
{
  if (!port) return;
  pthread_mutex_lock(&g_lock);
  Device *d = port->dev;
  if (port->input) {
    pthread_mutex_lock(&d->cb_lock);
    d->cb = NULL;
    d->user = NULL;
    pthread_mutex_unlock(&d->cb_lock);
  }
  if (--d->refs == 0) {
    Device **link = &g_devices;
    while (*link != d) link = &(*link)->next;
    *link = d->next;
    int attached;
    JNIEnv *env = attach(&attached);
    free_device(env, d);
    if (env) detach(attached);
  }
  pthread_mutex_unlock(&g_lock);
  free(port);
}

static int write_packets(Device *d, int fd, unsigned char *packets, size_t len)
{
  struct usbdevfs_bulktransfer bulk;
  bulk.ep = (unsigned int)d->out.address;
  bulk.len = (unsigned int)len;
  bulk.timeout = 100;
  bulk.data = packets;
  return ioctl(fd, USBDEVFS_BULK, &bulk) < 0 ? -1 : 0;
}

int rtmidi_android_usb_send(RtMidiAndroidUsbPort *port, const unsigned char *message, size_t size)
{
  Device *d = port ? port->dev : NULL;
  int fd = d ? atomic_load(&d->fd) : -1;
  if (fd < 0 || d->out.address < 0 || size == 0) return -1;
  unsigned char packets[64];
  size_t len = 0;
  if (message[0] == 0xF0) {
    for (size_t i = 0; i < size;) {
      size_t rest = size - i;
      size_t take = rest > 3 ? 3 : rest;
      unsigned char *p = packets + len;
      memset(p, 0, 4);
      p[0] = rest > 3 ? 0x4 : (unsigned char)(4 + rest);
      memcpy(p + 1, message + i, take);
      i += take;
      len += 4;
      if (len == sizeof(packets) || i == size) {
        if (write_packets(d, fd, packets, len) != 0) return -1;
        len = 0;
      }
    }
    return 0;
  }
  unsigned char status = message[0];
  int cin = status >= 0xF8 ? 0xF
          : status == 0xF2 ? 0x3
          : (status == 0xF1 || status == 0xF3) ? 0x2
          : status >= 0xF0 ? 0x5
          : status >> 4;
  memset(packets, 0, 4);
  packets[0] = (unsigned char)cin;
  memcpy(packets + 1, message, size > 3 ? 3 : size);
  return write_packets(d, fd, packets, 4);
}
