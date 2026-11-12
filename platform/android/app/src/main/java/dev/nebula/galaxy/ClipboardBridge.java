// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipDescription;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;

import androidx.core.content.FileProvider;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/**
 * LumaCore's clipboard pictures on Android (services-system 5), called from libmain.so's worker thread by name
 * (platform/sdl/luma_system_sdl.cpp); text goes through SDL. A picture is read from the primary clip's first item
 * whose address is a picture, and written as a PNG in the app's cache that the clip names by its FileProvider address.
 */
final class ClipboardBridge {
    private ClipboardBridge() {}

    private static ClipboardManager clipboard(Activity activity) {
        return (ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
    }

    /** The first item's picture address, or null. */
    private static Uri pictureOf(Activity activity) {
        ClipboardManager manager = clipboard(activity);
        ClipData clip = manager != null ? manager.getPrimaryClip() : null;
        if (clip == null) {
            return null;
        }
        ContentResolver resolver = activity.getContentResolver();
        for (int i = 0; i < clip.getItemCount(); i++) {
            Uri uri = clip.getItemAt(i).getUri();
            if (uri == null) {
                continue;
            }
            String type = resolver.getType(uri);
            if (type != null && ClipDescription.compareMimeTypes(type, "image/*")) {
                return uri;
            }
        }
        return null;
    }

    static boolean hasImage(Activity activity) {
        try {
            return pictureOf(activity) != null;
        } catch (RuntimeException e) {
            return false;
        }
    }

    /** The picture file's bytes; an empty array when it is longer than {@code limit}; null when it cannot be read. */
    static byte[] readImage(Activity activity, int limit) {
        try {
            Uri uri = pictureOf(activity);
            if (uri == null) {
                return null;
            }
            try (InputStream in = activity.getContentResolver().openInputStream(uri)) {
                if (in == null) {
                    return null;
                }
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] buffer = new byte[65536];
                long total = 0;
                for (int read; (read = in.read(buffer)) > 0; ) {
                    total += read;
                    if (total > limit) {
                        return new byte[0];
                    }
                    out.write(buffer, 0, read);
                }
                return out.toByteArray();
            }
        } catch (Exception | OutOfMemoryError e) {
            return null;
        }
    }

    /** Puts {@code png} on the clipboard (cache/clipboard/image.png, replaced each time). */
    static boolean writeImage(Activity activity, byte[] png) {
        try {
            File folder = new File(activity.getCacheDir(), "clipboard");
            if (!folder.isDirectory() && !folder.mkdirs()) {
                return false;
            }
            File file = new File(folder, "image.png");
            try (FileOutputStream out = new FileOutputStream(file)) {
                out.write(png);
            }
            Uri uri = FileProvider.getUriForFile(activity, activity.getPackageName() + ".files", file);
            ClipboardManager manager = clipboard(activity);
            if (manager == null) {
                return false;
            }
            manager.setPrimaryClip(ClipData.newUri(activity.getContentResolver(), "image", uri));
            return true;
        } catch (Exception e) {
            return false;
        }
    }
}
