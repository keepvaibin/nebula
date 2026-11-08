// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.util.Locale

/**
 * What a file dragged onto the launcher from another app (split screen, a desktop mode, a Chromebook) is for, so each
 * reaches the importer its picker would: a disc image goes to setup, saves to Import saves, Mii files to Import Miis,
 * anything else to the mod importer (which says so when it is not a mod).
 */
enum class DropKind {
    Disc, Saves, Miis, Mod;

    companion object {
        // The disc images setup reads (Importer.identify).
        private val DISC = setOf("iso", "rvz", "wia", "wbfs", "ciso", "gcz")

        // Every kind of Mii file the game's reader takes (runtime/src/mii_formats.cpp; Windows' Import Miis filter).
        private val MII = setOf(
            "mii", "rcd", "rsd", "mae", "miigx", "ncd", "nsd", "cfcd", "cfsd", "ffcd", "ffsd", "charinfo", "nfsd", "nfcd",
            "mnms", "emnms", "miic", "dat", "bin", "txt",
        )

        /** Saves are known by what is in them; the rest by the file's name. */
        fun of(context: Context, uri: Uri): DropKind {
            val (name, size) = nameAndSize(context, uri)
            val extension = name.substringAfterLast('.', "").lowercase(Locale.ROOT)
            return when {
                extension in DISC -> Disc
                SaveTransfer.holdsSaves(context, uri, size) -> Saves
                extension in MII -> Miis
                else -> Mod
            }
        }

        fun nameAndSize(context: Context, uri: Uri): Pair<String, Long> {
            var name = uri.lastPathSegment.orEmpty()
            var size = -1L
            runCatching {
                context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)
                    ?.use { c ->
                        if (c.moveToFirst()) {
                            if (!c.isNull(0)) name = c.getString(0)
                            if (!c.isNull(1)) size = c.getLong(1)
                        }
                    }
            }
            return name to size
        }
    }
}
