// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.display.DisplayManager;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.List;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * The game: SDL's activity running libmain.so (native/host/host_main.cpp built by platform/sdl), with the touch
 * controls and the HOME Menu over the picture. It has its own process, which ends with the game.
 */
public class GameActivity extends SDLActivity implements SensorEventListener {
    private static final String TAG = "Nebula";
    private SensorManager sensors;
    private final float[] phoneGyro = new float[3];
    // Controllers' own motion sensors, every sample, to libmain.so.
    private final PadSensors padSensors = new PadSensors(this, new PadSensors.Sink() {
        @Override
        public void device(InputDevice device, Sensor gyroscope, Sensor accelerometer) {
            nativePadSensorDevice(device.getId(), device.getName(), device.getDescriptor(), device.getVendorId(),
                device.getProductId(), gyroscope != null, accelerometer != null);
        }

        @Override
        public void removed(int deviceId) {
            nativePadSensorRemoved(deviceId);
        }

        @Override
        public void sample(int deviceId, int type, long timeNs, float x, float y, float z) {
            nativePadSensor(deviceId, type, timeNs, x, y, z);
        }
    });

    // libmain.so (platform/sdl/host_window_sdl.cpp).
    static native void nativeTouchControls(int buttons, float stickX, float stickY);
    static native void nativeTouchSettings(int sizePercent, int opacityPercent, boolean vibration);
    static native void nativeArrangingDone();
    static native int nativeTouchWanted(int which);
    static native void nativeInputDevices(boolean keyboard, boolean mouse);
    static native int nativeInputMode();
    // As RuntimeInputMode.
    private static final int INPUT_AUTO = 0;
    private static final int INPUT_PHONE = 4;
    static native void nativePointer(int action, int id, float x, float y);
    static native boolean nativeMenuContext();
    static native boolean nativeTitleScreen();
    static native boolean nativeTouchTargetAt(float x, float y);
    static native void nativeSetVisible(boolean visible);
    static native void nativeSurfaceWithdrawn();
    static native void nativeSurfaceReturned();
    static native void nativePhoneMotion(float ax, float ay, float az, float gx, float gy, float gz, long timeNs);
    static native void nativePanelRate(float hz);
    static native void nativeTouchSlop(float px);
    static native float nativeWantedFrameRate();
    static native void nativePadSensorDevice(int id, String name, String descriptor, int vendor, int product, boolean gyro, boolean accel);
    static native void nativePadSensorRemoved(int id);
    static native void nativePadSensor(int id, int type, long timeNs, float x, float y, float z);
    static native void nativeFilesPicked(byte[][] paths);

    // The settings' Import (pickFiles): its own request code beside SDL's.
    private static final int PICK_FILES_REQUEST = 0x4E42;
    private int pickKind;

    private TouchControlsView controls;
    private boolean menuOpen;
    private String controlsMode = "auto";
    private boolean realWiimote;  // a real Wii Remote (WiimoteUsb) is in the player's hand
    private boolean controlsWanted = true;   // the player's choice in the HOME Menu

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    // A shell-launched profiling run uses copied saves/settings; never the player's writable data.
    // BuildConfig.DEBUG is false (and this path removed by R8) in distributable release builds.
    private boolean isProfileRun() {
        return BuildConfig.DEBUG && getIntent().getBooleanExtra("nebula.profile", false);
    }

    // A harness run (plan 8.2 b; debug builds only, as the profiling run): the test runner's folder under the app's
    // private data, pushed there over adb (agents/luma/d/platforms/android/suite_android.py) and named by the
    // intent's "nebula.harness". A name with anything but letters, digits, '.', '_' and '-' is no harness run.
    private String harnessRun() {
        if (!BuildConfig.DEBUG) {
            return null;
        }
        final String name = getIntent().getStringExtra("nebula.harness");
        return name != null && name.matches("[A-Za-z0-9._-]{1,64}") ? name : null;
    }

    private File harnessFolder() {
        return new File(new File(GameFiles.data(this), "harness"), harnessRun());
    }

