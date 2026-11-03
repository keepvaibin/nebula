// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Arrays;

/**
 * The on-screen controls' arrangement, kept apart for each shape of screen: a phone upright and on its side, a foldable
 * folded and open, a window of any size. Each has its own overall size and each control's place.
 */
final class TouchLayouts {
    static final int ITEMS = 12;  // TouchControlsView's ten buttons, the stick and the D-pad

    /** One shape of screen's arrangement. NaN: as the game lays it out. */
    static final class Layout {
        float size = Float.NaN;
        final float[] x = new float[ITEMS];  // the control's centre, as a fraction of the screen
        final float[] y = new float[ITEMS];

        Layout() {
            clear();
        }

        void clear() {
            size = Float.NaN;
            Arrays.fill(x, Float.NaN);
            Arrays.fill(y, Float.NaN);
        }

        boolean moved(int item) {
            return !Float.isNaN(x[item]);
        }
    }

    private final SharedPreferences prefs;

    TouchLayouts(Context context) {
        prefs = context.getSharedPreferences("touch_layouts", Context.MODE_PRIVATE);
    }

    /** The screen's shape in steps of 16 dp, so a window a few pixels different is the same shape. */
    static String key(int width, int height, float density) {
        final int w = Math.round(width / density / 16.0f) * 16;
        final int h = Math.round(height / density / 16.0f) * 16;
        return w + "x" + h;
    }

    Layout load(String key) {
        final Layout layout = new Layout();
        final String text = prefs.getString(key, null);
        if (text == null) {
            return layout;
        }
        final String[] parts = text.split(";");
        try {
            if (!parts[0].isEmpty()) {
                layout.size = Float.parseFloat(parts[0]);
            }
            for (int i = 1; i < parts.length; ++i) {
                final String[] item = parts[i].split(",");
                final int index = Integer.parseInt(item[0]);
                if (index >= 0 && index < ITEMS && item.length >= 3) {
                    layout.x[index] = Float.parseFloat(item[1]);
                    layout.y[index] = Float.parseFloat(item[2]);
                }
            }
        } catch (NumberFormatException e) {
            layout.clear();
        }
        return layout;
    }

    void save(String key, Layout layout) {
        final StringBuilder text = new StringBuilder();
        if (!Float.isNaN(layout.size)) {
            text.append(layout.size);
        }
        for (int i = 0; i < ITEMS; ++i) {
            if (layout.moved(i)) {
                text.append(';').append(i).append(',').append(layout.x[i]).append(',').append(layout.y[i]);
            }
        }
        prefs.edit().putString(key, text.toString()).apply();
    }

    void remove(String key) {
        prefs.edit().remove(key).apply();
    }
}
