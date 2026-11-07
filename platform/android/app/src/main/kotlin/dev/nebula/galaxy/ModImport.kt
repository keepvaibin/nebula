// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.IOException

/** What an import came to: the mod's folder name, what it holds, and what the player reads. */
data class ModImportResult(val ok: Boolean, val name: String, val id: String, val kinds: String, val message: String)

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

    @JvmStatic
    private external fun importFile(source: String, data: String, progress: Progress?): String

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
                        if (size > 0) onProgress(0.5f * done / size)
                    }
                }
            }
            val answer = importFile(copy.path, data.path) { done, total ->
                if (total > 0) onProgress(0.5f + 0.5f * done / total)
                true
            }.split('\n', limit = 4)
            return ModImportResult(answer[0] == "1", name.substringBeforeLast('.').ifEmpty { name }, answer.getOrElse(1) { "" }, answer.getOrElse(2) { "" }, answer.getOrElse(3) { "" })
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
