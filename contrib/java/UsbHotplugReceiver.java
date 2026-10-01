package com.yellowlab.rtmidi;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * This class must be included in the Android app when it uses
 * RtMidi::createHotplug() with the Android USB backend. BroadcastReceiver
 * is an abstract class, so the C code cannot provide it through JNI alone.
 */
public class UsbHotplugReceiver extends BroadcastReceiver {
    private long nativeId;

    public UsbHotplugReceiver(long id) {
        nativeId = id;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        devicesChanged(nativeId);
    }

    private native static void devicesChanged(long id);
}
