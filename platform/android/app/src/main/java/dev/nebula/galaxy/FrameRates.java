// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import java.io.BufferedReader;
import java.io.IOException;

/**
 * The frame-rate setting as the activity reads it before the game does, by the game's step rate (the library's
 * nativeStepRate: one number, galaxy/game_step.h). Free of Android's classes, under FrameRatesTest.
 */
final class FrameRates {
    private FrameRates() {}

    /** galaxy/game_step.h kOwnFrameRateAllowance: a setting up to the step rate and this is the game's own frames. */
    static final float OWN_RATE_ALLOWANCE = 1.0f;

    /**
     * config.ini's max_fps (0: the display's own; "unlimited": infinite). With no max_fps line, or one that does not
     * read, the step rate (the game's own default).
     */
    static float saved(BufferedReader config, float stepRate) {
        float maxFps = stepRate;
        try {
            for (String line; (line = config.readLine()) != null; ) {
                if (line.startsWith("max_fps=")) {
                    final String value = line.substring("max_fps=".length()).trim();
                    maxFps = value.equals("unlimited") ? Float.POSITIVE_INFINITY : Float.parseFloat(value);
                }
            }
        } catch (IOException | NumberFormatException e) {
            return stepRate;
        }
        return maxFps;
    }

    /** The game's own frames: each step shown as it is; the display mode is left to the system. */
    static boolean own(float maxFps, float stepRate) {
        return maxFps > 0.0f && maxFps <= stepRate + OWN_RATE_ALLOWANCE;
    }
}
