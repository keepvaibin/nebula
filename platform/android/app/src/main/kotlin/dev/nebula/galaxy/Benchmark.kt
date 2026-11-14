package dev.nebula.galaxy

import android.content.Context
import org.json.JSONObject
import java.io.File

/** One recommended setting: a config.ini key, its value when the benchmark ran, and the recommended one. */
data class BenchmarkSetting(val key: String, val current: Int, val recommended: Int)

/**
 * Whether a result still holds: [Current] (config.ini as it ran), [Applied] (config.ini as it recommends), [Stale] (a
 * setting, Nebula or the graphics card changed since: shown, not applied).
 */
enum class BenchmarkState { Current, Applied, Stale }

/**
 * The benchmark's result file (perf/bench, schema 1): how the run ended, the verdict, the recommended settings, and
 * what it ran with (the player's config.ini values, the adapter) to tell whether it still holds.
 */
data class BenchmarkResult(
    val status: String,
    val failure: String,
    val verdict: String,
    val fits: Boolean,
    val cpuBound: Boolean,
    val settings: List<BenchmarkSetting>,
    val ranWith: Map<String, Double> = emptyMap(),
    val gpu: String = "",
    val vendorId: Long = 0,
    val deviceId: Long = 0,
) {
    val complete: Boolean get() = status == "complete"

    /** The rows whose recommended value differs from the one it ran with. */
    val changes: List<BenchmarkSetting> get() = if (complete) settings.filter { it.current != it.recommended } else emptyList()

    /**
     * Whether it still holds (perf/bench's rule): a run this launcher's version started ([ranBy], recorded when it
     * ended, against [version]), on the adapter of the game's last normal start ([adapter], adapter.ini, null when none
     * came after the run), and [config] holding every value it ran with ([BenchmarkState.Current]) or, after Apply, the
     * recommended ones in their place ([BenchmarkState.Applied]). Anything else is stale.
     */
    fun stateNow(config: Map<String, String>, adapter: Map<String, String>?, ranBy: String?, version: String): BenchmarkState {
        if (!complete || ranWith.isEmpty() || ranBy != version) return BenchmarkState.Stale
        if (adapter != null && !sameAdapter(adapter)) return BenchmarkState.Stale
        var current = true
        var applied = true
        for ((key, ran) in ranWith) {
            val now = Benchmark.configValue(config, key) ?: continue   // a key this check does not know stays out
            val recommended = settings.firstOrNull { it.key == key }?.recommended?.toDouble() ?: ran
            if (Math.abs(now - ran) > 0.0005) current = false
            if (Math.abs(now - recommended) > 0.0005) applied = false
        }
        return if (current) BenchmarkState.Current else if (applied) BenchmarkState.Applied else BenchmarkState.Stale
    }

    // The same PCI ids, or with no ids the same name (the result keeps printable ASCII, at most 96 bytes).
    private fun sameAdapter(adapter: Map<String, String>): Boolean {
        val vendor = adapter["vendor_id"]?.trim()?.toLongOrNull()
        val device = adapter["device_id"]?.trim()?.toLongOrNull()
        if (vendorId != 0L && vendor != null && device != null && vendor != 0L) return vendor == vendorId && device == deviceId
        val name = adapter["description"] ?: return true
        if (gpu.isEmpty()) return true
        return name.trim().filter { it.code in 0x20..0x7e }.take(96) == gpu
    }
}

/**
 * The benchmark from the launcher (perf/bench's contract): GameActivity started with [EXTRA] runs the game with
 * --benchmark 1 and [resultFile] in place of the saves; the game shows its steps and exits by itself, and the launcher
 * reads the file when it comes back (written atomically: present means whole). Apply writes the recommended keys into
 * config.ini, the other lines untouched.
 */
object Benchmark {
    const val EXTRA = "nebula.benchmark"

    // The keys the benchmark may recommend, the ones Apply writes (the contract's list).
    val KEYS = listOf("internal_height", "msaa_samples", "ambient_occlusion", "enhanced_lighting", "aa", "smaa_quality", "bloom")

    fun resultFile(context: Context): File = File(GameFiles.data(context), "benchmark-result.json")

