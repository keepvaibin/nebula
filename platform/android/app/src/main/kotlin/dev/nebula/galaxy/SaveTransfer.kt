// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.app.ActivityManager
import android.content.Context
import android.net.Uri
import java.io.File
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/** How a save or Mii move ended: its title and what the player reads. */
data class TransferResult(val ok: Boolean, val title: String, val message: String)

/**
 * The game's saves (GameFiles.saves: each release's save folder, the Mii database, the Wii save layout) to and from
 * one .zip the player keeps; and single save files from elsewhere: Nebula.sav (Nebula on another device) or
 * GameData.bin (a Wii, Dolphin or the Switch release), checked as the game checks them (SaveFileFormat.hpp).
 * The saves in place are zipped into save-backups/ before anything replaces them.
 */
object SaveTransfer {
    private const val MAX_FILE_BYTES = 16L shl 20
    private const val MAX_TOTAL_BYTES = 64L shl 20
    private const val BACKUPS_KEPT = 5
    private val SAVE_NAMES = Regex("^(Nebula\\.sav|GameData\\.bin)(\\.[0-9]+|\\.v2)?$")

    fun gameRunning(context: Context): Boolean =
        context.getSystemService(ActivityManager::class.java).runningAppProcesses.orEmpty()
            .any { it.processName.endsWith(":game") }

    /** Every save file as one .zip at [uri]. */
    fun export(context: Context, uri: Uri): TransferResult {
        val files = saveFiles(GameFiles.saves(context))
        if (files.none { SAVE_NAMES.matches(it.second.name) }) {
            return TransferResult(false, "No saves yet", "Play and save in the game first.")
        }
        val output = context.contentResolver.openOutputStream(uri, "wt") ?: throw IOException("The file could not be written")
        output.use { zip(files, it) }
        val count = if (files.size == 1) "1 file" else "${files.size} files"
        return TransferResult(true, "Saves exported", "$count, ${formatBytes(files.sumOf { it.second.length() })}.")
    }

    /**
     * The file at [uri] ([size] bytes, -1 not known) holds saves Import saves takes: a .zip with a Nebula.sav or
     * GameData.bin in the save folder's layout, or one of them alone.
     */
    fun holdsSaves(context: Context, uri: Uri, size: Long): Boolean {
        if (size > MAX_TOTAL_BYTES) return false
        return runCatching {
            val bytes = context.contentResolver.openInputStream(uri)?.use { readAtMost(it, MAX_TOTAL_BYTES) } ?: return false
            if (isZip(bytes)) {
                unzipSaves(bytes).any { SAVE_NAMES.matches(it.first.substringAfterLast('/')) }
            } else {
                singleFileName(bytes) != null
            }
        }.getOrDefault(false)
    }

    private fun isZip(bytes: ByteArray) = bytes.size >= 4 && bytes[0] == 'P'.code.toByte() && bytes[1] == 'K'.code.toByte() &&
        bytes[2] == 3.toByte() && bytes[3] == 4.toByte()

