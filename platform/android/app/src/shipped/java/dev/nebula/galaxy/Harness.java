// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.app.Activity;

import java.io.File;

/** A shipped build has no test runs: every start is the player's. Test builds take the test driver's Harness. */
final class Harness {
    private Harness() {}

    static File data(Activity activity) {
        return null;
    }

    static String[] arguments(Activity activity) {
        return null;
    }
}
