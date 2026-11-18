// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import java.io.File

/**
 * Setup's files in the game folder (files/game): the game in place (game.pak, then install.properties, written last),
 * and the new game built beside it (game.pak.new). The game in place goes only once the new one is whole, so a failed,
 * cancelled or cut-off setup leaves the game that was there. Also the room and leftovers of the launcher's mod imports
 * (ModImport).
 */
object GameSwap {
    const val PAK = "game.pak"
    const val INSTALL = "install.properties"
    const val NEW = "game.pak.new"

    /** How a setup makes room. */
    enum class Room {
        /** The new game is built beside the one in place, which stays until the new one is whole. */
        BESIDE,
        /** Room for one game only: the one in place goes first (the launcher says so before it starts). */
        REPLACE_FIRST,
        /** Not enough room even without the game in place. */
        NONE,
    }

    /** [free]: the device's free bytes; [inPlace]: the bytes the game in place and setup's leftovers take. */
    fun room(free: Long, inPlace: Long, needed: Long): Room = when {
        free >= needed -> Room.BESIDE
        free + inPlace >= needed -> Room.REPLACE_FIRST
        else -> Room.NONE
    }

    /** The bytes setup may take back: the game in place and any leftover of a setup that was cut off. */
    fun reclaimable(folder: File): Long =
        listOf(PAK, NEW, INSTALL).sumOf { name -> File(folder, name).let { if (it.isFile) it.length() else 0L } }

    /**
     * Leftovers of a setup that was cut off (the app ended, the device turned off): a new game never swapped in, and a
     * game.pak with no install.properties (a first setup that never finished, or a swap cut off between its two
     * steps). Not while a setup runs. Returns the bytes freed.
     */
    fun sweep(folder: File): Long {
        var freed = 0L
        val new = File(folder, NEW)
        if (new.isFile) {
            val bytes = new.length()
            if (new.delete()) freed += bytes
        }
        val pak = File(folder, PAK)
        if (pak.isFile && !File(folder, INSTALL).isFile) {
            val bytes = pak.length()
            if (pak.delete()) freed += bytes
        }
        return freed
    }

    /** The game in place removed: install.properties first, so a removal cut off leaves no install naming a missing pak. */
    fun removeInPlace(folder: File) {
        File(folder, INSTALL).delete()
        File(folder, PAK).delete()
    }

    /**
     * The new game, whole, takes the place of the old: install.properties goes, game.pak.new is renamed over game.pak
     * (one rename: the old file goes with it), then [writeInstall] writes the new install.properties. False when the
     * rename failed; the game in place is then without its install file and the next start sweeps it.
     */
    fun commit(folder: File, writeInstall: () -> Unit): Boolean {
        File(folder, INSTALL).delete()
        if (!replaceFile(File(folder, NEW), File(folder, PAK))) return false
        writeInstall()
        return true
    }

    /** A setup that failed or was cancelled: its new game goes, the game in place stays. */
    fun abandon(folder: File) {
        File(folder, NEW).delete()
    }

    /** The launcher's copy of a file a mod import reads, in the app's files folder: "<this><n>". */
    const val MOD_INCOMING = ".mod-incoming-"

    /** Room a mod import keeps free beside the file twice over (its copy, then the mod unpacked). */
    const val MOD_MARGIN_BYTES = 64L * 1024 * 1024

    /** The copy folders of mod imports in [data] (each import removes its own; one the app's end cut off stays). */
    fun modIncoming(data: File): List<File> =
        data.listFiles { file -> file.isDirectory && file.name.startsWith(MOD_INCOMING) }?.toList() ?: emptyList()

    /** A build's error from a full device (ENOSPC: "No space left on device (os error 28)" from the importer). */
    fun isNoSpace(error: String): Boolean =
        error.contains("os error 28") || error.contains("ENOSPC") || error.contains("No space left", ignoreCase = true)
}
