// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.view.InputDevice;
import android.view.MotionEvent;

/** Who owns the screen's input: the on-screen controls or the game (GameActivity, TouchControlsView). */
final class ControlsRules {
    // As RuntimeInputMode.
    static final int INPUT_AUTO = 0;
    static final int INPUT_PHONE = 4;

    private ControlsRules() {
    }

    /** The on-screen controls by the input mode: Phone always, Auto while no controller is in hand, the others never. */
    static boolean controlsWanted(int inputMode, boolean controllerInHand) {
        return inputMode == INPUT_PHONE || (inputMode == INPUT_AUTO && !controllerInHand);
    }

    /** A controller's press or a Wii Remote taken up hides the controls in Auto only (P-097). */
    static boolean controllerHidesControls(int inputMode) {
        return inputMode == INPUT_AUTO;
    }

    /**
     * A mouse's events are the game's: the controls let them through to SDL's surface, which makes them mouse buttons and
     * motion (P-102). Fingers and pens are the controls'.
     */
    static boolean mouseEvent(int toolType, int source) {
        return toolType == MotionEvent.TOOL_TYPE_MOUSE || (source & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE;
    }
}
