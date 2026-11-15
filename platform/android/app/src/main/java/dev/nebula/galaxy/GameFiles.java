// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

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

    /**
     * The mods that come with the app (its assets' mods folder: LumaKit), unpacked into the app's private storage once
     * for each install or update of the app. The native game installs them into its mods folder (--shipped-mods). Null
     * when the app has none or they could not be unpacked (said in the log): the game then starts without them.
     */
    static File shippedMods(Context context) {
        final File folder = new File(context.getNoBackupFilesDir(), "shipped-mods");
        final File stamp = new File(folder, ".unpacked");
        try {
            final String version = String.valueOf(
                context.getPackageManager().getPackageInfo(context.getPackageName(), 0).lastUpdateTime);
            final String[] names = context.getAssets().list("mods");
            if (names == null || names.length == 0) {
                return null;
            }
            if (stamp.isFile() && version.equals(new String(Files.readAllBytes(stamp.toPath()), StandardCharsets.UTF_8))) {
                return folder;
            }
            final File[] old = folder.listFiles();
            if (old != null) {
                for (File file : old) {
                    if (!file.delete()) {
                        throw new IOException("could not remove " + file.getPath());
                    }
                }
            }
            if (!folder.isDirectory() && !folder.mkdirs()) {
                throw new IOException("could not make " + folder.getPath());
            }
            final byte[] buffer = new byte[1 << 16];
            for (String name : names) {
                if (!name.endsWith(".luma")) {
                    continue;
                }
                final File part = new File(folder, name + ".part");
                try (InputStream in = context.getAssets().open("mods/" + name);
                     OutputStream out = new FileOutputStream(part)) {
                    for (int n; (n = in.read(buffer)) > 0; ) {
                        out.write(buffer, 0, n);
                    }
                }
                if (!part.renameTo(new File(folder, name))) {
                    throw new IOException("could not name " + name);
                }
            }
            Files.write(stamp.toPath(), version.getBytes(StandardCharsets.UTF_8));
            return folder;
        } catch (IOException | PackageManager.NameNotFoundException e) {
            Log.e("GameFiles", "the mods that come with the app were not unpacked: " + e);
            return null;
        }
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
