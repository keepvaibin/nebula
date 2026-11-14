// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.hardware.usb.UsbRequest;
import android.os.Build;
import android.util.Log;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.TimeoutException;

/**
 * Real Wii Remotes on a DolphinBar (its mode 4: a USB device whose HID interfaces are one remote's slot each) for the
 * game's Wii Remote driver (runtime/src/wiimote_hid.cpp through platform/sdl/wiimote_link_android.cpp). Android gives
 * apps no way into a Bluetooth remote's data channel, so this is the way a phone or tablet takes a whole remote: IR
 * pointer, motion, Nunchuk and rumble.
 *
 * A slot is one interface, opened on its own connection: input reports from its interrupt IN endpoint (report id
 * first), output reports by the control pipe as HID SET_REPORT (the bar's interfaces have no OUT endpoint). The
 * driver opens one slot at a time and asks the next when a slot holds no remote.
 */
final class WiimoteUsb {
    private static final String TAG = "NebulaWiimote";
    private static final int VENDOR = 0x057e;
    private static final int PRODUCT = 0x0306;     // RVL-CNT-01, as the bar reports every slot
    private static final int PRODUCT_TR = 0x0330;  // RVL-CNT-01-TR
    // The permission answer's action, under this install's own id (a test build beside the game has another).
    private static final String PERMISSION_SUFFIX = ".WIIMOTE_USB_PERMISSION";
    private static final int READ_WAIT_MS = 250;
    private static final int WRITE_TIMEOUT_MS = 1000;

    private static UsbManager usb;
    private static Context context;
    // Bars asked for permission in this run (Android asks the player; one question per bar).
    private static final Set<Integer> asked = new HashSet<>();
    private static Slot open;

    private WiimoteUsb() {}

