// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.view.Display;

/**
 * The display modes the game asks for, at the current resolution. Display.getRefreshRate() is no guide: it reports the
 * rate apps are held to (often 60) rather than what the panel can show.
 */
public final class DisplayRate {
    private DisplayRate() {}

    /** The fastest mode, a multiple of 60 Hz first (each game frame then spans whole refreshes). */
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

    /**
     * The mode for a frame rate: the slowest that shows it (at or above it, a multiple of 60 Hz first among equals), or
     * the fastest for 0 (the display's own rate) and for a rate above every mode.
     */
    public static Display.Mode modeFor(Display display, float frameRate) {
        final Display.Mode fastest = fastestMode(display);
        if (frameRate <= 0.0f) {
            return fastest;
        }
        final Display.Mode current = display.getMode();
        Display.Mode best = null;
        for (final Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() != current.getPhysicalWidth() ||
                mode.getPhysicalHeight() != current.getPhysicalHeight() || mode.getRefreshRate() + 0.5f < frameRate) {
                continue;
            }
            if (best == null || mode.getRefreshRate() < best.getRefreshRate() - 0.5f ||
                (Math.abs(mode.getRefreshRate() - best.getRefreshRate()) <= 0.5f &&
                 score(mode.getRefreshRate()) > score(best.getRefreshRate()))) {
                best = mode;
            }
        }
        return best != null ? best : fastest;
    }

    private static float score(float hz) {
        final float multiple = Math.round(hz / 60.0f) * 60.0f;
        return (Math.abs(hz - multiple) < 1.0f ? 1000.0f : 0.0f) + hz;
    }
}
