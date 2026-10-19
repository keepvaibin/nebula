// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.content.Context;
import android.content.SharedPreferences;

import java.io.File;

/**
 * Where everything lives, in the app's private storage (NEBULA_DATA_DIR for the native game), and the settings both
 * processes read.
 */
final class GameFiles {
    private GameFiles() {}

    static File data(Context context) {
        return context.getFilesDir();
    }

    static File pak(Context context) {
        return new File(data(context), "game/game.pak");
    }

    /** What setup installed (written last: its presence means the game is complete). */
    static File install(Context context) {
        return new File(data(context), "game/install.properties");
    }

    static File nand(Context context) {
        return new File(data(context), "nand");
    }

    static File log(Context context) {
        return new File(data(context), "logs/game.log");
    }

    static File lastError(Context context) {
        return new File(data(context), "last_error.txt");
    }

    static File renderConfig(Context context) {
        return new File(data(context), "config.ini");
    }

    static boolean installed(Context context) {
        return install(context).isFile() && pak(context).isFile();
    }

    // The on-screen controls' settings (the launcher writes them; the game's process reads them when it starts).
    static final String CONTROLS = "controls";
    static final String CONTROLS_MODE = "mode";          // "auto" (hidden while a controller is used), "always", "never"
    static final String CONTROLS_OPACITY = "opacity";    // 0.2 .. 1
    static final String CONTROLS_SCALE = "scale";        // 0.75 .. 1.4
    static final String CONTROLS_HAPTICS = "haptics";

    static SharedPreferences controls(Context context) {
        return context.getSharedPreferences(CONTROLS, Context.MODE_PRIVATE);
    }
}
