package dev.nebula.galaxy

import android.content.Context
import org.json.JSONObject
import java.io.File

/** One recommended setting: a config.ini key, its value when the benchmark ran, and the recommended one. */
data class BenchmarkSetting(val key: String, val current: Int, val recommended: Int)

/** The benchmark's result file (perf/bench, schema 1): how the run ended, the verdict, the recommended settings. */
data class BenchmarkResult(
    val status: String,
    val failure: String,
    val verdict: String,
    val fits: Boolean,
    val cpuBound: Boolean,
    val settings: List<BenchmarkSetting>,
) {
    val complete: Boolean get() = status == "complete"

    /** The rows the player is shown and Apply writes: those whose recommended value differs. */
    val changes: List<BenchmarkSetting> get() = if (complete) settings.filter { it.current != it.recommended } else emptyList()
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

    private fun configFile(context: Context): File = File(GameFiles.data(context), "config.ini")

    /** The result, or null when the text is not schema 1's. */
    fun parse(text: String): BenchmarkResult? = runCatching {
        val root = JSONObject(text)
        val schema = root.opt("schema")
        if (schema !is Int && schema !is Long || (schema as Number).toLong() != 1L) return null
        val summary = root.optJSONObject("summary")
        val recommendation = root.optJSONObject("recommendation")
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
    }

    /**
     * Apply: the recommended values of the differing keys into config.ini and nothing else (per_pixel_lighting stays as
     * the benchmark measured it); every other line as it was. Written to a new file and renamed over the old one.
     * False when it could not be written.
     */
    fun apply(context: Context, result: BenchmarkResult): Boolean {
        // Each key by name (tools/settings_table.py reads the keys the Android launcher writes from these lines).
        val recommended = result.changes.associate { it.key to it.recommended.toString() }
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
