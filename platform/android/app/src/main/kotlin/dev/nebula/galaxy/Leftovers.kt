// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import java.io.File

/**
 * What the launcher's imports leave when the app is ended during one (P-096): each import copies or stages into a
 * folder of its own and removes it at its end, so one cut off keeps it for good. Cleared at the launcher's start, when
 * none of these imports runs (they run in the launcher's own process).
 */
object Leftovers {
    /** A save import's staged saves, in the data folder (SaveTransfer.import). */
    const val SAVE_INCOMING = ".save-incoming-"
    /** The saves a save import moved aside, beside the save folder (SaveTransfer.replaceSaves). */
    const val SAVES_REPLACED = ".saves-replaced-"
    /** A GPU driver import's unpacked package, in the drivers' folder (GpuDrivers.import). */
    const val DRIVER_INCOMING = ".incoming-"
    /** A save backup being written, in the backups' folder (SaveTransfer.backUp). */
    const val WRITING = ".writing"

    /** The saves a save import moved aside, newest first. */
    private fun replaced(saves: File): List<File> =
        saves.parentFile?.listFiles { file -> file.isDirectory && file.name.startsWith(SAVES_REPLACED) }
            ?.sortedByDescending { it.lastModified() }.orEmpty()

    /**
     * A save import cut off between its two renames left no save folder and the saves moved aside: they are put back
     * (one rename). Run before anything reads the saves, so the game never starts with none. True when put back.
     */
    fun restoreSaves(saves: File): Boolean = !saves.exists() && replaced(saves).firstOrNull()?.renameTo(saves) == true

    /**
     * [data]: the game's data folder, which holds the mod and save imports' folders, the drivers' folder (gpu_drivers)
     * and the backups' (save-backups); [saves]: the save folder. Saves moved aside are removed only while the save folder
     * is there ([restoreSaves] first). Returns the bytes removed.
     */
    fun clear(data: File, saves: File): Long {
        var freed = 0L
        if (saves.exists()) {
            for (folder in replaced(saves)) freed += remove(folder)
        }
        for (folder in GameSwap.modIncoming(data)) freed += remove(folder)
        data.listFiles { file -> file.isDirectory && file.name.startsWith(SAVE_INCOMING) }?.forEach { freed += remove(it) }
        File(data, "gpu_drivers").listFiles { file -> file.isDirectory && file.name.startsWith(DRIVER_INCOMING) }
            ?.forEach { freed += remove(it) }
        File(data, "save-backups").listFiles { file -> file.isFile && file.name.startsWith(".") && file.name.endsWith(WRITING) }
            ?.forEach { freed += remove(it) }
        return freed
    }

    private fun remove(file: File): Long {
        val bytes = file.walkBottomUp().filter { it.isFile }.sumOf { it.length() }
        return if (file.deleteRecursively()) bytes else 0L
    }
}
