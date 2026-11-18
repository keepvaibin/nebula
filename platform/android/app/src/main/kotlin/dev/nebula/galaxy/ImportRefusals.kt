// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.Context

/** The importer's refusals (crates/nebula-import Refusal::id) in the player's words (localization/NebulaText.tsv). */
object ImportRefusals {
    /** The id's words; null for none, or an id this app does not know (then the importer's English is shown). */
    fun text(context: Context, id: String?): String? {
        val words = when (id) {
            "folders-only" -> R.string.RefusedFoldersOnly
            "not-a-disc" -> R.string.RefusedNotADisc
            "missing" -> R.string.RefusedMissing
            "not-a-folder" -> R.string.RefusedNotAFolder
            "damaged" -> R.string.RefusedDamaged
            "gamecube" -> R.string.RefusedGameCube
            "galaxy-2" -> R.string.RefusedGalaxy2
            "not-galaxy" -> R.string.RefusedNotGalaxy
            "region" -> R.string.DiscNotGame
            "changed" -> R.string.RefusedChanged
            "no-space" -> R.string.RefusedNoSpace
            else -> return null
        }
        return context.getString(words)
    }

    /** What Importer.build returned ("<id>\t<English>", or the English alone) as the player is told it. */
    fun failure(context: Context, error: String): String {
        val tab = error.indexOf('\t')
        if (tab > 0) text(context, error.substring(0, tab))?.let { return it }
        val english = if (tab > 0) error.substring(tab + 1) else error
        return english.replaceFirstChar { it.uppercase() }.let { if (it.endsWith(".")) it else "$it." }
    }
}
