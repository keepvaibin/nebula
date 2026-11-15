// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.app.Activity;

import java.io.File;

/**
 * A test runner's run on Android, built into test builds only (the shipped app has platform/android/app/src/shipped's
 * Harness, which answers none). The runner pushes a run folder under the app's private data over adb and starts the
 * game screen with the intent extra "nebula.harness" naming it; the game then runs as the desktop runner runs it
 * (tools/test_driver/luma_test): the run's own folders, the test extension, a time limit.
 */
final class Harness {
    private Harness() {}

    /** The run's name, or null when this start is no harness run. Letters, digits, '.', '_' and '-' only. */
    private static String run(Activity activity) {
        if (!BuildConfig.DEBUG) {
            return null;
        }
        final String name = activity.getIntent().getStringExtra("nebula.harness");
        return name != null && name.matches("[A-Za-z0-9._-]{1,64}") ? name : null;
    }

    private static File folder(Activity activity, String run) {
        return new File(new File(GameFiles.data(activity), "harness"), run);
    }

    /** The run's app data folder, or null when this start is no harness run. */
    static File data(Activity activity) {
        final String run = run(activity);
        return run == null ? null : new File(folder(activity, run), "appdata");
    }

    /** The run's command line, its environment set first; null when this start is no harness run. */
    static String[] arguments(Activity activity) {
        final String run = run(activity);
        if (run == null) {
            return null;
        }
        final File folder = folder(activity, run);
        final int seconds = activity.getIntent().getIntExtra("nebula.seconds", 60);
        // The desktop runner's names for a run's private folders: the test extension's data guard declines to start
        // without them.
        final String[][] names = {
            {"NEBULA_WORK", folder.getPath()},
            {"NEBULA_SETTINGS", new File(folder, "settings").getPath()},
            {"NEBULA_SAVES", new File(folder, "saves").getPath()},
            {"NEBULA_APPDATA", new File(folder, "appdata").getPath()},
            {"GALAXY_RENDER_CONFIG_PATH", new File(folder, "settings/config.ini").getPath()},
            {"GALAXY_RUNTIME_SETTINGS_PATH", new File(folder, "settings/runtime_settings.ini").getPath()},
            {"GALAXY_CRASH_DUMP_DIR", folder.getPath()},
            {"GALAXY_AUDIO_WAV_DUMP_ONLY", "1"},
            {"GALAXY_AUDIO_WAV_DUMP", new File(folder, "audio.wav").getPath()},
            {"GALAXY_AUDIO_WAV_DUMP_SECONDS", String.valueOf(seconds)},
            {"LUMA_HARNESS_PLAN", new File(folder, "plan.txt").getPath()},
        };
        try {
            for (String[] pair : names) {
                android.system.Os.setenv(pair[0], pair[1], true);
            }
        } catch (android.system.ErrnoException failed) {
            throw new IllegalStateException("harness run: the environment could not be set", failed);
        }
        final String libraries = activity.getApplicationInfo().nativeLibraryDir;
        return new String[] {
            "--data", new File(folder, "appdata").getPath(),
            "--pak", GameFiles.pak(activity).getPath(),
            "--saves", new File(folder, "saves").getPath(),
            "--settings", new File(folder, "settings").getPath(),
            "--seconds", String.valueOf(seconds),
            "--crash-dumps", folder.getPath(),
            "--module", libraries + "/libsmg_game.so",
            "--extension", libraries + "/libnebula_test_extension.so",
            "--diagnostics", "0",
        };
    }
}