    // Beside the result: the launcher version whose run wrote it (the result's app.version is the game's
    // NEBULA_VERSION, the CMake project version, not the app's versionName, so the launcher records its own).
    private fun ranByFile(context: Context): File = File(GameFiles.data(context), "benchmark-result.json.launcher")

    /** A run of this launcher ended with a result. */
    fun recordRanBy(context: Context) {
        runCatching { ranByFile(context).writeText(BuildConfig.VERSION_NAME) }
    }

    // The engine's value of a key absent from config.ini (runtime/src/ui/settings_table.tsv, Android).
    private val DEFAULTS = mapOf(
        "internal_height" to 0.0, "upscaler" to 0.0, "msaa_samples" to 1.0, "ambient_occlusion" to 0.0,
        "ambient_occlusion_ssao" to 0.0, "enhanced_lighting" to 0.0, "per_pixel_lighting" to 0.0, "aa" to 0.0,
        "smaa_quality" to 2.0, "bloom" to 100.0, "vsync_interval" to 0.0, "max_fps" to 60.0, "frame_generation" to 0.0,
        "ray_tracing_enabled" to 0.0, "display_mode" to 1.0,
    )
    private val FLAGS = setOf("ambient_occlusion", "ambient_occlusion_ssao", "enhanced_lighting", "per_pixel_lighting", "ray_tracing_enabled")

    // Choices config.ini may hold by name, the n-th name meaning n (render_config.cpp).
    private val NAMES = mapOf(
        "upscaler" to listOf("bilinear", "fsr", "fsr3", "dlss", "xess", "sgsr2", "metalfx"),
        "aa" to listOf("off", "smaa", "auto", "fxaa"),
        "smaa_quality" to listOf("low", "medium", "high", "ultra"),
        "display_mode" to listOf("windowed", "borderless", "fullscreen"),
    )

    /** A key's value as the engine reads config.ini's text; null for a key this check does not know. */
    fun configValue(config: Map<String, String>, key: String): Double? {
        val fallback = DEFAULTS[key] ?: return null
        val text = config[key]?.trim() ?: return fallback
        if (key in FLAGS) return when (text) { "1", "true" -> 1.0; "0", "false" -> 0.0; else -> fallback }
        val index = NAMES[key]?.indexOfFirst { it.equals(text, ignoreCase = true) } ?: -1
        if (index >= 0) return index.toDouble()
        return text.toDoubleOrNull() ?: fallback
    }

    // An ini file's keys and their texts, the last line of a key (comments and lines without a key skipped).
    private fun readIni(file: File): Map<String, String> {
        val values = HashMap<String, String>()
        if (!file.isFile) return values
        for (line in file.readLines()) {
            val equals = line.indexOf('=')
            if (equals <= 0) continue
            val key = line.substring(0, equals).trim()
            if (key.startsWith("#") || key.startsWith(";")) continue
            values[key] = line.substring(equals + 1)
        }
        return values
    }

    /** The result's state now, against config.ini and adapter.ini (a benchmark writes neither). */
    fun state(context: Context, result: BenchmarkResult): BenchmarkState = runCatching {
        val adapter = File(GameFiles.data(context), "adapter.ini")
        // adapter.ini as a normal start wrote it after the run; before one, the run's adapter is the latest used.
        val started = adapter.isFile && adapter.lastModified() > resultFile(context).lastModified()
        val ranBy = ranByFile(context).takeIf { it.isFile }?.readText()?.trim()
        result.stateNow(readIni(configFile(context)), if (started) readIni(adapter) else null, ranBy, BuildConfig.VERSION_NAME)
    }.getOrDefault(BenchmarkState.Stale)

    private fun configFile(context: Context): File = File(GameFiles.data(context), "config.ini")