    /** From the game's activity, once its libraries are loaded. */
    static synchronized void init(Context activity) {
        if (usb != null) {
            return;
        }
        context = activity.getApplicationContext();
        usb = context.getSystemService(UsbManager.class);
        if (usb == null) {
            return;
        }
        IntentFilter filter = new IntentFilter();
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        filter.addAction(context.getPackageName() + PERMISSION_SUFFIX);
        BroadcastReceiver receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context c, Intent intent) {
                UsbDevice device = deviceOf(intent);
                if (device == null || !isBar(device)) {
                    return;
                }
                if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(intent.getAction())) {
                    detached(device);
                } else {
                    nativeArrived();
                }
            }
        };
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            context.registerReceiver(receiver, filter);
        }
        nativeInit();
    }

    @SuppressWarnings("deprecation")
    private static UsbDevice deviceOf(Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice.class);
        }
        return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
    }

    private static boolean isBar(UsbDevice device) {
        return device.getVendorId() == VENDOR && (device.getProductId() == PRODUCT || device.getProductId() == PRODUCT_TR);
    }

    private static boolean isSlot(UsbInterface iface) {
        if (iface.getInterfaceClass() != UsbConstants.USB_CLASS_HID) {
            return false;
        }
        return inEndpoint(iface) != null;
    }

    private static UsbEndpoint inEndpoint(UsbInterface iface) {
        for (int i = 0; i < iface.getEndpointCount(); i++) {
            UsbEndpoint endpoint = iface.getEndpoint(i);
            if (endpoint.getDirection() == UsbConstants.USB_DIR_IN && endpoint.getType() == UsbConstants.USB_ENDPOINT_XFER_INT) {
                return endpoint;
            }
        }
        return null;
    }

    private static int key(UsbDevice device, int index) {
        return device.getDeviceId() << 4 | index;
    }

    private static UsbDevice device(int key) {
        for (UsbDevice device : usb.getDeviceList().values()) {
            if (device.getDeviceId() == key >>> 4 && isBar(device)) {
                return device;
            }
        }
        return null;
    }

    /** Called from native code: the slots there now, each (device id << 4 | interface index). A bar the app may not
     *  open yet is asked for once; the player's answer counts as an arrival. */
    static synchronized int[] slots() {
        if (usb == null) {
            return new int[0];
        }
        List<Integer> found = new ArrayList<>();
        for (UsbDevice device : usb.getDeviceList().values()) {
            if (!isBar(device)) {
                continue;
            }
            if (!usb.hasPermission(device)) {
                if (asked.add(device.getDeviceId())) {
                    Intent intent = new Intent(context.getPackageName() + PERMISSION_SUFFIX).setPackage(context.getPackageName());
                    usb.requestPermission(device, PendingIntent.getBroadcast(context, device.getDeviceId(), intent,
                            PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT));
                }
                continue;
            }
            for (int i = 0; i < device.getInterfaceCount() && i < 16; i++) {
                if (isSlot(device.getInterface(i))) {
                    found.add(key(device, i));
                }
            }
        }
        int[] out = new int[found.size()];
        for (int i = 0; i < out.length; i++) {
            out[i] = found.get(i);
        }
        return out;
    }

    /** Called from native code: the slot opened (any other closed first); its reports go to nativeReport. */
    static synchronized boolean open(int key) {
        closeOpen();
        UsbDevice device = usb != null ? device(key) : null;
        if (device == null || (key & 15) >= device.getInterfaceCount()) {
            return false;
        }
        UsbInterface iface = device.getInterface(key & 15);
        UsbEndpoint in = inEndpoint(iface);
        if (in == null) {
            return false;
        }
        UsbDeviceConnection connection = usb.openDevice(device);
        if (connection == null) {
            return false;
        }
        // Taken from the system's own driver, which otherwise reads it as a plain controller.
        if (!connection.claimInterface(iface, true)) {
            connection.close();
            return false;
        }
        open = new Slot(key, device, iface, in, connection);
        open.start();
        return true;
    }

    /** Called from native code: one output report (report id first) to the open slot. */
    static boolean write(int key, byte[] report, int length) {
        Slot slot;
        synchronized (WiimoteUsb.class) {
            slot = open;
        }
        if (slot == null || slot.key != key || length < 1) {
            return false;
        }
        int sent = slot.connection.controlTransfer(UsbConstants.USB_TYPE_CLASS | 0x01 /* interface */ | UsbConstants.USB_DIR_OUT,
                0x09 /* SET_REPORT */, 0x0200 /* output */ | (report[0] & 0xff), slot.iface.getId(), report, 0, length,
                WRITE_TIMEOUT_MS);
        return sent >= 0;
    }

    /** Called from native code. */
    static synchronized void close(int key) {
        if (open != null && open.key == key) {
            closeOpen();
        }
    }

    private static void closeOpen() {
        if (open != null) {
            open.finish();
            open = null;
        }
    }

    private static synchronized void detached(UsbDevice device) {
        if (open != null && open.device.getDeviceId() == device.getDeviceId()) {
            nativeGone(open.key);
            closeOpen();
        }
        asked.remove(device.getDeviceId());
    }

    private static final class Slot extends Thread {
        final int key;
        final UsbDevice device;
        final UsbInterface iface;
        final UsbEndpoint in;
        final UsbDeviceConnection connection;
        private volatile boolean running = true;

        Slot(int key, UsbDevice device, UsbInterface iface, UsbEndpoint in, UsbDeviceConnection connection) {
            super("Nebula Wiimote USB");
            this.key = key;
            this.device = device;
            this.iface = iface;
            this.in = in;
            this.connection = connection;
        }

        @Override
        public void run() {
            UsbRequest request = new UsbRequest();
            if (!request.initialize(connection, in)) {
                nativeGone(key);
                return;
            }
            ByteBuffer buffer = ByteBuffer.allocate(Math.max(in.getMaxPacketSize(), 32));
            boolean queued = false;
            while (running) {
                if (!queued) {
                    buffer.clear();
                    if (!request.queue(buffer)) {
                        break;
                    }
                    queued = true;
                }
                UsbRequest done;
                try {
                    done = connection.requestWait(READ_WAIT_MS);
                } catch (TimeoutException e) {
                    continue;
                } catch (RuntimeException e) {
                    Log.w(TAG, "slot read failed", e);
                    break;
                }
                if (done == null) {
                    break;
                }
                if (done == request) {
                    queued = false;
                    if (buffer.position() > 0) {
                        nativeReport(key, buffer.array(), buffer.position());
                    }
                }
            }
            if (running) {
                nativeGone(key);
            }
            request.cancel();
            request.close();
        }

        void finish() {
            running = false;
            try {
                join(READ_WAIT_MS * 4L);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            connection.releaseInterface(iface);
            connection.close();
        }
    }

    private static native void nativeInit();

    private static native void nativeArrived();

    private static native void nativeReport(int key, byte[] report, int length);

    private static native void nativeGone(int key);
}
