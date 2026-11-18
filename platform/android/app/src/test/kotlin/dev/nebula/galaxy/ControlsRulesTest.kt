// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.view.InputDevice
import android.view.MotionEvent
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Who owns the screen's input (ControlsRules): the input modes' on-screen controls and a mouse's events. */
class ControlsRulesTest {
    private val keyboardAndMouse = 1
    private val wiiRemote = 2

    @Test
    fun phoneKeepsTheControlsWithAController() {
        assertTrue(ControlsRules.controlsWanted(ControlsRules.INPUT_PHONE, true))
        assertFalse(ControlsRules.controllerHidesControls(ControlsRules.INPUT_PHONE))
    }

    @Test
    fun autoShowsThemUntilAControllerIsInHand() {
        assertTrue(ControlsRules.controlsWanted(ControlsRules.INPUT_AUTO, false))
        assertFalse(ControlsRules.controlsWanted(ControlsRules.INPUT_AUTO, true))
        assertTrue(ControlsRules.controllerHidesControls(ControlsRules.INPUT_AUTO))
    }

    @Test
    fun otherModesNeverShowThem() {
        for (mode in intArrayOf(keyboardAndMouse, wiiRemote)) {
            assertFalse(ControlsRules.controlsWanted(mode, false))
            assertFalse(ControlsRules.controllerHidesControls(mode))
        }
    }

    @Test
    fun aMouseIsTheGames() {
        assertTrue(ControlsRules.mouseEvent(MotionEvent.TOOL_TYPE_MOUSE, InputDevice.SOURCE_MOUSE))
        assertTrue(ControlsRules.mouseEvent(MotionEvent.TOOL_TYPE_UNKNOWN, InputDevice.SOURCE_MOUSE))
        assertTrue(ControlsRules.mouseEvent(MotionEvent.TOOL_TYPE_MOUSE, InputDevice.SOURCE_TOUCHSCREEN))
        assertFalse(ControlsRules.mouseEvent(MotionEvent.TOOL_TYPE_FINGER, InputDevice.SOURCE_TOUCHSCREEN))
        assertFalse(ControlsRules.mouseEvent(MotionEvent.TOOL_TYPE_STYLUS, InputDevice.SOURCE_STYLUS))
    }
}