    private File nativeData() {
        if (harnessRun() != null) {
            return new File(harnessFolder(), "appdata");
        }
        return isProfileRun() ? new File(GameFiles.data(this), "profiling") : GameFiles.data(this);
    }

    // The desktop runner's names for a run's private folders (luma_test.py run_one): the test extension's data guard
    // declines to start without them.
    private void passHarness(File run) {
        final String[][] names = {
            {"NEBULA_WORK", run.getPath()},
            {"NEBULA_SETTINGS", new File(run, "settings").getPath()},
            {"NEBULA_SAVES", new File(run, "saves").getPath()},
            {"NEBULA_APPDATA", new File(run, "appdata").getPath()},
            {"GALAXY_RENDER_CONFIG_PATH", new File(run, "settings/config.ini").getPath()},
            {"GALAXY_RUNTIME_SETTINGS_PATH", new File(run, "settings/runtime_settings.ini").getPath()},
            {"GALAXY_CRASH_DUMP_DIR", run.getPath()},
            {"GALAXY_AUDIO_WAV_DUMP_ONLY", "1"},
            {"GALAXY_AUDIO_WAV_DUMP", new File(run, "audio.wav").getPath()},
            {"GALAXY_AUDIO_WAV_DUMP_SECONDS", String.valueOf(getIntent().getIntExtra("nebula.seconds", 60))},
            {"LUMA_HARNESS_PLAN", new File(run, "plan.txt").getPath()},
        };
        try {
            for (String[] pair : names) {
                android.system.Os.setenv(pair[0], pair[1], true);
            }
        } catch (android.system.ErrnoException failed) {
            throw new IllegalStateException("harness run: the environment could not be set", failed);
        }
    }

    /** The game's language follows the device's where the disc has it (runtime_settings.cpp reads LANG). */
    private static void passLocale() {
        try {
            android.system.Os.setenv("LANG", java.util.Locale.getDefault().getLanguage(), true);
        } catch (android.system.ErrnoException ignored) {
            // No language passed: the game plays in English.
        }
    }

    @Override
    protected String[] getArguments() {
        passLocale();
        if (harnessRun() != null) {
            // The desktop runner's command (luma_test.py run_one), with this app's game module and test extension.
            final File run = harnessFolder();
            passHarness(run);
            final String libraries = getApplicationInfo().nativeLibraryDir;
            return new String[] {
                "--data", new File(run, "appdata").getPath(),
                "--pak", GameFiles.pak(this).getPath(),
                "--saves", new File(run, "saves").getPath(),
                "--settings", new File(run, "settings").getPath(),
                "--seconds", String.valueOf(getIntent().getIntExtra("nebula.seconds", 60)),
                "--crash-dumps", run.getPath(),
                "--module", libraries + "/libsmg_game.so",
                "--extension", libraries + "/libnebula_test_extension.so",
                "--diagnostics", "0",
            };
        }
        // The launcher's Record diagnostics: this run only.
        final SharedPreferences prefs = GameFiles.controls(this);
        final boolean record = isProfileRun() || prefs.getBoolean(GameFiles.RECORD_NEXT_RUN, false);
        if (record && !isProfileRun()) {
            prefs.edit().remove(GameFiles.RECORD_NEXT_RUN).commit();
        }
        // The native game's settings, saves and log: the app's private storage.
        return new String[] {
            "--data", nativeData().getPath(),
            "--pak", GameFiles.pak(this).getPath(),
            "--saves", GameFiles.savesIn(nativeData()).getPath(),
            "--module", getApplicationInfo().nativeLibraryDir + "/libsmg_game.so",
            "--diagnostics", record ? "1" : "0",
        };
    }

    // The frame-rate setting saved last (config.ini max_fps; 0: the display's own), before the game has read it.
    private float savedFrameRate() {
        float maxFps = 60.0f;
        try (BufferedReader reader = new BufferedReader(new FileReader(new File(nativeData(), "config.ini")))) {
            for (String line; (line = reader.readLine()) != null; ) {
                if (line.startsWith("max_fps=")) {
                    maxFps = Float.parseFloat(line.substring("max_fps=".length()).trim());
                }
            }
        } catch (Exception e) {
            return 60.0f;
        }
        return maxFps;
    }

