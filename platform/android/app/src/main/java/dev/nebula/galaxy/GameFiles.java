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
import java.util.Map;

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

    // Each settings file has one writer process (P-093): a process keeps its own copy of a file it has read and writes
    // that copy whole, so a second writer's change would be undone. The launcher and the game run in two processes.

    // The on-screen controls' settings: the game's process only (its menu writes them, it reads them when it starts).
    static final String CONTROLS = "controls";
    static final String CONTROLS_OPACITY = "opacity";    // 0.2 .. 1
    static final String CONTROLS_HAPTICS = "haptics";

    static SharedPreferences controls(Context context) {
        return context.getSharedPreferences(CONTROLS, Context.MODE_PRIVATE);
    }

    // The launcher's process only: the selected graphics driver (GpuDrivers), the time it started the game. Earlier
    // builds kept them in the controls file; they are carried over once (that file is left as it is).
    static final String LAUNCHER = "launcher";
    private static final String LAUNCHER_CARRIED = "carriedOver";
    private static final String[] LAUNCHER_KEYS = {"gpuDriver", "launchedAt"};

    static SharedPreferences launcher(Context context) {
        final SharedPreferences prefs = context.getSharedPreferences(LAUNCHER, Context.MODE_PRIVATE);
        if (!prefs.getBoolean(LAUNCHER_CARRIED, false)) {
            final Map<String, ?> old = controls(context).getAll();
            final SharedPreferences.Editor edit = prefs.edit();
            for (String key : LAUNCHER_KEYS) {
                final Object value = old.get(key);
                if (value instanceof String) {
                    edit.putString(key, (String) value);
                } else if (value instanceof Long) {
                    edit.putLong(key, (Long) value);
                }
            }
            edit.putBoolean(LAUNCHER_CARRIED, true).commit();
        }
        return prefs;
    }

    // The next normal run records diagnostics in its log: set by the launcher or by the game's restart for it, cleared
    // by the run that records. A file, read afresh each time, as both processes write it.
    static File recordNextRunFile(Context context) {
        return new File(data(context), "record-next-run");
    }

    static boolean recordNextRun(Context context) {
        return recordNextRunFile(context).isFile();
    }

    static boolean setRecordNextRun(Context context, boolean on) {
        return setFlag(recordNextRunFile(context), on);
    }

    /** A flag file made or removed; true when it is as asked. */
    static boolean setFlag(File flag, boolean on) {
        try {
            return on ? flag.createNewFile() || flag.isFile() : flag.delete() || !flag.exists();
        } catch (IOException e) {
            return false;
        }
    }
}
