// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.File
import java.io.InputStream
import java.nio.file.Files

/** SaveTransfer's bounded read: a whole stream up to the limit, none past it, however the stream hands its bytes over. */
class SaveTransferTest {
    // A stream that hands over at most `step` bytes a read, as a slow document provider does.
    private class Trickle(private val bytes: ByteArray, private val step: Int) : InputStream() {
        private var at = 0
        override fun read(): Int = if (at < bytes.size) bytes[at++].toInt() and 0xFF else -1
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (at >= bytes.size) return -1
            val n = minOf(len, step, bytes.size - at)
            System.arraycopy(bytes, at, b, off, n)
            at += n
            return n
        }
    }

    @Test
    fun aWholeStreamUpToTheLimit() {
        val bytes = ByteArray(200_000) { it.toByte() }
        assertArrayEquals(bytes, SaveTransfer.readAtMost(ByteArrayInputStream(bytes), 200_000))
        assertArrayEquals(bytes, SaveTransfer.readAtMost(Trickle(bytes, 7), 200_000))
        assertEquals(0, SaveTransfer.readAtMost(ByteArrayInputStream(ByteArray(0)), 0)?.size)
    }

    @Test
    fun nonePastIt() {
        val bytes = ByteArray(200_001)
        assertNull(SaveTransfer.readAtMost(ByteArrayInputStream(bytes), 200_000))
        assertNull(SaveTransfer.readAtMost(Trickle(bytes, 1000), 200_000))
        assertNull(SaveTransfer.readAtMost(ByteArrayInputStream(ByteArray(1)), 0))
    }

    private fun names(entries: List<Pair<String, ByteArray>>) = entries.map { it.first }.sorted()
    private fun entry(path: String, text: String = "x") = path to text.toByteArray()

    // P-119: a computer's export (a Nebula saves archive) is read, and the achievements and stats come with the saves.
    @Test
    fun aComputersArchiveIsRead() {
        val archive = listOf(
            entry("nebula-saves.json", "{}"), entry("saves/RMGE/Nebula.sav"), entry("saves/nebula-miis.txt"),
            entry("saves/nebula-achievements.ini"), entry("saves/nebula-stats.ini"), entry("saves/unrelated.txt"),
        )
        assertEquals(
            listOf("RMGE/Nebula.sav", "nebula-achievements.ini", "nebula-miis.txt", "nebula-stats.ini"),
            names(SaveTransfer.savePaths(archive)),
        )
    }

    @Test
    fun aZippedSaveFolderAsBefore() {
        val zipped = listOf(entry("saves/RMGE/Nebula.sav"), entry("saves/title/00010000/524d4745/data/GameData.bin"),
            entry("saves/nebula-stats.ini"))
        assertEquals(
            listOf("RMGE/Nebula.sav", "nebula-stats.ini", "title/00010000/524d4745/data/GameData.bin"),
            names(SaveTransfer.savePaths(zipped)),
        )
        assertEquals(listOf("RMGP/Nebula.sav"), names(SaveTransfer.savePaths(listOf(entry("RMGP/Nebula.sav"), entry("other.txt")))))
    }

    @Test
    fun progressIsHandedOverToTheGame() {
        val data = Files.createTempDirectory("savetransfer").toFile()
        try {
            File(data, "achievements.ini").writeText("comets=2026-01-01T00:00:00Z\n")
            File(data, "stats.ini").writeText("")
            assertEquals(listOf("nebula-achievements.ini"), SaveTransfer.progressFiles(data).map { it.first })

            SaveTransfer.handOver(data, listOf(entry("nebula-stats.ini", "coins=5")))
            SaveTransfer.handOver(data, listOf(entry("nebula-stats.ini", "coins=7\n"), entry("nebula-achievements.ini", "a=b\n")))
            assertEquals("coins=5\ncoins=7\n", File(data, "stats.ini.import").readText())
            assertEquals("a=b\n", File(data, "achievements.ini.import").readText())
            assertTrue(data.listFiles().orEmpty().none { it.name.endsWith(".writing") })
        } finally {
            data.deleteRecursively()
        }
    }
}
