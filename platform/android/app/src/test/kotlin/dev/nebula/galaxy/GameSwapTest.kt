// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

/**
 * Setup's files (GameSwap) on the JVM: setting up again keeps the game in place until the new one is whole (P-094),
 * and a cut-off setup's leftovers are found and given back (P-095).
 */
class GameSwapTest {
    private fun folder(): File = Files.createTempDirectory("gameswap").toFile()

    private fun installed(folder: File, text: String) {
        File(folder, GameSwap.PAK).writeText(text)
        File(folder, GameSwap.INSTALL).writeText("gameId=$text")
    }

    @Test
    fun roomForBothBuildsBeside() {
        assertEquals(GameSwap.Room.BESIDE, GameSwap.room(free = 10, inPlace = 4, needed = 5))
        assertEquals(GameSwap.Room.REPLACE_FIRST, GameSwap.room(free = 3, inPlace = 4, needed = 5))
        assertEquals(GameSwap.Room.NONE, GameSwap.room(free = 0, inPlace = 4, needed = 5))
    }

    @Test
    fun aFailedSetupKeepsTheGameInPlace() {
        val folder = folder()
        installed(folder, "old")
        File(folder, GameSwap.NEW).writeText("half a new game")
        GameSwap.abandon(folder)
        assertEquals("old", File(folder, GameSwap.PAK).readText())
        assertTrue(File(folder, GameSwap.INSTALL).isFile)
        assertFalse(File(folder, GameSwap.NEW).exists())
        folder.deleteRecursively()
    }

    @Test
    fun aWholeNewGameTakesThePlace() {
        val folder = folder()
        installed(folder, "old")
        File(folder, GameSwap.NEW).writeText("new")
        var installSeesPak = ""
        assertTrue(GameSwap.commit(folder) {
            installSeesPak = File(folder, GameSwap.PAK).readText()
            File(folder, GameSwap.INSTALL).writeText("gameId=new")
        })
        assertEquals("new", installSeesPak)
        assertEquals("new", File(folder, GameSwap.PAK).readText())
        assertEquals("gameId=new", File(folder, GameSwap.INSTALL).readText())
        assertFalse(File(folder, GameSwap.NEW).exists())
        folder.deleteRecursively()
    }

    @Test
    fun aCutOffSetupIsSweptAndItsRoomCounts() {
        val folder = folder()
        installed(folder, "old")
        File(folder, GameSwap.NEW).writeText("half")
        assertEquals(3L + "gameId=old".length + 4L, GameSwap.reclaimable(folder))
        assertEquals(4L, GameSwap.sweep(folder))
        assertEquals("old", File(folder, GameSwap.PAK).readText())

        // A first setup cut off: a game.pak with no install file.
        File(folder, GameSwap.INSTALL).delete()
        assertEquals(3L, GameSwap.sweep(folder))
        assertFalse(File(folder, GameSwap.PAK).exists())
        folder.deleteRecursively()
    }

    @Test
    fun aModImportsLeftoverCopiesAreFound() {
        val data = folder()
        File(data, "${GameSwap.MOD_INCOMING}123").mkdirs()
        File(data, "${GameSwap.MOD_INCOMING}123/pack.zip").writeText("half")
        File(data, "saves").mkdirs()
        File(data, "${GameSwap.MOD_INCOMING}file").writeText("not a folder")
        assertEquals(listOf("${GameSwap.MOD_INCOMING}123"), GameSwap.modIncoming(data).map { it.name })
        data.deleteRecursively()
    }

    @Test
    fun aFullDeviceIsKnown() {
        assertTrue(GameSwap.isNoSpace("game.pak cannot be written: No space left on device (os error 28)"))
        assertTrue(GameSwap.isNoSpace("write failed: ENOSPC (No space left on device)"))
        assertFalse(GameSwap.isNoSpace("the disc image is not a Wii disc"))
    }
}
