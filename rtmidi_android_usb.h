#ifndef RTMIDI_ANDROID_USB_H
#define RTMIDI_ANDROID_USB_H

#include <stddef.h>
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RtMidiAndroidUsbPort RtMidiAndroidUsbPort;
typedef void (*RtMidiAndroidUsbCallback)(double timeStamp, const unsigned char *message, size_t size, void *userData);

void rtmidi_android_usb_set_java_context(JavaVM *vm, jobject context);
unsigned int rtmidi_android_usb_port_count(int input);
int rtmidi_android_usb_port_name(int input, unsigned int portNumber, char *buf, size_t len);
RtMidiAndroidUsbPort *rtmidi_android_usb_open(int input, unsigned int portNumber, RtMidiAndroidUsbCallback callback, void *userData);
void rtmidi_android_usb_close(RtMidiAndroidUsbPort *port);
int rtmidi_android_usb_send(RtMidiAndroidUsbPort *port, const unsigned char *message, size_t size);

typedef struct RtMidiAndroidUsbHotplug RtMidiAndroidUsbHotplug;
typedef void (*RtMidiAndroidUsbHotplugCallback)(void *userData);

RtMidiAndroidUsbHotplug *rtmidi_android_usb_hotplug_create(RtMidiAndroidUsbHotplugCallback callback, void *userData);
void rtmidi_android_usb_hotplug_destroy(RtMidiAndroidUsbHotplug *hotplug);

#ifdef __cplusplus
}
#endif

#endif