    /** Replaces the saves with those in the file at [uri] (a .zip, Nebula.sav or GameData.bin). */
    fun import(context: Context, uri: Uri, gameId: String): TransferResult {
        if (gameRunning(context)) {
            return TransferResult(false, "The game is running", "Close the game, then import again.")
        }
        val data = GameFiles.data(context)
        val incoming = File(data, ".save-incoming-${System.nanoTime()}")
        try {
            val input = context.contentResolver.openInputStream(uri) ?: throw IOException("The file could not be opened")
            val bytes = input.use { readAtMost(it, MAX_TOTAL_BYTES) }
                ?: return TransferResult(false, "Not a save", "The file is far larger than any save.")
            val release = gameId.take(4).ifEmpty { "RMGE" }
            val files: List<Pair<String, ByteArray>> = if (isZip(bytes)) {
                unzipSaves(bytes)
            } else {
                when (singleFileName(bytes)) {
                    "Nebula.sav" -> listOf("$release/Nebula.sav" to bytes)
                    "GameData.bin" -> listOf("title/00010000/${hexTitle(release)}/data/GameData.bin" to bytes)
                    else -> return TransferResult(false, "Not a save", singleFileProblem(bytes))
                }
            }
            val saves = files.filter { SAVE_NAMES.matches(it.first.substringAfterLast('/')) }
            if (saves.isEmpty()) {
                return TransferResult(
                    false, "No saves in that file",
                    "It holds no Nebula.sav or GameData.bin. Use a .zip made by Export, or a single save file.",
                )
            }
            // Each save is checked as the game checks it before anything in place changes.
            for ((path, content) in saves) {
                val problem = if (path.substringAfterLast('/').startsWith("Nebula.sav")) {
                    if (singleFileName(content) == "Nebula.sav") null else "${path.substringAfterLast('/')} is not a Nebula save."
                } else {
                    validGameData(content)
                }
                if (problem != null) return TransferResult(false, "That save is damaged", problem)
            }
            val miis = files.firstOrNull { it.first == MII_LIBRARY }
            if (miis != null && !String(miis.second, Charsets.UTF_8).startsWith(MII_LIBRARY_FORMAT)) {
                return TransferResult(false, "That save is damaged", "$MII_LIBRARY is not a Mii library.")
            }
            val staged = File(incoming, "saves")
            stage(GameFiles.saves(context), staged, files)
            val backedUp = backUp(context)
            replaceSaves(GameFiles.saves(context), staged)
            return TransferResult(
                true, "Saves imported",
                "${saves.joinToString(", ") { it.first.substringAfterLast('/') }}${if (miis != null) " and your Miis" else ""} in place." +
                    if (backedUp) " The saves you had are kept in Nebula's backups." else "",
            )
        } finally {
            incoming.deleteRecursively()
        }
    }

    // Save files under the save folder (not tmp/), by their path inside it.
    private fun saveFiles(root: File): List<Pair<String, File>> =
        root.walkTopDown().filter { it.isFile }
            .map { it.relativeTo(root).invariantSeparatorsPath to it }
            .filter { !it.first.startsWith("tmp/") }
            .toList()

    private fun zip(files: List<Pair<String, File>>, to: java.io.OutputStream) {
        ZipOutputStream(to).use { out ->
            for ((path, file) in files) {
                out.putNextEntry(ZipEntry(path).apply { time = file.lastModified() })
                file.inputStream().use { it.copyTo(out) }
                out.closeEntry()
            }
        }
    }

    /** The whole stream, or null past [limit] bytes. */
    internal fun readAtMost(input: java.io.InputStream, limit: Long): ByteArray? {
        val out = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(1 shl 16)
        while (true) {
            val read = input.read(buffer)
            if (read < 0) return out.toByteArray()
            if (out.size() + read > limit) return null
            out.write(buffer, 0, read)
        }
    }

    // The saves in place, copied to [staged], with [files] over them. Each imported save file replaces its group in its
    // folder (Nebula.sav and its .1/.2, or GameData.bin and its copies); a GameData.bin in a release's Wii folder also
    // retires that release's Nebula.sav unless one comes with it, as the game reads Nebula.sav first. The rest stays.
    private fun stage(saveFolder: File, staged: File, files: List<Pair<String, ByteArray>>) {
        if (saveFolder.isDirectory) saveFolder.copyRecursively(staged, overwrite = true) else staged.mkdirs()
        val wiiData = Regex("^title/00010000/([0-9a-f]{8})/data/GameData\\.bin")
        val nativeReleases = files.map { it.first }.filter { it.substringAfterLast('/').startsWith("Nebula.sav") }
            .map { it.substringBefore('/') }.toSet()
        for ((path, _) in files) {
            val name = path.substringAfterLast('/')
            if (!SAVE_NAMES.matches(name)) continue
            val group = if (name.startsWith("Nebula.sav")) "Nebula.sav" else "GameData.bin"
            File(staged, path).parentFile?.listFiles()?.filter { it.name.startsWith(group) }?.forEach { it.delete() }
            wiiData.find(path)?.let { match ->
                val release = match.groupValues[1].chunked(2).map { it.toInt(16).toChar() }.joinToString("")
                if (release !in nativeReleases) {
                    File(staged, release).listFiles()?.filter { it.name.startsWith("Nebula.sav") }?.forEach { it.delete() }
                }
            }
        }
        for ((path, content) in files) {
            val file = File(staged, path)
            file.parentFile?.mkdirs()
            file.writeBytes(content)
        }
    }

