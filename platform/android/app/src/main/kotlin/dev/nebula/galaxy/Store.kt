// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.Context
import android.os.StatFs
import java.io.File
import java.util.Properties

private const val STORE_MAGIC = "NBSTORE\u0000"

/** The store version the importer writes and the game reads (crates/nebula-import cook/store.rs VERSION). */
private val currentStoreVersion: Int by lazy { Importer.storeVersion() }

/**
 * False only when the file was read and is not a store of the current version (cook/store.rs: the magic, then the
 * version as a little-endian u32): a read that fails removes nothing.
 */
private fun isCurrentStore(file: File): Boolean = runCatching {
    file.inputStream().use { input ->
        val head = ByteArray(12)
        if (input.read(head) != head.size || String(head, 0, 8, Charsets.US_ASCII) != STORE_MAGIC) return@use false
        val version = (head[8].toInt() and 0xFF) or ((head[9].toInt() and 0xFF) shl 8) or
            ((head[10].toInt() and 0xFF) shl 16) or ((head[11].toInt() and 0xFF) shl 24)
        version == currentStoreVersion
    }
}.getOrDefault(true)

/** What setup installed: written after game.pak is complete, so its presence means a playable game. */
data class Install(
    val gameId: String,
    val title: String,
    val format: String,
    val source: String,
    val bytes: Long,
    val installedAt: Long,
) {
    companion object {
        fun read(context: Context): Install? {
            if (outdated(context)) return null
            val properties = Properties()
            return runCatching {
                GameFiles.install(context).inputStream().use { properties.load(it) }
                Install(
                    gameId = properties.getProperty("gameId", ""),
                    title = properties.getProperty("title", ""),
                    format = properties.getProperty("format", ""),
                    source = properties.getProperty("source", ""),
                    bytes = properties.getProperty("bytes", "0").toLong(),
                    installedAt = properties.getProperty("installedAt", "0").toLong(),
                )
            }.getOrNull()
        }

        /**
         * A game set up by an earlier Nebula whose store the game no longer reads (its version is not the importer's):
         * it needs setting up again from the disc image. The saves, Miis and settings stay.
         */
        fun outdated(context: Context): Boolean = GameFiles.installed(context) && !isCurrentStore(GameFiles.pak(context))
    }

    fun write(context: Context) {
        val properties = Properties()
        properties["gameId"] = gameId
        properties["title"] = title
        properties["format"] = format
        properties["source"] = source
        properties["bytes"] = bytes.toString()
        properties["installedAt"] = installedAt.toString()
        val file = GameFiles.install(context)
        val temporary = File(file.parentFile, ".${file.name}.writing")
        temporary.outputStream().use { properties.store(it, "Nebula setup") }
        temporary.renameTo(file)
    }
}

object Storage {
    /** game.pak of the North American release (crates/nebula-import), and room to spare beside it. */
    const val GAME_BYTES = 3_913_558_602L
    const val MARGIN_BYTES = 256L * 1024 * 1024

    fun freeBytes(context: Context): Long = StatFs(GameFiles.data(context).path).availableBytes

    fun saveBytes(context: Context): Long = GameFiles.saves(context).walkTopDown().filter { it.isFile }.sumOf { it.length() }

    /** The game's files, not the saves (saves/) or settings. */
    fun removeGame(context: Context) {
        GameFiles.install(context).delete()
        File(GameFiles.data(context), "game").deleteRecursively()
    }
}

// Decimal units, as Android's storage settings count.
fun formatBytes(bytes: Long): String = when {
    bytes >= 1_000_000_000L -> String.format(java.util.Locale.getDefault(), "%.2f GB", bytes / 1e9)
    bytes >= 1_000_000L -> String.format(java.util.Locale.getDefault(), "%.0f MB", bytes / 1e6)
    bytes >= 1_000L -> String.format(java.util.Locale.getDefault(), "%.0f kB", bytes / 1e3)
    else -> "$bytes bytes"
}