    // The display mode for a frame-rate setting (max_fps; 0: the display's own). Interpolated rates take the mode that
    // shows them (DisplayRate.modeFor): without the request most variable-rate panels hold the game at 60 Hz. The
    // game's own 30 or 60 frames leave the mode to the system.
    private void applyDisplayRate(float maxFps) {
        final Display display = getDisplay();
        if (display == null) {
            return;
        }
        final boolean own = maxFps > 0.0f && maxFps <= 61.0f;
        final Display.Mode mode = own ? null : DisplayRate.modeFor(display, maxFps);
        final int wanted = mode != null ? mode.getModeId() : 0;
        final WindowManager.LayoutParams attributes = getWindow().getAttributes();
        if (attributes.preferredDisplayModeId == wanted) {
            return;
        }
        attributes.preferredDisplayModeId = wanted;
        getWindow().setAttributes(attributes);
        Log.i(TAG, "display mode " + (mode != null ? mode.getRefreshRate() + " Hz" : "the system's") + " for frame rate " +
                maxFps + " (was " + display.getMode().getRefreshRate() + " Hz)");
    }

    // The panel's fastest rate, which the game's settings offer interpolation by.
    private void reportPanelRate() {
        final Display display = getDisplay();
        if (display != null) {
            nativePanelRate(DisplayRate.fastestMode(display).getRefreshRate());
        }
    }

    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override
        public void onDisplayAdded(int displayId) {}

        @Override
        public void onDisplayRemoved(int displayId) {}

