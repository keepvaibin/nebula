// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.BufferedReader
import java.io.File
import java.io.StringReader

/**
 * The activity's frame-rate setting before the game reads it (G2.3.7, P-042's Android part): with no saved rate the
 * game's step rate, and a rate is the game's own frames up to the step rate and its allowance, whatever that rate is
 * (tried at 60 and at a fed 50).
 */
class FrameRatesTest {
    private fun saved(text: String, stepRate: Float) = FrameRates.saved(BufferedReader(StringReader(text)), stepRate)

    @Test
    fun noSavedRateIsTheStepRate() {
        assertEquals(60.0f, saved("", 60.0f))
        assertEquals(50.0f, saved("", 50.0f))
        assertEquals(50.0f, saved("vsync=1\nrenderer=vulkan\n", 50.0f))
        assertEquals(50.0f, saved("max_fps=sixty\n", 50.0f))
    }

    @Test
    fun aSavedRateIsRead() {
        assertEquals(120.0f, saved("vsync=1\nmax_fps=120\n", 60.0f))
        assertEquals(0.0f, saved("max_fps=0\n", 60.0f))
        assertEquals(Float.POSITIVE_INFINITY, saved("max_fps=unlimited\n", 60.0f))
        assertEquals(30.0f, saved("max_fps= 30 \n", 50.0f))
    }

    @Test
    fun theGamesOwnRatesFollowTheStepRate() {
        assertTrue(FrameRates.own(60.0f, 60.0f))
        assertTrue(FrameRates.own(30.0f, 60.0f))
        assertTrue(FrameRates.own(61.0f, 60.0f))
        assertFalse(FrameRates.own(61.5f, 60.0f))
        assertFalse(FrameRates.own(0.0f, 60.0f))
        assertFalse(FrameRates.own(Float.POSITIVE_INFINITY, 60.0f))
        assertTrue(FrameRates.own(50.0f, 50.0f))
        assertTrue(FrameRates.own(51.0f, 50.0f))
        assertFalse(FrameRates.own(52.0f, 50.0f))
        assertFalse(FrameRates.own(60.0f, 50.0f))
    }

    @Test
    fun theActivityHasNoLiteralRate() {
        // savedFrameRate and applyDisplayRate read the step rate from the library, never a 60 or 61 of their own.
        val source = File("src/main/java/dev/nebula/galaxy/GameActivity.java").readText()
        for (name in listOf("savedFrameRate", "applyDisplayRate")) {
            val body = Regex("""private \w+ $name\([^)]*\) \{(.*?)\n    \}""", RegexOption.DOT_MATCHES_ALL).find(source)
            assertTrue("GameActivity has $name", body != null)
            assertFalse("$name names no literal rate", Regex("""\b6[01](\.0)?f?\b""").containsMatchIn(body!!.groupValues[1]))
        }
    }
}
