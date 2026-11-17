// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.app.Activity;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;

/**
 * A test runner's run on Android, built into test builds only (the shipped app has platform/android/app/src/shipped's
 * Harness, which answers none). The runner pushes a run folder under the app's private data over adb and starts the
 * game screen with the intent extra "nebula.harness" naming it; the game then runs as the desktop runner runs it
 * (tools/test_driver/luma_test): the run's own folders, the test extension, a time limit. With the extra
 * "nebula.start_check" (frames) the start is the push workflow's start check instead (host_main.cpp run_start_check):
 * the run's settings and data folders, no game data, no extension.
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
            // The test's hooks (luma_test.py TEST_HOOKS), which the host reads from its environment at start: env.txt,
            // one NAME=value a line, GALAXY_TEST_ names only.
            final File hooks = new File(folder, "env.txt");
            if (hooks.isFile()) {
                for (String line : Files.readAllLines(hooks.toPath(), StandardCharsets.UTF_8)) {
                    final int at = line.indexOf('=');
                    if (line.isEmpty()) {
                        continue;
                    }
                    if (at < 0 || !line.substring(0, at).matches("GALAXY_TEST_[A-Z0-9_]{1,64}")) {
                        throw new IllegalStateException("harness run: env.txt takes GALAXY_TEST_ names only: " + line);
                    }
                    android.system.Os.setenv(line.substring(0, at), line.substring(at + 1), true);
                }
            }
        } catch (android.system.ErrnoException | IOException failed) {
            throw new IllegalStateException("harness run: the environment could not be set", failed);
        }
        final int startCheck = activity.getIntent().getIntExtra("nebula.start_check", 0);
        if (startCheck > 0) {
            return new String[] {
                "--start-check", String.valueOf(startCheck),
                "--data", new File(folder, "appdata").getPath(),
                "--settings", new File(folder, "settings").getPath(),
                "--crash-dumps", folder.getPath()};
        }
        final String libraries = activity.getApplicationInfo().nativeLibraryDir;
        final List<String> arguments = new ArrayList<>(List.of(
            "--data", new File(folder, "appdata").getPath(),
            "--pak", GameFiles.pak(activity).getPath(),
            "--saves", new File(folder, "saves").getPath(),
            "--settings", new File(folder, "settings").getPath(),
            "--seconds", String.valueOf(seconds),
            "--crash-dumps", folder.getPath(),
            "--module", libraries + "/libsmg_game.so",
            "--extension", libraries + "/libnebula_test_extension.so"));
        // The run's --diagnostics (luma_test_android.py writes the runner's and the test's names to diagnostics.txt);
        // the game checks the names, so one it does not know ends the start with its message.
        final String diagnostics = diagnostics(new File(folder, "diagnostics.txt"));
        if (!diagnostics.isEmpty()) {
            arguments.add("--diagnostics");
            arguments.add(diagnostics);
            if (("," + diagnostics + ",").contains(",timeline,")) {
                arguments.add("--timeline");
                arguments.add(new File(folder, "timeline.bin").getPath());
            }
        }
        return arguments.toArray(new String[0]);
    }

    /** diagnostics.txt's one line of names ("timeline,frame-times"), or "" without the file. */
    private static String diagnostics(File file) {
        if (!file.isFile()) {
            return "";
        }
        try {
            final String names = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8).trim();
            if (!names.matches("[a-z0-9,-]{0,512}")) {
                throw new IllegalStateException("harness run: diagnostics.txt takes names and commas only: " + names);
            }
            return names;
        } catch (IOException failed) {
            throw new IllegalStateException("harness run: diagnostics.txt could not be read", failed);
        }
    }
}
