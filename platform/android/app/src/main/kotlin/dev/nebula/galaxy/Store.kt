// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.Context
import android.os.StatFs
import java.io.File
import java.util.Properties

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
            if (!GameFiles.installed(context)) return null
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
    const val GAME_BYTES = 3_391_676_376L
    const val MARGIN_BYTES = 256L * 1024 * 1024

    fun freeBytes(context: Context): Long = StatFs(GameFiles.data(context).path).availableBytes

    fun saveBytes(context: Context): Long = GameFiles.nand(context).walkTopDown().filter { it.isFile }.sumOf { it.length() }

    /** The game's files, not the saves (nand/) or settings. */
    fun removeGame(context: Context) {
        GameFiles.install(context).delete()
        File(GameFiles.data(context), "game").deleteRecursively()
    }
}

/** The renderer's settings, config.ini in the data folder (runtime/src/gx/render_config.cpp reads it). */
data class GraphicsSettings(
    val scale: Int,
    val matchScreen: Boolean,
    val vsync: Boolean,
    val displayRate: Boolean,
    val aa: String,         // config.ini's aa: "off", "fxaa" or "smaa"
    val msaa: Int,          // msaa_samples: 1 (off), 2 or 4; SMAA and FXAA are off with it
    val fsr: Boolean,       // upscaler "fsr" (FSR 1) rather than "bilinear"
    val sharpness: Int,     // RCAS, 0-100
    val hudAtScreen: Boolean,  // hud_output_resolution: menus and HUD drawn at the screen's resolution
) {
    companion object {
        // The runtime's defaults: 1x, VSYNC on, 60 fps (displayRate: the display's own rate, frames in between drawn
        // from the game's motion), no anti-aliasing, bilinear enlarging, no sharpening.
        val Default = GraphicsSettings(
            scale = 1, matchScreen = false, vsync = true, displayRate = false, aa = "off", msaa = 1, fsr = false,
            sharpness = 0, hudAtScreen = false,
        )
        const val MAX_SCALE = 4

        fun read(context: Context): GraphicsSettings {
            val values = readIni(GameFiles.renderConfig(context))
            return GraphicsSettings(
                scale = values["efb_scale"]?.toIntOrNull()?.coerceIn(1, MAX_SCALE) ?: Default.scale,
                matchScreen = values["internal_follow_window"]?.let { it == "1" } ?: Default.matchScreen,
                vsync = values["vsync_interval"]?.let { it != "0" } ?: Default.vsync,
                displayRate = values["max_fps"]?.toFloatOrNull()?.let { it == 0f } ?: Default.displayRate,
                aa = values["aa"]?.takeIf { it == "fxaa" || it == "smaa" } ?: Default.aa,
                msaa = values["msaa_samples"]?.toIntOrNull()?.let { if (it >= 4) 4 else if (it >= 2) 2 else 1 } ?: Default.msaa,
                fsr = values["upscaler"]?.let { it == "fsr" } ?: Default.fsr,
                sharpness = values["sharpness"]?.toIntOrNull()?.coerceIn(0, 100) ?: Default.sharpness,
                hudAtScreen = values["hud_output_resolution"]?.let { it == "1" } ?: Default.hudAtScreen,
            )
        }

        private fun readIni(file: File): LinkedHashMap<String, String> {
            val values = LinkedHashMap<String, String>()
            if (!file.isFile) return values
            file.readLines().forEach { line ->
                val equals = line.indexOf('=')
                if (equals > 0 && !line.startsWith("#") && !line.startsWith(";")) {
                    values[line.substring(0, equals).trim()] = line.substring(equals + 1).trim()
                }
            }
            return values
        }
    }

    /** Changes only these keys: whatever else the file holds stays. */
    fun write(context: Context) {
        val file = GameFiles.renderConfig(context)
        val values = readIni(file)
        values["efb_scale"] = scale.toString()
        values["internal_follow_window"] = if (matchScreen) "1" else "0"
        values["vsync_interval"] = if (vsync) "1" else "0"
        values["max_fps"] = if (displayRate) "0" else "60"
        values["aa"] = aa
        values["msaa_samples"] = msaa.toString()
        values["upscaler"] = if (fsr) "fsr" else "bilinear"
        values["sharpness"] = sharpness.toString()
        values["hud_output_resolution"] = if (hudAtScreen) "1" else "0"
        file.parentFile?.mkdirs()
        val temporary = File(file.parentFile, ".config.ini.writing")
        temporary.writeText(values.entries.joinToString("\n", postfix = "\n") { "${it.key}=${it.value}" })
        temporary.renameTo(file)
    }
}

/** The on-screen controls (GameFiles.CONTROLS; the game's process reads them when it starts). */
data class ControlsSettings(val mode: String, val opacity: Float, val scale: Float, val haptics: Boolean) {
    companion object {
        fun read(context: Context): ControlsSettings {
            val prefs = GameFiles.controls(context)
            return ControlsSettings(
                mode = prefs.getString(GameFiles.CONTROLS_MODE, "auto") ?: "auto",
                opacity = prefs.getFloat(GameFiles.CONTROLS_OPACITY, 0.6f),
                scale = prefs.getFloat(GameFiles.CONTROLS_SCALE, 1.0f),
                haptics = prefs.getBoolean(GameFiles.CONTROLS_HAPTICS, true),
            )
        }
    }

    fun write(context: Context) {
        GameFiles.controls(context).edit()
            .putString(GameFiles.CONTROLS_MODE, mode)
            .putFloat(GameFiles.CONTROLS_OPACITY, opacity)
            .putFloat(GameFiles.CONTROLS_SCALE, scale)
            .putBoolean(GameFiles.CONTROLS_HAPTICS, haptics)
            .commit()
    }
}

// Decimal units, as Android's storage settings count.
fun formatBytes(bytes: Long): String = when {
    bytes >= 1_000_000_000L -> String.format(java.util.Locale.getDefault(), "%.2f GB", bytes / 1e9)
    bytes >= 1_000_000L -> String.format(java.util.Locale.getDefault(), "%.0f MB", bytes / 1e6)
    bytes >= 1_000L -> String.format(java.util.Locale.getDefault(), "%.0f kB", bytes / 1e3)
    else -> "$bytes bytes"
}
