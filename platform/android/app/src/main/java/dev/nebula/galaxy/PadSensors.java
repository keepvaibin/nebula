// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Process;
import android.util.SparseArray;
import android.view.InputDevice;

/**
 * Controllers' own motion sensors (InputDevice.getSensorManager): Android lists a controller's gyroscope and
 * accelerometer when its kernel driver reports them (hid-nintendo for Switch controllers, hid-playstation for
 * DualShock 4 and DualSense). Every sample is taken, as fast as the controller sends them (SENSOR_DELAY_FASTEST: a Pro
 * Controller's three per report, 200 a second), on a thread of its own, with the sensor's own time. A controller whose
 * sensors appear after it connects (a driver adds them as a second input device) is picked up when Android reports the
 * change. Values are Android's: metres a second squared and radians a second, on the axes the driver reports.
 * A controller's motion sensors that Android lists as a device of their own (a driver's IMU node it does not take as
 * part of the controller) are read too: an external device with a vendor and product id and motion sensors.
 *
 * Android's kernels before Linux 6.1 (android12-5.10, android13-5.15, android14-5.15) carry hid-nintendo without its
 * IMU: a Switch controller over Bluetooth has no motion for any app there. Over a cable SDL reads the controller's own
 * reports (HIDAPI), motion included.
 */
public final class PadSensors implements InputManager.InputDeviceListener {
    /** What the sensors report, on the sensor thread. */
    public interface Sink {
        /** A controller as Android lists it, when it is first seen and whenever its sensors change. */
        void device(InputDevice device, Sensor gyroscope, Sensor accelerometer);

        void removed(int deviceId);

        /** One sample: type is Sensor.TYPE_GYROSCOPE or Sensor.TYPE_ACCELEROMETER. */
        void sample(int deviceId, int type, long timeNs, float x, float y, float z);
    }

    private static final class Listening implements SensorEventListener {
        final int deviceId;
        final Sink sink;
        SensorManager manager;
        Sensor gyroscope;
        Sensor accelerometer;

        Listening(int deviceId, Sink sink) {
            this.deviceId = deviceId;
            this.sink = sink;
        }

        @Override
        public void onSensorChanged(SensorEvent event) {
            if (event.values.length < 3) {
                return;
            }
            sink.sample(deviceId, event.sensor.getType(), event.timestamp, event.values[0], event.values[1], event.values[2]);
        }

        @Override
        public void onAccuracyChanged(Sensor sensor, int accuracy) {
        }

        void stop() {
            if (manager != null) {
                manager.unregisterListener(this);
            }
        }
    }

    private final Context context;
    private final Sink sink;
    private final SparseArray<Listening> listening = new SparseArray<>();
    private HandlerThread thread;
    private Handler handler;
    private InputManager input;

    /** `context` is read at start(), not here: an activity's own field is made before the activity has its context. */
    public PadSensors(Context context, Sink sink) {
        this.context = context;
        this.sink = sink;
    }

    /** A controller (a gamepad or joystick that is a real device). */
    public static boolean isController(InputDevice device) {
        if (device == null || device.isVirtual()) {
            return false;
        }
        final int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
            || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    /** A device that is not a controller but carries a controller's motion sensors (vendor and product ids). */
    private static boolean isMotionNode(InputDevice device) {
        if (device == null || device.isVirtual() || !device.isExternal() || device.getVendorId() == 0) {
            return false;
        }
        final SensorManager manager = device.getSensorManager();
        return manager != null && (manager.getDefaultSensor(Sensor.TYPE_GYROSCOPE) != null
            || manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) != null);
    }

    public synchronized void start() {
        if (thread != null) {
            return;
        }
        thread = new HandlerThread("pad-sensors", Process.THREAD_PRIORITY_URGENT_DISPLAY);
        thread.start();
        handler = new Handler(thread.getLooper());
        input = context.getApplicationContext().getSystemService(InputManager.class);
        if (input != null) {
            input.registerInputDeviceListener(this, handler);
        }
        handler.post(() -> {
            for (final int id : InputDevice.getDeviceIds()) {
                refresh(id);
            }
        });
    }

    public synchronized void stop() {
        if (thread == null) {
            return;
        }
        if (input != null) {
            input.unregisterInputDeviceListener(this);
        }
        final HandlerThread ending = thread;
        handler.post(() -> {
            for (int i = 0; i < listening.size(); ++i) {
                listening.valueAt(i).stop();
            }
            listening.clear();
            ending.quitSafely();
        });
        thread = null;
        handler = null;
    }

    // On the sensor thread.
    private void refresh(int deviceId) {
        final InputDevice device = InputDevice.getDevice(deviceId);
        if (!isController(device) && !isMotionNode(device)) {
            drop(deviceId);
            return;
        }
        final SensorManager manager = device.getSensorManager();
        final Sensor gyroscope = manager != null ? manager.getDefaultSensor(Sensor.TYPE_GYROSCOPE) : null;
        final Sensor accelerometer = manager != null ? manager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) : null;
        Listening entry = listening.get(deviceId);
        if (entry != null && entry.gyroscope == gyroscope && entry.accelerometer == accelerometer) {
            return;
        }
        if (entry != null) {
            entry.stop();
        } else {
            entry = new Listening(deviceId, sink);
            listening.put(deviceId, entry);
        }
        entry.manager = manager;
        entry.gyroscope = gyroscope;
        entry.accelerometer = accelerometer;
        // The accelerometer first, so gravity is known by the first turn.
        if (accelerometer != null) {
            manager.registerListener(entry, accelerometer, SensorManager.SENSOR_DELAY_FASTEST, handler);
        }
        if (gyroscope != null) {
            manager.registerListener(entry, gyroscope, SensorManager.SENSOR_DELAY_FASTEST, handler);
        }
        sink.device(device, gyroscope, accelerometer);
    }

    private void drop(int deviceId) {
        final Listening entry = listening.get(deviceId);
        if (entry != null) {
            entry.stop();
            listening.remove(deviceId);
            sink.removed(deviceId);
        }
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        refresh(deviceId);
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        refresh(deviceId);
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        drop(deviceId);
    }
}
