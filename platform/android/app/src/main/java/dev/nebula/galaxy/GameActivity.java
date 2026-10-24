// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.input.InputManager;
import android.os.Bundle;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
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
public class GameActivity extends SDLActivity {
    private static final String TAG = "Nebula";

    // libmain.so (platform/sdl/host_window_sdl.cpp).
    static native void nativeTouchControls(int buttons, float stickX, float stickY);
    static native void nativePointer(int action, int id, float x, float y);
    static native boolean nativeMenuContext();
    static native void nativeHomeChoice(int choice);
    static native void nativeSetVisible(boolean visible);
    static native void nativeSurfaceWithdrawn();
    static native void nativeSurfaceReturned();

    private TouchControlsView controls;
    private HomeMenuView homeMenu;
    private String controlsMode = "auto";
    private boolean controlsWanted = true;   // the player's choice in the HOME Menu
    private float menuAxis;                  // a controller stick's last direction in the HOME Menu

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected String[] getArguments() {
        return new String[] {
            "--pak", GameFiles.pak(this).getPath(),
            "--nand", GameFiles.nand(this).getPath(),
            "--module", getApplicationInfo().nativeLibraryDir + "/libsmg_game.so",
        };
    }

    // The pipelines the whole game uses and their precompiled shaders (assets/vk_seed_*.bin) beside the pipeline cache,
    // which builds them in the background from the first launch and then deletes them, keeping only what this GPU
    // uses (PipelineCacheVk). Installed once per app update.
    private void installPipelineSeed() {
        final SharedPreferences prefs = getSharedPreferences("pipeline_seed", MODE_PRIVATE);
        try {
            final long installed = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
            if (prefs.getLong("installed_for", 0L) == installed) {
                return;
            }
            final File dir = new File(GameFiles.data(this), "shadercache/RMGE01");
            if (!dir.isDirectory() && !dir.mkdirs()) {
                return;
            }
            final List<String> shipped = Arrays.asList(getAssets().list(""));
            for (final String name : new String[] {"vk_seed_shaders.bin", "vk_seed_keys.bin"}) {
                // A seed of an earlier version goes; this version's, if it has one, replaces it.
                new File(dir, name).delete();
                if (!shipped.contains(name)) {
                    continue;
                }
                final File partial = new File(dir, name + ".tmp");
                try (InputStream in = getAssets().open(name);
                     OutputStream out = new FileOutputStream(partial)) {
                    final byte[] buffer = new byte[1 << 16];
                    for (int read; (read = in.read(buffer)) > 0; ) {
                        out.write(buffer, 0, read);
                    }
                }
                if (!partial.renameTo(new File(dir, name))) {
                    partial.delete();
                    return;
                }
            }
            prefs.edit().putLong("installed_for", installed).apply();
        } catch (Exception e) {
            Log.w(TAG, "pipeline seed not installed", e);
        }
    }

