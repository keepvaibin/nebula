// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory

/** The manifest's promises the code relies on. */
class ManifestTest {
    private val android = "http://schemas.android.com/apk/res/android"

    // Every android:configChanges value (R.attr.configChanges, API 36).
    private val everyChange = setOf(
        "mcc", "mnc", "locale", "touchscreen", "keyboard", "keyboardHidden", "navigation", "screenLayout", "fontScale",
        "uiMode", "orientation", "screenSize", "smallestScreenSize", "layoutDirection", "colorMode", "density",
        "fontWeightAdjustment", "grammaticalGender",
    )

    @Test
    fun theGameHandlesEveryConfigurationChange() {
        // A recreated GameActivity would start the native game a second time in its process (P-098).
        val factory = DocumentBuilderFactory.newInstance().apply { isNamespaceAware = true }
        val document = factory.newDocumentBuilder().parse(File("src/main/AndroidManifest.xml"))
        val activities = document.getElementsByTagName("activity")
        var changes: Set<String>? = null
        for (i in 0 until activities.length) {
            val activity = activities.item(i) as org.w3c.dom.Element
            if (activity.getAttributeNS(android, "name") == ".GameActivity") {
                changes = activity.getAttributeNS(android, "configChanges").split('|').toSet()
            }
        }
        assertEquals(everyChange, changes)
    }
}
