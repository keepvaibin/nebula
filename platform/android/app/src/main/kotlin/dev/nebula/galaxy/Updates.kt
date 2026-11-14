// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.util.Base64
import androidx.core.content.IntentCompat
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.math.BigInteger
import java.net.HttpURLConnection
import java.net.URL
import java.security.KeyFactory
import java.security.Signature
import java.security.spec.RSAPublicKeySpec
import java.util.concurrent.atomic.AtomicBoolean

/** A published release newer than this build: its version, notes as plain text, and its signed APK. */
data class Release(
    val version: String,
    val tag: String,
    val notes: String,
    val apkUrl: String,
    val signatureUrl: String,
    val apkBytes: Long,
)

/** Why an update did not install, as the prompt words it. */
enum class UpdateFailure { Download, Rejected, NoRoom }

class UpdateException(val failure: UpdateFailure, message: String) : IOException(message)

/**
 * Nebula's updates for an APK installed from its releases, by the Windows launcher's rules (UpdateService.cs,
 * Updater.cs): the check starts as the launcher opens and never holds it up; no request within an hour of an answer,
 * an answer dated after now checked again; a failed check keeps the last answer and its time; a beta takes betas only;
 * Skip this version. The prompt shows the release's notes; Update downloads the APK (resumed when cut off), checks
 * its release-key signature (the Windows setups' key, installer/release-public-key.xml) and that it is this app at
 * the release's version, and hands it to Android's installer, which asks the player and itself refuses another
 * signing key or a lower version. An install from a store updates through the store: no check.
 */
object Updates {
    const val RELEASES_URL = "https://api.github.com/repos/keepvaibin/nebula/releases?per_page=30"
    private const val CHECK_INTERVAL_MS = 60L * 60L * 1000L
    private const val CONNECT_MS = 10_000
    private const val READ_MS = 15_000
    private const val MAX_LISTING_CHARS = 8 shl 20
    private const val MAX_SIGNATURE_BYTES = 64L * 1024L
    private const val MAX_APK_BYTES = 2L shl 30
    private const val NOTES_LIMIT = 4000
    private const val PREFS = "updates"
    private const val ASK = "ask_at_start"
    private const val SKIPPED = "skipped"
    private const val CACHE = "update-check.json"

    // installer/release-public-key.xml (tools/android_text.py --check compares them).
    const val RELEASE_MODULUS = "rUk5e121s2AdrxlsfX2k8jjcuGmiMrgQ2vqzUq3k0ed5/VjFko4leTGUpcCtDSjlR+UToFHSdmubvhB4yPFMRptscbBzEyNyKYHO6EBp+RY7iBldBN0piupFpxTWiXHPklRiBIZHDjERcWFY/PrwyYS5SMnJ7eWL7AvFCVSU97+xJBJgQ9G3ZEaxqQyHD6OBSpwGyHuI8t9m8yx63fsmA53JdDCFPIf1PuDp6BBACfsMBq47W1TMH61UxjLC4m9ToLgsCe1qVfwLAt3KZS8SCGTI0txejW2kydWdo5dsRhqP5TJpSy289S3pjQYjIB1wHvNyafifyikS6D8BN2z1crP9lGykOD2vBy+e5vfR5BYu/ziXlCVKBfA95833NxFfGoWyZZhZGNV3gzkX5gEdfJe4kb5bQnGYfAKcw5lhdMufv6QLjlKVD5RTgORobOa0ivKzt3JdIb0jpX/xVwgHe2aNVo+LlyjgMc1eOy4IJnMYfdtzDctbUVjQPiuZv5PN"
    const val RELEASE_EXPONENT = "AQAB"

    private val running = AtomicBoolean(false)

    /** The asset this build updates from: a beta's has its own name, so a release build never sees it. */
    fun assetName(installed: String): String = if (isBeta(installed)) "Nebula-Beta-Android.apk" else "Nebula-Android.apk"