    // Frame rate set to the display's own (config.ini max_fps=0): DisplayRate's mode, so the renderer draws the frames in
    // between at its rate. Without the request Android may hold the game at 60 Hz.
    private void requestDisplayRate() {
        String maxFps = "60";
        try (BufferedReader reader = new BufferedReader(new FileReader(GameFiles.renderConfig(this)))) {
            for (String line; (line = reader.readLine()) != null; ) {
                if (line.startsWith("max_fps=")) {
                    maxFps = line.substring("max_fps=".length()).trim();
                }
            }
        } catch (Exception e) {
            return;
        }
        final Display display = getDisplay();
        if (display == null || !"0".equals(maxFps)) {
            return;
        }
        final Display.Mode current = display.getMode();
        final Display.Mode best = DisplayRate.fastestMode(display);
        final WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.preferredDisplayModeId = best.getModeId();
        getWindow().setAttributes(attributes);
        Log.i(TAG, "display mode " + best.getRefreshRate() + " Hz requested (was " + current.getRefreshRate() + " Hz)");
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new GameSurface(context);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // The native game's settings, saves and log: the app's private storage.
        try {
            Os.setenv("NEBULA_DATA_DIR", GameFiles.data(this).getPath(), true);
        } catch (ErrnoException e) {
            Log.e(TAG, "NEBULA_DATA_DIR not set", e);
        }
        GameFiles.lastError(this).delete();
        installPipelineSeed();
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries || mLayout == null) {
            return;
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        requestDisplayRate();

        final SharedPreferences prefs = GameFiles.controls(this);
        controlsMode = prefs.getString(GameFiles.CONTROLS_MODE, "auto");
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
            public boolean menuContext() {
                return nativeMenuContext();
            }
        });
        controls.configure(prefs.getFloat(GameFiles.CONTROLS_OPACITY, 0.6f),
                prefs.getFloat(GameFiles.CONTROLS_SCALE, 1.0f), prefs.getBoolean(GameFiles.CONTROLS_HAPTICS, true));
        mLayout.addView(controls, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        homeMenu = new HomeMenuView(this, new HomeMenuView.Listener() {
            @Override
            public void onHomeChoice(int choice) {
                nativeHomeChoice(choice);
                applyControlsVisibility();
            }

            @Override
            public void onTouchControlsToggled(boolean shown) {
                controlsWanted = shown;
            }
        });
        mLayout.addView(homeMenu, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        controlsWanted = !"never".equals(controlsMode) && !("auto".equals(controlsMode) && controllerConnected());
        homeMenu.setControlsShown(controlsWanted);
        applyControlsVisibility();
        final InputManager input = getSystemService(InputManager.class);
        input.registerInputDeviceListener(new InputManager.InputDeviceListener() {
            @Override
            public void onInputDeviceAdded(int deviceId) {
                if ("auto".equals(controlsMode) && isController(InputDevice.getDevice(deviceId))) {
                    hideControlsForController();
                }
            }

            @Override
            public void onInputDeviceRemoved(int deviceId) {
                if ("auto".equals(controlsMode) && !controllerConnected()) {
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
        }
    }

    @Override
    protected void onStop() {
        if (!mBrokenLibraries) {
            nativeSetVisible(false);
        }
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // One game per process: the native game cannot start twice in it.
        if (isFinishing()) {
            Process.killProcess(Process.myPid());
        }
    }

    // Called by libmain.so (host_window_sdl.cpp, on SDL's thread).
    public void showHomeMenu() {
        runOnUiThread(() -> {
            if (homeMenu != null) {
                controls.setControlsShown(false);
                homeMenu.setControlsShown(controlsWanted);
                homeMenu.show();
            }
        });
    }

    // Called by libmain.so (host_window_sdl.cpp, on SDL's thread).
    public void hideHomeMenu() {
        runOnUiThread(() -> {
            if (homeMenu != null) {
                homeMenu.hide();
                applyControlsVisibility();
            }
        });
    }

    private void applyControlsVisibility() {
        if (controls != null) {
            controls.setControlsShown(controlsWanted && (homeMenu == null || !homeMenu.showing()));
        }
    }

    private void revealControls() {
        if ("auto".equals(controlsMode) && !controlsWanted) {
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
        if (homeMenu != null && homeMenu.showing()) {
            return homeMenu.handleKey(event);
        }
        if (fromController(event.getSource()) && "auto".equals(controlsMode)) {
            hideControlsForController();
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (homeMenu != null && homeMenu.showing()) {
            // A stick or hat pad moves the menu's focus, once per push.
            if (fromController(event.getSource()) && event.getAction() == MotionEvent.ACTION_MOVE) {
                float axis = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
                if (Math.abs(axis) < 0.5f) {
                    axis = event.getAxisValue(MotionEvent.AXIS_Y);
                }
                final float direction = axis > 0.6f ? 1.0f : axis < -0.6f ? -1.0f : 0.0f;
                if (direction != 0.0f && direction != menuAxis) {
                    final int code = direction > 0 ? KeyEvent.KEYCODE_DPAD_DOWN : KeyEvent.KEYCODE_DPAD_UP;
                    homeMenu.handleKey(new KeyEvent(KeyEvent.ACTION_DOWN, code));
                }
                menuAxis = direction;
            }
            return true;
        }
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