    // The .zip's files under the save folder's own top folders, with one shared outer folder (a zipped "saves", or "nand" from earlier builds) taken
    // off. Paths that leave the folder, and sizes past any save's, refuse the whole file.
    private fun unzipSaves(bytes: ByteArray): List<Pair<String, ByteArray>> {
        val entries = ArrayList<Pair<String, ByteArray>>()
        var total = 0L
        ZipInputStream(bytes.inputStream()).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                if (entry.isDirectory) continue
                val path = entry.name.replace('\\', '/').trimStart('/')
                if (path.split('/').any { it == ".." || it.isEmpty() }) throw IOException("The .zip has a path outside its folder")
                val content = readAtMost(zip, MAX_FILE_BYTES) ?: throw IOException("The .zip is far larger than any saves")
                total += content.size
                if (total > MAX_TOTAL_BYTES) throw IOException("The .zip is far larger than any saves")
                entries += path to content
            }
        }
        val first = entries.map { it.first.substringBefore('/', "") }.distinct()
        val strip = first.size == 1 && first[0].isNotEmpty() && !isSaveTopFolder(first[0]) && entries.all { it.first.contains('/') }
        return entries.map { (path, content) -> (if (strip) path.substringAfter('/') else path) to content }
            .filter { it.first == MII_LIBRARY || isSaveTopFolder(it.first.substringBefore('/', "")) }
    }

    // The Mii library the game's icon picker reads (host_main.cpp mii_library_path), at the save folder's top, and its
    // first line (mii_import.cpp kLibraryFormat).
    private const val MII_LIBRARY = "nebula-miis.txt"
    private const val MII_LIBRARY_FORMAT = "nebula-mii-library 1"

    // The save folder's own top folders: a release's (RMGE, RMGP, RMGJ, ...), the Wii layout's, the Mii database's.
    private fun isSaveTopFolder(name: String) = name == "title" || name == "shared2" || Regex("^[A-Z0-9]{4}$").matches(name)

    private fun singleFileName(bytes: ByteArray): String? = when {
        bytes.size >= 16 && bytes[0] == 'N'.code.toByte() && bytes[1] == 'B'.code.toByte() &&
            bytes[2] == 'S'.code.toByte() && bytes[3] == 'V'.code.toByte() -> "Nebula.sav"
        validGameData(bytes) == null -> "GameData.bin"
        else -> null
    }

    private fun singleFileProblem(bytes: ByteArray): String =
        validGameData(bytes) ?: "Choose a .zip made by Export, a Nebula.sav, or a GameData.bin."

    private fun hexTitle(release: String) = release.toByteArray(Charsets.US_ASCII).joinToString("") { "%02x".format(it.toInt() and 0xFF) }

    // SaveFileFormat.hpp's validate (Wii's big-endian version 2, the Switch release's little-endian 2, Nebula's 3):
    // null when the game will read it, else what is wrong.
    private fun validGameData(d: ByteArray): String? {
        if (d.size == 111360) return "This is a Wii SD-card export (data.bin), which is encrypted. Copy GameData.bin out of the Wii's or Dolphin's save folder instead."
        if (d.size < 16) return "Choose a .zip made by Export, a Nebula.sav, or a GameData.bin."
        fun u16(at: Int, big: Boolean) = if (big) (d[at].toInt() and 0xFF) shl 8 or (d[at + 1].toInt() and 0xFF)
            else (d[at + 1].toInt() and 0xFF) shl 8 or (d[at].toInt() and 0xFF)
        fun u32(at: Int, big: Boolean): Long = if (big) (u16(at, true).toLong() shl 16) or u16(at + 2, true).toLong()
            else (u16(at + 2, false).toLong() shl 16) or u16(at, false).toLong()
        val big = u32(4, true) == 2L
        val little = u32(4, false)
        if (!big && little != 2L && little != 3L) return "Choose a .zip made by Export, a Nebula.sav, or a GameData.bin."
        val entries = u32(8, big)
        val size = u32(12, big)
        if (size < 16 || ((size + 31) and 31L.inv()) != d.size.toLong()) return "That GameData.bin is cut short or padded wrongly."
        if (entries == 0L || entries >= 24 || entries > (size - 16) / 16) return "That GameData.bin's file table is damaged."
        val tableEnd = 16 + entries * 16
        for (i in 0 until entries.toInt()) {
            val entry = 16 + i * 16
            var nameBytes = 0
            while (nameBytes < 12 && d[entry + nameBytes].toInt() != 0) {
                val c = d[entry + nameBytes].toInt() and 0xFF
                if (c < 32 || c > 126 || c == '/'.code || c == '\\'.code || c == ':'.code) return "That GameData.bin's file table is damaged."
                nameBytes++
            }
            if (nameBytes == 0 || nameBytes == 12) return "That GameData.bin's file table is damaged."
            val start = u32(entry + 12, big)
            val end = if (i + 1 < entries) u32(entry + 28, big) else size
            if (start < tableEnd || start > end || end > size || end - start < 4) return "That GameData.bin's file table is damaged."
            if (d[start.toInt()].toInt() != 1) return "That GameData.bin is damaged."
            var position = 4L
            for (chunk in 0 until (d[start.toInt() + 1].toInt() and 0xFF)) {
                val remaining = end - start - position
                if (remaining < 12) return "That GameData.bin is damaged."
                val chunkSize = u32((start + position + 8).toInt(), big)
                if (chunkSize < 12 || chunkSize > remaining) return "That GameData.bin is damaged."
                position += chunkSize
            }
        }
        var sum = 0L
        var inverse = 0L
        var i = 4
        while (i + 1 < size) {
            val word = u16(i, big)
            sum += word
            inverse += word.inv() and 0xFFFF
            i += 2
        }
        val checksum = ((sum and 0xFFFF) shl 16) or (inverse and 0xFFFF)
        return if (u32(0, big) == checksum) null else "That GameData.bin's checksum does not match: the file is damaged."
    }

    // The saves in place as save-backups/Saves before import <time>.zip; the newest five are kept. False: none to keep.
    private fun backUp(context: Context): Boolean {
        val files = saveFiles(GameFiles.saves(context))
        if (files.isEmpty()) return false
        val folder = File(GameFiles.data(context), "save-backups")
        folder.mkdirs()
        val stamp = SimpleDateFormat("yyyy-MM-dd HHmmss", Locale.ROOT).format(Date())
        val backup = File(folder, "Saves before import $stamp.zip")
        val writing = File(folder, ".${backup.name}.writing")
        writing.outputStream().use { zip(files, it) }
        if (!writing.renameTo(backup)) throw IOException("The backup of your saves could not be written")
        folder.listFiles { f -> f.name.startsWith("Saves before import ") }.orEmpty()
            .sortedByDescending { it.name }.drop(BACKUPS_KEPT).forEach { it.delete() }
        return true
    }

    // The staged folder becomes the save folder: renames, so a failure leaves the old saves whole.
    private fun replaceSaves(saveFolder: File, staged: File) {
        val old = File(saveFolder.parentFile, ".saves-replaced-${System.nanoTime()}")
        if (saveFolder.exists() && !saveFolder.renameTo(old)) throw IOException("The save folder could not be replaced")
        if (!staged.renameTo(saveFolder)) {
            old.renameTo(saveFolder)
            throw IOException("The save folder could not be replaced")
        }
        old.deleteRecursively()
    }
}
