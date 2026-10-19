// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.view.Display;

/**
 * The display mode the game asks for when its frame rate is the display's own: the fastest at the current resolution,
 * a multiple of 60 Hz first (each game frame then spans whole refreshes). Display.getRefreshRate() is no guide: it
 * reports the rate apps are held to (often 60) rather than what the panel can show.
 */
public final class DisplayRate {
    private DisplayRate() {}

    public static Display.Mode fastestMode(Display display) {
        final Display.Mode current = display.getMode();
        Display.Mode best = current;
        for (final Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() == current.getPhysicalWidth() &&
                mode.getPhysicalHeight() == current.getPhysicalHeight() &&
                score(mode.getRefreshRate()) > score(best.getRefreshRate())) {
                best = mode;
            }
        }
        return best;
    }

    private static float score(float hz) {
        final float multiple = Math.round(hz / 60.0f) * 60.0f;
        return (Math.abs(hz - multiple) < 1.0f ? 1000.0f : 0.0f) + hz;
    }
}