    /** The check runs: the player has not turned it off, and no store installed the app. */
    fun enabled(context: Context): Boolean =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(ASK, true) && !fromStore(context)

    fun setEnabled(context: Context, on: Boolean) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(ASK, on).apply()
    }

    fun fromStore(context: Context): Boolean = runCatching {
        val source = context.packageManager.getInstallSourceInfo(context.packageName)
        val installer = source.installingPackageName ?: source.initiatingPackageName
        // A sideloaded APK: the system's package installer (or adb) installed it.
        installer != null && installer != "com.google.android.packageinstaller" && installer != "com.android.packageinstaller" &&
            installer != "com.android.shell" && installer != context.packageName
    }.getOrDefault(false)

    /**
     * Starts the check behind the caller unless the last answer is under an hour old (and not dated after now).
     * [answered] runs on the check's thread with the release to offer, or null.
     */
    fun begin(context: Context, answered: (Release?) -> Unit) {
        val app = context.applicationContext
        if (!enabled(app)) return
        val (_, checkedMs) = readCache(app) ?: (null to 0L)
        val now = System.currentTimeMillis()
        if (checkedMs in 1..now && now - checkedMs < CHECK_INTERVAL_MS) return
        if (!running.compareAndSet(false, true)) return
        Thread({
            try {
                val release = selectNewer(getString(RELEASES_URL), BuildConfig.VERSION_NAME)
                writeCache(app, release, System.currentTimeMillis())
                answered(offered(app, release))
            } catch (_: Exception) {
                // Offline, a portal, a refused or rate-limited answer, a malformed list: the last answer and its time
                // stand, so the next start checks again.
            } finally {
                running.set(false)
            }
        }, "nebula-update-check").apply { isDaemon = true }.start()
    }

    /** The kept answer to offer now: newer than this build, of its channel, and newer than the version skipped. */
    fun pending(context: Context): Release? = if (enabled(context)) offered(context, readCache(context)?.first) else null

    fun skip(context: Context, version: String) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(SKIPPED, version).apply()
    }

    private fun offered(context: Context, release: Release?): Release? {
        if (release == null) return null
        val installed = BuildConfig.VERSION_NAME
        if (compareVersions(release.version, installed) <= 0 || isBeta(release.version) != isBeta(installed)) return null
        val skipped = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(SKIPPED, null)
        return if (skipped.isNullOrEmpty() || compareVersions(release.version, skipped) > 0) release else null
    }

    // The release list: the newest of this build's channel newer than it, with its APK and signature over HTTPS.
    internal fun selectNewer(json: String, installed: String): Release? {
        val beta = isBeta(installed)
        val asset = assetName(installed)
        val list = JSONArray(json)
        var best: Release? = null
        for (i in 0 until list.length()) {
            val release = list.optJSONObject(i) ?: continue
            if (release.optBoolean("draft")) continue
            val tag = release.optString("tag_name")
            val version = tag.removePrefix("v")
            if (isBeta(version) != beta) continue
            if (compareVersions(version, best?.version ?: installed) <= 0) continue
            var apk: String? = null
            var signature: String? = null
            var bytes = 0L
            val assets = release.optJSONArray("assets") ?: JSONArray()
            for (j in 0 until assets.length()) {
                val item = assets.optJSONObject(j) ?: continue
                when (item.optString("name")) {
                    asset -> {
                        apk = item.optString("browser_download_url")
                        bytes = item.optLong("size")
                    }
                    "$asset.sig" -> signature = item.optString("browser_download_url")
                }
            }
            if (!https(apk) || !https(signature)) continue
            best = Release(version, tag, plainNotes(release.optString("body")), apk!!, signature!!, bytes)
        }
        return best
    }

    private fun https(url: String?): Boolean = url != null && url.startsWith("https://")

    fun isBeta(version: String?): Boolean = version != null && version.contains("-beta.", ignoreCase = true)

    /** Updater.CompareVersions: major.minor.patch, a release after its pre-releases, then the pre-release's number. */
    fun compareVersions(a: String?, b: String?): Int {
        val x = versionKey(a)
        val y = versionKey(b)
        for (i in x.indices) if (x[i] != y[i]) return x[i].compareTo(y[i])
        return 0
    }

    private fun versionKey(version: String?): LongArray {
        val key = LongArray(5)
        if (version.isNullOrEmpty()) return key
        val parts = version.split('-', limit = 2)
        val numbers = parts[0].split('.')
        for (i in 0 until minOf(3, numbers.size)) key[i] = numbers[i].toLongOrNull() ?: 0L
        key[3] = if (parts.size == 1) 1L else 0L
        if (parts.size == 2) {
            val dot = parts[1].lastIndexOf('.')
            if (dot >= 0) key[4] = parts[1].substring(dot + 1).toLongOrNull() ?: 0L
        }
        return key
    }

    /** ReleaseNotes.Plain: GitHub's Markdown as plain text, at most [NOTES_LIMIT] characters. */
    fun plainNotes(markdown: String?): String {
        val link = Regex("""\[([^\]]+)\]\(([^)\s]+)\)""")
        val emphasis = Regex("""(\*\*|__)(.+?)\1|(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])""")
        val code = Regex("`([^`]*)`")
        val text = StringBuilder()
        var blank = true
        for (raw in (markdown ?: "").replace("\r\n", "\n").split('\n')) {
            var line = raw.trimEnd()
            while (line.startsWith(">")) line = line.substring(1).trimStart()
            line = line.replace(Regex("""^#{1,6}\s+"""), "")
            line = line.replace(Regex("""^(\s*)[*+]\s+"""), "$1- ")
            line = link.replace(line) { m -> if (m.groupValues[1] == m.groupValues[2]) m.groupValues[2] else "${m.groupValues[1]} (${m.groupValues[2]})" }
            line = emphasis.replace(line) { m -> if (m.groups[2] != null) m.groupValues[2] else m.groupValues[3] }
            line = code.replace(line, "$1")
            if (Regex("""^\s*([-*_]\s*){3,}$""").matches(line)) line = ""
            if (line.isEmpty()) {
                if (!blank) text.append('\n')
                blank = true
                continue
            }
            text.append(line).append('\n')
            blank = false
        }
        val plain = text.toString().trimEnd()
        if (plain.length <= NOTES_LIMIT) return plain
        val cut = plain.lastIndexOf('\n', NOTES_LIMIT)
        return (if (cut > NOTES_LIMIT / 2) plain.substring(0, cut) else plain.substring(0, NOTES_LIMIT)).trimEnd() + "\n…"
    }

    private fun request(url: String, from: Long = 0L): HttpURLConnection {
        val connection = URL(url).openConnection() as HttpURLConnection
        connection.connectTimeout = CONNECT_MS
        connection.readTimeout = READ_MS
        connection.instanceFollowRedirects = true
        connection.setRequestProperty("User-Agent", "Nebula")
        if (from > 0L) connection.setRequestProperty("Range", "bytes=$from-")
        return connection
    }

    private fun getString(url: String): String {
        val connection = request(url)
        connection.setRequestProperty("Accept", "application/vnd.github+json")
        try {
            if (connection.responseCode != HttpURLConnection.HTTP_OK) throw IOException("HTTP ${connection.responseCode}")
            val text = StringBuilder()
            val buffer = CharArray(16 * 1024)
            connection.inputStream.reader(Charsets.UTF_8).use { reader ->
                while (true) {
                    val read = reader.read(buffer)
                    if (read < 0) break
                    if (text.length + read > MAX_LISTING_CHARS) throw IOException("The release list is too large")
                    text.appendRange(buffer, 0, read)
                }
            }
            return text.toString()
        } finally {
            connection.disconnect()
        }
    }

    private fun cacheFile(context: Context) = File(context.filesDir, CACHE)

    private fun writeCache(context: Context, release: Release?, checkedMs: Long) {
        val o = JSONObject().put("checkedMs", checkedMs)
        if (release != null) {
            o.put("version", release.version).put("tag", release.tag).put("notes", release.notes)
                .put("apkUrl", release.apkUrl).put("signatureUrl", release.signatureUrl).put("apkBytes", release.apkBytes)
        }
        val file = cacheFile(context)
        val temp = File(file.path + ".tmp")
        temp.writeText(o.toString())
        if (!temp.renameTo(file)) temp.delete()
    }

    private fun readCache(context: Context): Pair<Release?, Long>? = runCatching {
        val o = JSONObject(cacheFile(context).readText())
        val release = if (o.has("version")) Release(
            o.getString("version"), o.optString("tag"), o.optString("notes"), o.getString("apkUrl"),
            o.getString("signatureUrl"), o.optLong("apkBytes"),
        ) else null
        release to o.optLong("checkedMs")
    }.getOrNull()

    /**
     * The release's APK in the app's cache, signed by the release key and this app at the release's version.
     * [progress] gets the bytes so far; [canceled] stops it between pieces. Throws [UpdateException].
     */
    fun download(context: Context, release: Release, progress: (Long) -> Unit, canceled: () -> Boolean): File {
        val folder = File(context.cacheDir, "update-${release.version}").apply { mkdirs() }
        // Older downloads go: only this release's folder is kept.
        context.cacheDir.listFiles()?.filter { it.isDirectory && it.name.startsWith("update-") && it != folder }?.forEach { it.deleteRecursively() }
        val apk = File(folder, "Nebula.apk")
        val partial = File(folder, "Nebula.apk.partial")
        val limit = if (release.apkBytes > 0L) release.apkBytes else MAX_APK_BYTES
        for (attempt in 0..1) {
            apk.delete()
            if (attempt > 0) partial.delete()
            val signature = try {
                fetchBytes(release.signatureUrl, MAX_SIGNATURE_BYTES)
            } catch (e: IOException) {
                throw UpdateException(UpdateFailure.Download, e.message ?: "signature")
            }
            fetchFile(context, release.apkUrl, partial, limit, progress, canceled)
            if (!partial.renameTo(apk)) throw UpdateException(UpdateFailure.Download, "rename")
            if (verifySignature(apk, signature)) {
                // The version is inside the signed bytes: an older signed APK under a newer tag is refused.
                val info = context.packageManager.getPackageArchiveInfo(apk.path, 0)
                if (info != null && info.packageName == context.packageName && info.versionName == release.version &&
                    compareVersions(release.version, BuildConfig.VERSION_NAME) > 0
                ) return apk
                apk.delete()
                throw UpdateException(UpdateFailure.Rejected, "not this app at ${release.version}")
            }
            apk.delete()
        }
        throw UpdateException(UpdateFailure.Rejected, "not signed by the release key")
    }

    private fun fetchBytes(url: String, limit: Long): ByteArray {
        val connection = request(url)
        try {
            if (connection.responseCode != HttpURLConnection.HTTP_OK) throw IOException("HTTP ${connection.responseCode}")
            if (connection.contentLengthLong > limit) throw IOException("too large")
            return readLimited(connection.inputStream, limit)
        } finally {
            connection.disconnect()
        }
    }

    private fun readLimited(input: InputStream, limit: Long): ByteArray = input.use {
        val out = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(16 * 1024)
        while (true) {
            val read = it.read(buffer)
            if (read < 0) break
            if (out.size() + read > limit) throw IOException("too large")
            out.write(buffer, 0, read)
        }
        out.toByteArray()
    }

    // The APK into [partial], resumed from what is there when the server answers the range (206 from that byte), from
    // the start otherwise; never more than [limit] bytes, and the room checked first.
    private fun fetchFile(context: Context, url: String, partial: File, limit: Long, progress: (Long) -> Unit, canceled: () -> Boolean) {
        var from = if (partial.isFile) partial.length() else 0L
        if (from > limit) {
            partial.delete()
            from = 0L
        }
        val connection = try {
            request(url, from).also { it.connect() }
        } catch (e: IOException) {
            throw UpdateException(UpdateFailure.Download, e.message ?: "connect")
        }
        try {
            val code = connection.responseCode
            val resumed = code == HttpURLConnection.HTTP_PARTIAL &&
                connection.getHeaderField("Content-Range")?.startsWith("bytes $from-") == true
            if (code == 416 && from > 0L) {
                // The partial file is already whole (or wrong): the signature check decides, and a retry starts over.
                return
            }
            if (!resumed && code != HttpURLConnection.HTTP_OK) throw UpdateException(UpdateFailure.Download, "HTTP $code")
            if (!resumed) from = 0L
            // What is still to come (a 206's length is the rest), with room to spare for the installer's copy.
            val remaining = connection.contentLengthLong
            if (remaining > 0L && context.cacheDir.usableSpace < remaining + (64L shl 20)) throw UpdateException(UpdateFailure.NoRoom, "room")
            var written = from
            java.io.FileOutputStream(partial, resumed).use { out ->
                connection.inputStream.use { input ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        if (canceled()) throw UpdateException(UpdateFailure.Download, "canceled")
                        val read = input.read(buffer)
                        if (read < 0) break
                        written += read
                        if (written > limit) throw UpdateException(UpdateFailure.Rejected, "larger than the release lists")
                        out.write(buffer, 0, read)
                        progress(written)
                    }
                }
            }
        } catch (e: UpdateException) {
            throw e
        } catch (e: IOException) {
            throw UpdateException(if (context.cacheDir.usableSpace < (16L shl 20)) UpdateFailure.NoRoom else UpdateFailure.Download, e.message ?: "read")
        } finally {
            connection.disconnect()
        }
    }

    /** RSA PKCS#1 v1.5 SHA-256 over the whole file, the signature file being its base64 (build-native-setup.ps1). */
    internal fun verifySignature(file: File, signatureFile: ByteArray): Boolean = runCatching {
        val signature = Base64.decode(String(signatureFile, Charsets.US_ASCII).trim(), Base64.DEFAULT)
        val key = KeyFactory.getInstance("RSA").generatePublic(
            RSAPublicKeySpec(
                BigInteger(1, Base64.decode(RELEASE_MODULUS, Base64.DEFAULT)),
                BigInteger(1, Base64.decode(RELEASE_EXPONENT, Base64.DEFAULT)),
            ),
        )
        val verifier = Signature.getInstance("SHA256withRSA")
        verifier.initVerify(key)
        file.inputStream().use { input ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                verifier.update(buffer, 0, read)
            }
        }
        verifier.verify(signature)
    }.getOrDefault(false)

    /** Android lets this app install packages (the player allows it once, in the system's settings). */
    fun canInstall(context: Context): Boolean = context.packageManager.canRequestPackageInstalls()

    /** The verified APK to Android's installer; it asks the player, then replaces this app. */
    fun install(context: Context, apk: File) {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
            setAppPackageName(context.packageName)
            setSize(apk.length())
            setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED)
        }
        val id = installer.createSession(params)
        installer.openSession(id).use { session ->
            session.openWrite("Nebula.apk", 0, apk.length()).use { out ->
                apk.inputStream().use { it.copyTo(out, 64 * 1024) }
                session.fsync(out)
            }
            val status = PendingIntent.getBroadcast(
                context, id, Intent(context, UpdateInstallReceiver::class.java),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
            )
            session.commit(status.intentSender)
        }
    }
}

/** Android's installer's answer: its confirmation screen when it waits on the player (the only step it asks for). */
class UpdateInstallReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE) != PackageInstaller.STATUS_PENDING_USER_ACTION) return
        val confirm = IntentCompat.getParcelableExtra(intent, Intent.EXTRA_INTENT, Intent::class.java) ?: return
        context.startActivity(confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }
}
