// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import dev.nebula.galaxy.ui.Sky
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs
import kotlin.math.min

/** The launcher's sky (ui/Starfield.kt): nothing in it jumps, however long it runs (P-101). */
class SkyTest {
    // On a circle of circumference 1: the shorter way round.
    private fun apart(a: Float, b: Float): Float = min(abs(a - b), 1f - abs(a - b))

    @Test
    fun starsDriftWithoutJumps() {
        val step = 1.0 / Sky.RATE
        // Over the old two-minute wrap, and after a day open.
        for (around in doubleArrayOf(0.0, 120.0, 240.0, 86_400.0)) {
            var seconds = around - 1.0
            while (seconds < around + 1.0) {
                for (speed in floatArrayOf(0.4f, 1.0f, 1.8f)) {
                    for (start in floatArrayOf(0f, 0.37f, 0.999f)) {
                        val moved = apart(Sky.starX(start, speed, seconds), Sky.starX(start, speed, seconds + step))
                        assertTrue("star at $seconds s moved $moved", moved < 0.001f)
                        val change = abs(Sky.twinkle(start * 6f, speed, seconds) - Sky.twinkle(start * 6f, speed, seconds + step))
                        assertTrue("twinkle at $seconds s changed $change", change < 0.05f)
                    }
                }
                val turn = abs(Sky.glowTurn(seconds) - Sky.glowTurn(seconds + step))
                assertTrue("glow at $seconds s turned $turn", turn < 0.01f || turn > 6.27f)
                seconds += step
            }
        }
    }

    @Test
    fun starsStayOnTheSky() {
        for (seconds in doubleArrayOf(0.0, 59.9, 120.0, 1e6)) {
            val x = Sky.starX(0.5f, 1.8f, seconds)
            assertTrue(x >= 0f && x < 1f)
        }
    }
}
