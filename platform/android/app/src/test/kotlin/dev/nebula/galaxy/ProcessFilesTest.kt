// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

/**
 * The launcher and the game run in two processes, and each keeps its own copy of a settings file it has read and
 * writes that copy whole: a file with two writer processes loses one's changes (P-093). Each settings file has one
 * writer; what both write is a flag file, read afresh.
 */
class ProcessFilesTest {
    private val sources = File("src/main")

    // The game's process runs GameActivity (android:process=":game") and the Java classes it uses; the Kotlin is the
    // launcher's, and GameFiles and Importer serve both.
    private fun inGameProcess(file: File) = file.name.endsWith(".java") && file.name !in setOf("GameFiles.java", "Importer.java")

    private fun edits(file: File, settings: String): Boolean =
        Regex("""GameFiles\.$settings\([^)]*\)\s*\.edit\(\)|val \w+ = GameFiles\.$settings\([^)]*\)\.edit\(\)""")
            .containsMatchIn(file.readText())

    @Test
    fun eachSettingsFileHasOneWriterProcess() {
        val files = sources.walkTopDown().filter { it.isFile && (it.name.endsWith(".kt") || it.name.endsWith(".java")) }.toList()
        assertTrue("the app's sources are found from ${File(".").absolutePath}", files.any { it.name == "GameActivity.java" })
        for (file in files) {
            if (inGameProcess(file)) {
                assertFalse("${file.name} (the game's process) edits the launcher's settings", edits(file, "launcher"))
            } else {
                assertFalse("${file.name} (the launcher's process) edits the game's controls settings", edits(file, "controls"))
            }
        }
    }

    @Test
    fun aFlagFileIsSetAndCleared() {
        val folder = Files.createTempDirectory("flags").toFile()
        val flag = File(folder, "record-next-run")
        assertTrue(GameFiles.setFlag(flag, false))
        assertFalse(flag.exists())
        assertTrue(GameFiles.setFlag(flag, true))
        assertTrue(GameFiles.setFlag(flag, true))
        assertTrue(flag.isFile)
        assertTrue(GameFiles.setFlag(flag, false))
        assertEquals(false, flag.exists())
        folder.deleteRecursively()
    }
}
