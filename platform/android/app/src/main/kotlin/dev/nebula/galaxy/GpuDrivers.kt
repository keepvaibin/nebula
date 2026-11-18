// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.app.ActivityManager
import android.content.Context
import android.net.Uri
import android.os.Build
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.zip.ZipInputStream

/** A Vulkan driver the player imported: its package's meta.json and the folder it was unpacked to. */
data class GpuDriver(
    val id: String,
    val name: String,
    val version: String,
    val vendor: String,
    val author: String,
    val description: String,
    /** The driver's library in [folder] (meta.json libraryName). */
    val library: String,
    val folder: File,
)

/**
 * Graphics drivers the player imports in place of the system's (Turnip, or another build for Adreno GPUs): the package
 * format the Android emulators share (a .zip holding meta.json and the driver's library). The game loads the selected
 * one through libadrenotools (vk_context.cpp load_vulkan); a start that shows no frame with it (it crashed, or made no
 * device) leaves the file "starting" in its folder, and the launcher then selects the system driver again.
 */
object GpuDrivers {
    /** The start intent's extra with the game's arguments for the selected driver ([launchArguments]). */
    const val EXTRA = "nebula.gpuDriver"
    private const val SELECTED = "gpuDriver"
    private const val STARTING = "starting"
    private const val META = "meta.json"
    private const val MOST_BYTES = 512L shl 20

    /** libadrenotools serves Qualcomm's Adreno GPUs, in 64-bit ARM apps only. */
    fun supported(): Boolean =
        Build.SUPPORTED_ABIS.firstOrNull() == "arm64-v8a" &&
            (Build.SOC_MANUFACTURER.equals("QTI", ignoreCase = true) || Build.SOC_MANUFACTURER.contains("qualcomm", ignoreCase = true))

    private fun root(context: Context) = File(GameFiles.data(context), "gpu_drivers")

    /** The imported drivers, by name. */
    fun list(context: Context): List<GpuDriver> =
        root(context).listFiles()?.filter { it.isDirectory && !it.name.startsWith(".") }?.mapNotNull { read(it) }
            ?.sortedBy { it.name.lowercase() } ?: emptyList()

    private fun read(folder: File): GpuDriver? = runCatching {
        val meta = JSONObject(File(folder, META).readText())
        val library = meta.getString("libraryName")
        if (!File(folder, library).isFile) return null
        GpuDriver(
            id = folder.name,
            name = meta.optString("name").ifEmpty { library },
            version = meta.optString("driverVersion"),
            vendor = meta.optString("vendor"),
            author = meta.optString("author"),
            description = meta.optString("description"),
            library = library,
            folder = folder,
        )
    }.getOrNull()

    /** The driver the game starts with; null: the system's. */
    fun selected(context: Context): GpuDriver? {
        if (!supported()) return null
        val id = GameFiles.launcher(context).getString(SELECTED, null) ?: return null
        return list(context).firstOrNull { it.id == id }
    }

    fun select(context: Context, driver: GpuDriver?) {
        val edit = GameFiles.launcher(context).edit()
        if (driver == null) edit.remove(SELECTED) else edit.putString(SELECTED, driver.id)
        edit.commit()
    }

    fun remove(context: Context, driver: GpuDriver) {
        if (selected(context)?.id == driver.id) select(context, null)
        driver.folder.deleteRecursively()
    }

    /**
     * A driver package from a file the player picked, unpacked into the app's own storage (the only place Android lets
     * the game load a library from) and selected. A package with the same name and version replaces the one there.
     */
    fun import(context: Context, uri: Uri): GpuDriver {
        val root = root(context)
        val incoming = File(root, "${Leftovers.DRIVER_INCOMING}${System.nanoTime()}")
        if (!incoming.mkdirs()) throw IOException(context.getString(R.string.FileUnreadable))
        try {
            val input = context.contentResolver.openInputStream(uri) ?: throw IOException(context.getString(R.string.FileUnopenable))
            var total = 0L
            ZipInputStream(input.buffered()).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    // The packages are flat: a file in a folder, or a name that leaves the folder, is not the driver's.
                    val name = entry.name
                    if (entry.isDirectory || name.contains('/') || name.contains('\\') || name == "." || name == "..") continue
                    File(incoming, name).outputStream().use { out ->
                        val buffer = ByteArray(1 shl 16)
                        while (true) {
                            val read = zip.read(buffer)
                            if (read < 0) break
                            total += read
                            if (total > MOST_BYTES) throw IOException(context.getString(R.string.DriverNotPackage))
                            out.write(buffer, 0, read)
                        }
                    }
                }
            }
            val meta = runCatching { JSONObject(File(incoming, META).readText()) }.getOrNull()
                ?: throw IOException(context.getString(R.string.DriverNotPackage))
            val library = meta.optString("libraryName")
            if (library.isEmpty() || !library.endsWith(".so") || library.contains('/')) throw IOException(context.getString(R.string.DriverNotPackage))
            if (!File(incoming, library).isFile) throw IOException(context.getString(R.string.DriverMissingLibrary, library))
            if (meta.optInt("minApi", 0) > Build.VERSION.SDK_INT) throw IOException(context.getString(R.string.DriverNeedsAndroid))
            val label = listOf(meta.optString("name"), meta.optString("driverVersion")).filter { it.isNotEmpty() }.joinToString("-")
                .ifEmpty { library.removeSuffix(".so") }
            val id = label.replace(Regex("[^A-Za-z0-9._-]"), "_").trim('.', '_').ifEmpty { "driver" }.take(80)
            val folder = File(root, id)
            folder.deleteRecursively()
            if (!incoming.renameTo(folder)) throw IOException(context.getString(R.string.FileUnreadable))
            val driver = read(folder) ?: throw IOException(context.getString(R.string.DriverNotPackage))
            select(context, driver)
            return driver
        } finally {
            incoming.deleteRecursively()
        }
    }

    /**
     * The game's arguments for the selected driver (none for the system's). The file "starting" goes into its folder
     * first; the game's first frame removes it.
     */
    fun launchArguments(context: Context): List<String> {
        val driver = selected(context) ?: return emptyList()
        File(driver.folder, STARTING).writeText("")
        return listOf(
            "--gpu-driver", driver.folder.path,
            "--gpu-driver-library", driver.library,
            "--gpu-driver-hooks", context.applicationInfo.nativeLibraryDir,
        )
    }

    /**
     * After a run: the selected driver's start that never showed a frame, while no game runs. The system driver is
     * selected again and the driver's name given, for the launcher to say so; null when nothing failed.
     */
    fun failedStart(context: Context): String? {
        val driver = selected(context) ?: return null
        val starting = File(driver.folder, STARTING)
        if (!starting.isFile || gameRunning(context)) return null
        starting.delete()
        select(context, null)
        return driver.name
    }

    private fun gameRunning(context: Context): Boolean =
        context.getSystemService(ActivityManager::class.java).runningAppProcesses
            ?.any { it.processName == context.packageName + ":game" } == true
}
