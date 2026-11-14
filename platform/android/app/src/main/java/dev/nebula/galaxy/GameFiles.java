// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;

import java.io.File;

/**
 * Where everything lives, in the app's private storage (--data for the native game), and the settings both
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

    /** The saves and the Mii database. */
    static File saves(Context context) {
        return savesIn(data(context));
    }

    /**
     * The saves folder under [dataDir]. Earlier builds called it "nand": it is renamed to "saves" the first time it is
     * asked for. If the rename fails, the old folder is used for this run so no save is lost.
     */
    static File savesIn(File dataDir) {
        final File saves = new File(dataDir, "saves");
        final File old = new File(dataDir, "nand");
        if (saves.exists() || !old.isDirectory()) {
            return saves;
        }
        if (old.renameTo(saves)) {
            return saves;
        }
        Log.e("GameFiles", "the saves folder could not be renamed from nand; using " + old.getPath());
        return old;
    }

    static File log(Context context) {
        return new File(data(context), "logs/game.log");
    }

    static File lastError(Context context) {
        return new File(data(context), "last_error.txt");
    }

    static boolean installed(Context context) {
        return install(context).isFile() && pak(context).isFile();
    }

    // The on-screen controls' settings (the game's menu writes them; the game's process reads them when it starts).
    static final String CONTROLS = "controls";
    static final String CONTROLS_OPACITY = "opacity";    // 0.2 .. 1
    static final String CONTROLS_HAPTICS = "haptics";
    static final String RECORD_NEXT_RUN = "recordNextRun";  // the next run records diagnostics in its log

    static SharedPreferences controls(Context context) {
        return context.getSharedPreferences(CONTROLS, Context.MODE_PRIVATE);
    }
}