    /** The result, or null when the text is not schema 1's. */
    fun parse(text: String): BenchmarkResult? = runCatching {
        val root = JSONObject(text)
        val schema = root.opt("schema")
        if (schema !is Int && schema !is Long || (schema as Number).toLong() != 1L) return null
        val summary = root.optJSONObject("summary")
        val recommendation = root.optJSONObject("recommendation")
        val device = root.optJSONObject("device")
        val ranWith = HashMap<String, Double>()
        root.optJSONObject("settings")?.let { ran ->
            for (key in ran.keys()) {
                when (val value = ran.opt(key)) {
                    is Boolean -> ranWith[key] = if (value) 1.0 else 0.0
                    is Number -> ranWith[key] = value.toDouble()
                }
            }
        }
        val settings = mutableListOf<BenchmarkSetting>()
        val rows = recommendation?.optJSONArray("settings")
        if (rows != null) {
            for (i in 0 until rows.length()) {
                val row = rows.optJSONObject(i) ?: continue
                val key = row.optString("key")
                val current = row.opt("current")
                val recommended = row.opt("recommended")
                if (key !in KEYS || current !is Number || recommended !is Number) continue
                settings += BenchmarkSetting(key, Math.round(current.toDouble()).toInt(), Math.round(recommended.toDouble()).toInt())
            }
        }
        BenchmarkResult(
            status = root.optString("status"),
            failure = root.optString("failure"),
            verdict = summary?.optString("verdict", "none") ?: "none",
            fits = recommendation?.optBoolean("fits") ?: false,
            cpuBound = recommendation?.optBoolean("cpu_bound") ?: false,
            settings = settings,
            ranWith = ranWith,
            gpu = device?.optString("gpu").orEmpty(),
            vendorId = (device?.opt("vendor_id") as? Number)?.toLong() ?: 0L,
            deviceId = (device?.opt("device_id") as? Number)?.toLong() ?: 0L,
        )
    }.getOrNull()

    /** The last run's result, or null when there is none. */
    fun read(context: Context): BenchmarkResult? {
        val file = resultFile(context)
        if (!file.isFile) return null
        return runCatching { parse(file.readText()) }.getOrNull()
    }

    /** Before a run: the last result goes, so a run that ends without one is told apart. */
    fun clear(context: Context) {
        resultFile(context).delete()
        ranByFile(context).delete()
    }

    /**
     * Apply: every recommended value into config.ini (the combination the run measured, not only the rows that
     * differ) and nothing else (per_pixel_lighting stays as the benchmark measured it); every other line as it was.
     * Written to a new file and renamed over the old one. False when it could not be written. The caller applies
     * only a result whose [state] is current.
     */
    fun apply(context: Context, result: BenchmarkResult): Boolean {
        // Each key by name (tools/settings_table.py reads the keys the Android launcher writes from these lines).
        val recommended = (if (result.complete) result.settings else emptyList()).associate { it.key to it.recommended.toString() }
        val values = LinkedHashMap<String, String>()
        recommended["internal_height"]?.let { values["internal_height"] = it }
        recommended["msaa_samples"]?.let { values["msaa_samples"] = it }
        recommended["ambient_occlusion"]?.let { values["ambient_occlusion"] = it }
        recommended["enhanced_lighting"]?.let { values["enhanced_lighting"] = it }
        recommended["aa"]?.let { values["aa"] = it }
        recommended["smaa_quality"]?.let { values["smaa_quality"] = it }
        recommended["bloom"]?.let { values["bloom"] = it }
        if (values.isEmpty()) return true
        val config = configFile(context)
        return runCatching {
            val lines = if (config.isFile) config.readLines().toMutableList() else mutableListOf()
            val left = LinkedHashMap(values)
            for (i in lines.indices) {
                val equals = lines[i].indexOf('=')
                if (equals <= 0) continue
                val key = lines[i].substring(0, equals).trim()
                if (key.startsWith("#") || key.startsWith(";")) continue
                val value = values[key] ?: continue   // every line of the key (a repeated key: the reader takes the last)
                left.remove(key)
                lines[i] = "$key=$value"
            }
            for ((key, value) in left) lines += "$key=$value"
            val temp = File(config.parentFile, config.name + ".new")
            java.io.FileOutputStream(temp).use { out ->
                out.write(lines.joinToString("\n", postfix = "\n").toByteArray(Charsets.UTF_8))
                out.fd.sync()
            }
            if (!temp.renameTo(config)) {
                temp.delete()
                error("rename")
            }
            true
        }.getOrDefault(false)
    }
}