        @Override
        public void onDisplayChanged(int displayId) {
            final Display display = getDisplay();
            if (display != null && display.getDisplayId() == displayId) {
                reportPanelRate();
            }
        }
    };

    // Called by libmain.so when the frame-rate setting changes (on SDL's thread).
    public void displayRateChanged() {
        final float maxFps = nativeWantedFrameRate();
        runOnUiThread(() -> applyDisplayRate(maxFps));
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new GameSurface(context);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        GameFiles.lastError(this).delete();
        // The last run's picked copies were imported then.
        deleteTree(pickedFolder());
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries || mLayout == null) {
            return;
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        WiimoteUsb.init(this);
        reportPanelRate();
        nativeTouchSlop(ViewConfiguration.get(this).getScaledTouchSlop());
        getSystemService(DisplayManager.class).registerDisplayListener(displayListener, null);
        applyDisplayRate(savedFrameRate());

        final SharedPreferences prefs = GameFiles.controls(this);
        controls = new TouchControlsView(this, new TouchControlsView.Listener() {
            @Override
            public void onControls(int buttons, float stickX, float stickY) {
                nativeTouchControls(buttons, stickX, stickY);
            }

            @Override
            public void onPointer(int action, int id, float x, float y) {
                if (action == 0) {
                    revealControls();
                }
                nativePointer(action, id, x, y);
            }

            @Override
            public boolean titleScreen() {
                return nativeTitleScreen();
            }

            @Override
            public boolean touchTarget(float x, float y) {
                return nativeTouchTargetAt(x, y);
            }

            @Override
            public boolean menuContext() {
                return nativeMenuContext();
            }

            @Override
            public void arrangingDone() {
                nativeArrangingDone();
                applyControlsVisibility();
            }

            @Override
            public void touchSettings(int sizePercent, int opacityPercent, boolean haptics) {
                GameFiles.controls(GameActivity.this).edit().putFloat(GameFiles.CONTROLS_OPACITY, opacityPercent / 100.0f)
                        .putBoolean(GameFiles.CONTROLS_HAPTICS, haptics).apply();
                nativeTouchSettings(sizePercent, opacityPercent, haptics);
            }
        });
        controls.configure(prefs.getFloat(GameFiles.CONTROLS_OPACITY, 0.6f), prefs.getBoolean(GameFiles.CONTROLS_HAPTICS, true));
        mLayout.addView(controls, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        reportInputDevices();
        applyInputMode();
        final InputManager input = getSystemService(InputManager.class);
        input.registerInputDeviceListener(new InputManager.InputDeviceListener() {
            @Override
            public void onInputDeviceAdded(int deviceId) {
                reportInputDevices();
                if (nativeInputMode() == INPUT_AUTO && isController(InputDevice.getDevice(deviceId))) {
                    hideControlsForController();
                }
            }

            @Override
            public void onInputDeviceRemoved(int deviceId) {
                reportInputDevices();
                if (nativeInputMode() == INPUT_AUTO && !controllerConnected()) {
                    revealControls();
                }
            }

            @Override
            public void onInputDeviceChanged(int deviceId) {}
        }, null);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            // The whole display for the picture; the bars come back with a swipe from the edge.
            final WindowInsetsController insets = getWindow().getInsetsController();
            if (insets != null) {
                insets.hide(WindowInsets.Type.systemBars());
                insets.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    // Out of sight, the game waits (the host stops its clocks); in multi-window it plays on while visible.
    @Override
    protected void onStart() {
        super.onStart();
        if (!mBrokenLibraries) {
            nativeSetVisible(true);
            listenToPhoneMotion(true);
            padSensors.start();
        }
    }

    // The phone's own sensors: tilt for the sections the game steers by tilting, a shake for the spin.
    private void listenToPhoneMotion(boolean listen) {
        if (sensors == null) {
            sensors = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        }
        if (sensors == null) {
            return;
        }
        if (!listen) {
            sensors.unregisterListener(this);
            return;
        }
        final Sensor accelerometer = sensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
        final Sensor gyroscope = sensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
        if (accelerometer != null) {
            sensors.registerListener(this, accelerometer, SensorManager.SENSOR_DELAY_GAME);
        }
        if (gyroscope != null) {
            sensors.registerListener(this, gyroscope, SensorManager.SENSOR_DELAY_GAME);
        }
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        // From the phone's axes to the screen's as it is turned: x right, y up, z out of the glass.
        final Display display = getDisplay();
        final int rotation = display != null ? display.getRotation() : Surface.ROTATION_0;
        final float dx = event.values[0];
        final float dy = event.values[1];
        final float x = rotation == Surface.ROTATION_90 ? -dy : rotation == Surface.ROTATION_180 ? -dx : rotation == Surface.ROTATION_270 ? dy : dx;
        final float y = rotation == Surface.ROTATION_90 ? dx : rotation == Surface.ROTATION_180 ? -dy : rotation == Surface.ROTATION_270 ? -dx : dy;
        final float z = event.values[2];
        if (event.sensor.getType() == Sensor.TYPE_GYROSCOPE) {
            phoneGyro[0] = x;
            phoneGyro[1] = y;
            phoneGyro[2] = z;
        } else {
            nativePhoneMotion(x, y, z, phoneGyro[0], phoneGyro[1], phoneGyro[2], event.timestamp);
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    @Override
    protected void onStop() {
        if (!mBrokenLibraries) {
            listenToPhoneMotion(false);
            padSensors.stop();
            nativeSetVisible(false);
        }
        super.onStop();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        // Another density (an external display, a desktop mode): its touch slop.
        if (!mBrokenLibraries) {
            nativeTouchSlop(ViewConfiguration.get(this).getScaledTouchSlop());
        }
    }

    @Override
    protected void onDestroy() {
        if (!mBrokenLibraries) {
            getSystemService(DisplayManager.class).unregisterDisplayListener(displayListener);
        }
        super.onDestroy();
        // One game per process: the native game cannot start twice in it.
        if (isFinishing()) {
            Process.killProcess(Process.myPid());
        }
    }

    // Called by libmain.so (host_window_sdl.cpp, on SDL's thread).
    public void menuOpened() {
        runOnUiThread(() -> {
            menuOpen = true;
            applyControlsVisibility();
        });
    }

    // Keyboards and mice that are really attached: not the phone's own buttons, not the screen's virtual keyboard.
    private void reportInputDevices() {
        boolean keyboard = false;
        boolean mouse = false;
        for (int id : InputDevice.getDeviceIds()) {
            final InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual() || !device.isExternal()) {
                continue;
            }
            keyboard |= device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC;
            mouse |= (device.getSources() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE;
        }
        nativeInputDevices(keyboard, mouse);
    }

    // What the input mode says of the on-screen controls: Phone always, Auto while no controller is in hand, others never.
    private void applyInputMode() {
        final int mode = nativeInputMode();
        controlsWanted = mode == INPUT_PHONE || (mode == INPUT_AUTO && !controllerConnected() && !realWiimote);
        applyControlsVisibility();
    }

    // Called by libmain.so: the player took up a real Wii Remote, or let it go (on its input thread).
    public void realWiimoteTaken() {
        runOnUiThread(() -> {
            realWiimote = true;
            hideControlsForController();
        });
    }

    public void realWiimoteLetGo() {
        runOnUiThread(() -> {
            realWiimote = false;
            applyInputMode();
        });
    }

    // Called by libmain.so when the player changes the input mode (on SDL's thread).
    public void inputModeChanged() {
        runOnUiThread(this::applyInputMode);
    }

    // Called by libmain.so: CONTROLS' Arrange (on SDL's thread). The game's settings window shows the editor's words
    // and its Save, Reset and Cancel until nativeArrangingDone.
    public void arrangeControls() {
        runOnUiThread(() -> {
            if (controls != null) {
                controls.startArranging();
            } else {
                nativeArrangingDone();
            }
        });
    }

    // Called by libmain.so: the editor's answers in the game's window (on SDL's thread).
    public void saveArrangedControls() {
        runOnUiThread(() -> {
            if (controls != null && controls.isArranging()) {
                controls.finishArranging(true);
            }
        });
    }

    public void cancelArrangedControls() {
        runOnUiThread(() -> {
            if (controls != null && controls.isArranging()) {
                controls.finishArranging(false);
            }
        });
    }

    public void resetArrangedControls() {
        runOnUiThread(() -> {
            if (controls != null && controls.isArranging()) {
                controls.resetArranged();
            }
        });
    }

    // Called by libmain.so: CONTROLS' on-screen control rows, applied (on SDL's thread).
    public void touchSettingsChanged() {
        final int size = nativeTouchWanted(0);
        final int opacity = nativeTouchWanted(1);
        final boolean vibration = nativeTouchWanted(2) != 0;
        runOnUiThread(() -> {
            if (controls != null) {
                controls.applySettings(size, opacity, vibration);
            }
        });
    }

    // Called by libmain.so: CONTROLS' BUTTON PLACES reset, applied (on SDL's thread).
    public void resetControlPlaces() {
        runOnUiThread(() -> {
            if (controls != null) {
                controls.resetPlaces();
            }
        });
    }

    // Called by libmain.so: the settings' Import, Miis (kind 0) or mods and texture packs (1), on SDL's thread. The
    // chosen documents are copied where the game reads them by path, then handed over (nativeFilesPicked); none when
    // the picker is cancelled or cannot open.
    public void pickFiles(int kind) {
        runOnUiThread(() -> {
            pickKind = kind;
            final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
            try {
                startActivityForResult(intent, PICK_FILES_REQUEST);
            } catch (android.content.ActivityNotFoundException e) {
                Log.e(TAG, "no document picker", e);
                nativeFilesPicked(new byte[0][]);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_FILES_REQUEST) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        final java.util.ArrayList<android.net.Uri> uris = new java.util.ArrayList<>();
        if (resultCode == RESULT_OK && data != null) {
            if (data.getClipData() != null) {
                for (int i = 0; i < data.getClipData().getItemCount(); ++i) {
                    uris.add(data.getClipData().getItemAt(i).getUri());
                }
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
        }
        final File folder = new File(pickedFolder(), pickKind == 0 ? "miis" : "mods");
        // Copied off the UI thread: a texture pack can be large.
        new Thread(() -> {
            deleteTree(folder);
            final java.util.ArrayList<byte[]> paths = new java.util.ArrayList<>();
            for (int i = 0; i < uris.size(); ++i) {
                final File copy = copyDocument(uris.get(i), new File(folder, Integer.toString(i)));
                if (copy != null) {
                    paths.add(copy.getPath().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                }
            }
            nativeFilesPicked(paths.toArray(new byte[0][]));
        }, "Nebula Picked Files").start();
    }

    private File pickedFolder() {
        return new File(nativeData(), ".picked");
    }

    // A document into `folder` under its own name; null when it cannot be read or written.
    private File copyDocument(android.net.Uri uri, File folder) {
        String name = null;
        try (android.database.Cursor cursor = getContentResolver().query(uri,
                new String[] {android.provider.OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst() && !cursor.isNull(0)) {
                name = cursor.getString(0);
            }
        } catch (Exception e) {
            Log.w(TAG, "no name for a picked file", e);
        }
        name = name == null ? "" : name.replaceAll("[/\\\\:*?\"<>|]", "_").trim();
        if (name.isEmpty()) {
            name = "file";
        }
        if (!folder.mkdirs() && !folder.isDirectory()) {
            return null;
        }
        final File copy = new File(folder, name);
        try (InputStream from = getContentResolver().openInputStream(uri);
             OutputStream to = new FileOutputStream(copy)) {
            if (from == null) {
                return null;
            }
            final byte[] buffer = new byte[1 << 20];
            for (int read; (read = from.read(buffer)) >= 0; ) {
                to.write(buffer, 0, read);
            }
        } catch (Exception e) {
            Log.e(TAG, "a picked file could not be copied", e);
            copy.delete();
            return null;
        }
        return copy;
    }

    private static void deleteTree(File file) {
        final File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        file.delete();
    }

    // Called by libmain.so: the game starts again, straight back into the game (on SDL's thread). One game per process, so
    // a new process takes over this one's task.
    public void restartGame() {
        runOnUiThread(() -> {
            final Intent intent = new Intent(getIntent());
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(intent);
            overridePendingTransition(0, 0);
            Process.killProcess(Process.myPid());
        });
    }

    // Called by libmain.so (host_window_sdl.cpp, on SDL's thread).
    public void menuContextChanged() {
        runOnUiThread(() -> {
            if (controls != null) {
                controls.syncMenuContext();
            }
        });
    }

    // Called by libmain.so (host_window_sdl.cpp, on SDL's thread).
    public void menuClosed() {
        runOnUiThread(() -> {
            menuOpen = false;
            applyControlsVisibility();
        });
    }

    private void applyControlsVisibility() {
        if (controls != null) {
            controls.setControlsShown(controlsWanted && !menuOpen);
        }
    }

    private void revealControls() {
        if (nativeInputMode() == INPUT_AUTO && !controlsWanted) {
            controlsWanted = true;
            applyControlsVisibility();
        }
    }

    private void hideControlsForController() {
        if (controlsWanted) {
            controlsWanted = false;
            applyControlsVisibility();
        }
    }

    private static boolean isController(InputDevice device) {
        if (device == null || device.isVirtual()) {
            return false;
        }
        final int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private static boolean controllerConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            if (isController(InputDevice.getDevice(id))) {
                return true;
            }
        }
        return false;
    }

    private static boolean fromController(int source) {
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
                || (source & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (controls != null && controls.isArranging() && event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            // Back is the phone's own Cancel; a keyboard's and a controller's keys reach the editor's window in the game.
            if (event.getAction() == KeyEvent.ACTION_UP) {
                controls.finishArranging(false);
            }
            return true;
        }
        if (fromController(event.getSource()) && "auto".equals(controlsMode)) {
            hideControlsForController();
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (fromController(event.getSource()) && "auto".equals(controlsMode)) {
            hideControlsForController();
        }
        return super.dispatchGenericMotionEvent(event);
    }

    /** SDL's surface, telling the renderer when it goes and comes back (host_window.h's surface withdrawal). */
    static final class GameSurface extends SDLSurface {
        GameSurface(Context context) {
            super(context);
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            nativeSurfaceWithdrawn();
            super.surfaceDestroyed(holder);
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            super.surfaceChanged(holder, format, width, height);
            if (mIsSurfaceReady) {
                nativeSurfaceReturned();
            }
        }
    }
}
