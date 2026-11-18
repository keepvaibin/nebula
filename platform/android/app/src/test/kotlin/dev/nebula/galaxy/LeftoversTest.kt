// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

/**
 * What the launcher's imports leave when the app is ended during one (P-096), on the JVM: copies and staged folders
 * are removed at the launcher's start, and saves a cut-off save import moved aside are put back, never removed.
 */
class LeftoversTest {
    private fun data(): File = Files.createTempDirectory("leftovers").toFile()

    private fun file(parent: File, path: String, text: String): File =
        File(parent, path).also { it.parentFile!!.mkdirs(); it.writeText(text) }

    @Test
    fun importsLeftoversRemovedTheRestKept() {
        val data = data()
        val saves = File(data, "saves")
        file(saves, "RMGE/Nebula.sav", "current")
        val mod = file(data, ".mod-incoming-1/pack.zip", "a mod's copy")
        val save = file(data, ".save-incoming-2/saves/RMGE/Nebula.sav", "staged")
        val driver = file(data, "gpu_drivers/.incoming-3/libvulkan.so", "unpacked")
        val kept = file(data, "gpu_drivers/turnip/libvulkan.so", "a driver")
        val writing = file(data, "save-backups/.Saves before import 2026-11-18 101010.zip.writing", "half")
        val backup = file(data, "save-backups/Saves before import 2026-11-17 101010.zip", "whole")
        val aside = file(data, ".saves-replaced-4/RMGE/Nebula.sav", "old")

        val freed = Leftovers.clear(data, saves)

        for (gone in listOf(mod, save, driver, writing, aside)) assertFalse(gone.path, gone.exists())
        for (folder in listOf(".mod-incoming-1", ".save-incoming-2", "gpu_drivers/.incoming-3", ".saves-replaced-4")) {
            assertFalse(folder, File(data, folder).exists())
        }
        assertEquals("a driver", kept.readText())
        assertEquals("whole", backup.readText())
        assertEquals("current", File(saves, "RMGE/Nebula.sav").readText())
        assertEquals(12L + 6 + 8 + 4 + 3, freed)  // each removed file's bytes
    }

    @Test
    fun saveImportCutOffBetweenItsRenamesPutsTheSavesBack() {
        val data = data()
        val saves = File(data, "saves")
        file(data, ".saves-replaced-5/RMGE/Nebula.sav", "the player's")
        file(data, ".save-incoming-6/saves/RMGE/Nebula.sav", "the import's")

        assertTrue(Leftovers.restoreSaves(saves))
        Leftovers.clear(data, saves)

        assertEquals("the player's", File(saves, "RMGE/Nebula.sav").readText())
        assertFalse(File(data, ".saves-replaced-5").exists())
        assertFalse(File(data, ".save-incoming-6").exists())
    }

    @Test
    fun savesMovedAsideAreNeverRemovedWhileTheSaveFolderIsMissing() {
        val data = data()
        val saves = File(data, "saves")
        val aside = file(data, ".saves-replaced-7/RMGE/Nebula.sav", "the player's")

        Leftovers.clear(data, saves)

        assertEquals("the player's", aside.readText())
        assertFalse(Leftovers.restoreSaves(File(data, "saves").also { it.mkdirs() }))
    }

    @Test
    fun nothingToClear() {
        val data = data()
        assertFalse(Leftovers.restoreSaves(File(data, "saves")))
        assertEquals(0L, Leftovers.clear(data, File(data, "saves")))
    }
}
