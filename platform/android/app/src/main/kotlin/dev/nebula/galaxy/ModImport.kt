// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.IOException
import java.util.zip.ZipInputStream

/**
 * A mod or texture pack from a .zip the player picked into the mods folder the game reads: unpacked into a hidden folder
 * first and named only when complete, so a failed import leaves nothing behind. A lone top folder is the mod's own. An
 * entry that would leave the folder is refused, and so is an archive past the limit.
 */
object ModImport {
    private const val MAX_BYTES = 16L * 1024 * 1024 * 1024
    private const val MAX_ENTRIES = 200_000

    fun folder(context: Context) = File(GameFiles.data(context), "mods")

    /** The mod's name, or throws with what the player should read. */
    fun import(context: Context, uri: Uri): String {
        val shown = displayName(context, uri) ?: "Mod"
        val name = shown.substringBeforeLast('.').trim().ifEmpty { "Mod" }.replace(Regex("[/\\\\:*?\"<>|]"), "_")
        val mods = folder(context)
        if (!mods.isDirectory && !mods.mkdirs()) throw IOException("The mods folder could not be made")
        val staging = File(mods, ".importing-${System.nanoTime()}")
        try {
            unpack(context, uri, staging)
            val parts = staging.listFiles().orEmpty()
            val folders = parts.filter { it.isDirectory }
            val root = if (folders.size == 1 && parts.count { it.isFile && !isNote(it) } == 0) folders[0] else staging
            // A .luma says which mod it is: the same id is an update of it, whatever the file is called.
            val manifest = File(root, "luma.json")
            val id = if (manifest.isFile) runCatching { org.json.JSONObject(manifest.readText()).optString("id") }.getOrDefault("") else ""
            val folderName = if (id.matches(Regex("[a-z0-9-]+"))) id else name
            val target = File(mods, folderName)
            if (target.exists() && !target.deleteRecursively()) throw IOException("The old $name could not be replaced")
            if (!root.renameTo(target)) throw IOException("$name could not be put in the mods folder")
            return name
        } finally {
            staging.deleteRecursively()
        }
    }

    private fun isNote(file: File) =
        file.extension.lowercase() in setOf("txt", "md", "pdf", "url", "htm", "html", "rtf", "png", "jpg", "jpeg", "webp", "gif", "nfo")

    private fun unpack(context: Context, uri: Uri, into: File) {
        if (!into.mkdirs()) throw IOException("The mods folder could not be written")
        val base = into.canonicalPath + File.separator
        var bytes = 0L
        var entries = 0
        val input = context.contentResolver.openInputStream(uri) ?: throw IOException("The file could not be opened")
        ZipInputStream(input.buffered()).use { zip ->
            val buffer = ByteArray(1 shl 16)
            while (true) {
                val entry = zip.nextEntry ?: break
                if (++entries > MAX_ENTRIES) throw IOException("This archive has too many files")
                val file = File(into, entry.name)
                if (!file.canonicalPath.startsWith(base)) throw IOException("This archive reaches outside its folder")
                if (entry.isDirectory) {
                    file.mkdirs()
                    continue
                }
                file.parentFile?.mkdirs()
                file.outputStream().use { out ->
                    while (true) {
                        val read = zip.read(buffer)
                        if (read < 0) break
                        bytes += read
                        if (bytes > MAX_BYTES) throw IOException("This archive unpacks too large")
                        out.write(buffer, 0, read)
                    }
                }
            }
        }
        if (entries == 0) throw IOException("Only .zip files can be imported on a phone for now")
    }

    private fun displayName(context: Context, uri: Uri): String? =
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0) else null
        }
}
