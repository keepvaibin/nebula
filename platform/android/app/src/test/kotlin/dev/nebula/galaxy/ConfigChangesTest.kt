// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * The game runs once per process (GameActivity, android:process=":game"): a configuration change the activity does not
 * handle makes Android destroy and create it again in the same process, where the native game cannot start a second
 * time (P-098). GameActivity declares every change android:configChanges can name, so none recreates it.
 */
class ConfigChangesTest {
    // Every value of android:configChanges (ActivityInfo.CONFIG_*).
    private val every = setOf(
        "mcc", "mnc", "locale", "touchscreen", "keyboard", "keyboardHidden", "navigation", "screenLayout", "fontScale",
        "uiMode", "orientation", "screenSize", "smallestScreenSize", "layoutDirection", "density", "colorMode",
        "grammaticalGender", "fontWeightAdjustment",
    )

    @Test
    fun theGameHandlesEveryConfigurationChange() {
        val manifest = File("src/main/AndroidManifest.xml").readText()
        val activity = Regex("""<activity\b[^>]*android:name="\.GameActivity"[^>]*>""").find(manifest)
        assertTrue("GameActivity is in the manifest", activity != null)
        val declared = Regex("""android:configChanges="([^"]*)"""").find(activity!!.value)?.groupValues?.get(1)
            ?.split('|')?.toSet() ?: emptySet()
        val missing = every - declared
        assertTrue("GameActivity does not handle $missing", missing.isEmpty())
    }
}
