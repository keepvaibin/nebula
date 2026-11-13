// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.IOException

/** What an import came to: the mod's folder name, what it holds, and what the player reads. */
data class ModImportResult(
    val ok: Boolean,
    val name: String,
    val id: String,
    val kinds: String,
    val message: String,
    /** Several imports' answer: its own title over [message]. */
    val title: String? = null,
    /** A mod with exactly these files was there already (under [name]): nothing was added. */
    val already: Boolean = false,
)

/**
 * A mod or texture pack from a file the player picked (.luma, .zip, .7z, .rar or .tar), into the mods folder the game
 * reads, by the game's own importer (libnebula_mods.so, runtime/src/mod_import.cpp): copied beside the mods folder
 * first, as the file the picker gives may not be one that can be read twice or out of order, then unpacked there.
 */
object ModImport {
    init {
        System.loadLibrary("nebula_mods")
    }

    fun interface Progress {
        /** Bytes done of the total (0 while unknown); false cancels. */
        fun update(done: Long, total: Long): Boolean
    }

    // Paths and the answer cross as UTF-8 bytes: JNI's own strings are modified UTF-8, which spells a character past
    // U+FFFF (an emoji in a file's name) differently from the file system.
    @JvmStatic
    private external fun importFile(source: ByteArray, data: ByteArray, pak: ByteArray, progress: Progress?): ByteArray

    /** [onProgress] gets the copy's share (0 to 0.5) and then the unpacking's (0.5 to 1). */
    fun import(context: Context, uri: Uri, onProgress: (Float) -> Unit): ModImportResult {
        val data = GameFiles.data(context)
        val (shown, size) = nameAndSize(context, uri)
        val name = (shown ?: "Mod").replace(Regex("[/\\\\:*?\"<>|]"), "_").trim().ifEmpty { "Mod" }
        val incoming = File(data, ".mod-incoming-${System.nanoTime()}")
        if (!incoming.mkdirs()) throw IOException("The mods folder could not be written")
        try {
            val copy = File(incoming, name)
            val input = context.contentResolver.openInputStream(uri) ?: throw IOException("The file could not be opened")
            input.use { from ->
                copy.outputStream().use { to ->
                    val buffer = ByteArray(1 shl 20)
                    var done = 0L
                    while (true) {
                        val read = from.read(buffer)
                        if (read < 0) break
                        to.write(buffer, 0, read)
                        done += read
                        if (size > 0) onProgress(0.5f * minOf(done, size) / size)
                    }
                }
            }
            val answer = String(importFile(copy.path.toByteArray(Charsets.UTF_8), data.path.toByteArray(Charsets.UTF_8),
                GameFiles.pak(context).path.toByteArray(Charsets.UTF_8)) { done, total ->
                if (total > 0) onProgress(0.5f + 0.5f * minOf(done, total) / total)
                true
            }, Charsets.UTF_8).split('\n', limit = 5)
            // The mod's own name as the game's Mods page shows it; the file's name if the import named none.
            val title = answer.getOrElse(3) { "" }.ifEmpty { name.substringBeforeLast('.').ifEmpty { name } }
            return ModImportResult(answer[0] == "1" || answer[0] == "2", title, answer.getOrElse(1) { "" }, answer.getOrElse(2) { "" },
                answer.getOrElse(4) { "" }, already = answer[0] == "2")
        } finally {
            incoming.deleteRecursively()
        }
    }

    private fun nameAndSize(context: Context, uri: Uri): Pair<String?, Long> =
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)
            ?.use { cursor ->
                if (!cursor.moveToFirst()) return@use null to 0L
                val name = if (cursor.isNull(0)) null else cursor.getString(0)
                val size = if (cursor.isNull(1)) 0L else cursor.getLong(1)
                name to size
            } ?: (null to 0L)
}
