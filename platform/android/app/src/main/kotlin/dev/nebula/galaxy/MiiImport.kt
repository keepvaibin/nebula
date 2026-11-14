// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File

/**
 * Miis from files the player picked (a Wii, DS, 3DS, Wii U or Switch, Mii Studio or Mii Creator), read by the game's
 * own Mii reader (libnebula_mods.so, runtime/src/mii_formats.cpp) and kept in the Mii folder the game imports at
 * every start (runtime/src/mii_import.cpp), where its own checks run; the launcher's process has no game.
 */
object MiiImport {
    init {
        System.loadLibrary("nebula_mods")
    }

    // Paths, names and the answer cross as UTF-8 bytes (JNI's own strings are modified UTF-8).
    @JvmStatic
    private external fun addFile(folder: ByteArray, name: ByteArray, content: ByteArray): ByteArray

    @JvmStatic
    private external fun libraryCount(file: ByteArray): Int

    // The largest Mii file there is (a console's Mii database) is well under this.
    private const val MAX_FILE_BYTES = 4L shl 20

    /** The folder the game imports at every start (host_main.cpp: the settings' folder, "miis"). */
    private fun folder(context: Context) = File(GameFiles.data(context), "miis")

    /** The Miis the game's icon picker shows (host_main.cpp: the save folder's nebula-miis.txt); null before any. */
    fun count(context: Context): Int? =
        libraryCount(File(GameFiles.saves(context), "nebula-miis.txt").path.toByteArray(Charsets.UTF_8)).takeIf { it >= 0 }

    fun import(context: Context, uris: List<Uri>): TransferResult {
        val folder = folder(context)
        var miis = 0
        var converted = 0
        val unread = mutableListOf<String>()
        val unkept = mutableListOf<String>()
        for (uri in uris) {
            val name = displayName(context, uri)?.replace(Regex("[/\\\\:*?\"<>|]"), "_")?.trim()?.ifEmpty { null } ?: "Mii"
            val bytes = context.contentResolver.openInputStream(uri)?.use { SaveTransfer.readAtMost(it, MAX_FILE_BYTES) }
            if (bytes == null) {
                unread += name
                continue
            }
            val answer = String(
                addFile(folder.path.toByteArray(Charsets.UTF_8), name.toByteArray(Charsets.UTF_8), bytes),
                Charsets.UTF_8,
            ).split('\n', limit = 4)
            val found = answer[0].toIntOrNull() ?: 0
            if (found == 0) {
                unread += name
                continue
            }
            if (answer.getOrNull(2) != "1") {
                unkept += name
                continue
            }
            miis += found
            converted += answer.getOrNull(1)?.toIntOrNull() ?: 0
        }
        if (miis == 0) {
            if (unkept.isNotEmpty()) {
                return TransferResult(false, context.getString(R.string.nebula_miis_unkept), context.getString(R.string.nebula_mii_folder_unwritable))
            }
            return TransferResult(
                false, context.getString(if (uris.size == 1) R.string.nebula_no_miis_file else R.string.nebula_no_miis_files),
                context.getString(R.string.nebula_choose_mii_files),
            )
        }
        val parts = mutableListOf(if (miis == 1) context.getString(R.string.nebula_one_mii_found) else context.getString(R.string.nebula_miis_found, miis.toString()))
        if (converted == 1) parts += context.getString(R.string.nebula_one_mii_converted)
        if (converted > 1) parts += context.getString(R.string.nebula_miis_converted, converted.toString())
        if (unread.isNotEmpty()) parts += context.getString(R.string.nebula_not_mii_data, unread.joinToString(", "))
        if (unkept.isNotEmpty()) parts += context.getString(R.string.nebula_not_kept, unkept.joinToString(", "))
        parts += if (SaveTransfer.gameRunning(context)) {
            context.getString(R.string.nebula_miis_join_next)
        } else {
            context.getString(R.string.nebula_miis_join)
        }
        return TransferResult(true, context.getString(R.string.nebula_miis_imported), parts.joinToString(" "))
    }

    private fun displayName(context: Context, uri: Uri): String? =
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst() && !cursor.isNull(0)) cursor.getString(0) else null
        }
}
