// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.app.ActivityManager
import android.app.Application
import android.app.ApplicationExitInfo
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

/** What the importer found in a disc image (crates/nebula-import identify). */
data class DiscIdentity(
    val supported: Boolean,
    val gameId: String,
    val title: String,
    val format: String,
    val message: String,
) {
    companion object {
        fun parse(json: String): DiscIdentity {
            val o = JSONObject(json)
            return DiscIdentity(
                supported = o.optBoolean("supported"),
                gameId = o.optString("gameId"),
                title = o.optString("title"),
                format = o.optString("format"),
                message = o.optString("message"),
            )
        }
    }
}

/** The chosen disc image: its name and size, and once read, what it is. */
data class DiscPick(val uri: Uri, val name: String, val bytes: Long, val identity: DiscIdentity? = null)

/** How the last run of the game ended, when it did not end by the player's choice. */
data class GameExit(val title: String, val message: String, val hasLog: Boolean)

enum class Screen { Welcome, Disc, Import, Home, Settings, MotionTest, About, Licenses }

class LauncherModel(application: Application) : AndroidViewModel(application) {
    private val app = application

    var install by mutableStateOf(Install.read(app))
        private set

    /** The game set up by an earlier Nebula needs setting up again (Install.outdated). */
    var outdated by mutableStateOf(Install.outdated(app))
        private set
    var screen by mutableStateOf(if (install != null) Screen.Home else Screen.Welcome)
    var disc by mutableStateOf<DiscPick?>(null)
        private set
    var gameExit by mutableStateOf<GameExit?>(null)
    var freeBytes by mutableStateOf(Storage.freeBytes(app))
        private set
    var saveBytes by mutableStateOf(0L)
        private set
    var license by mutableStateOf<License?>(null)

    /** The next run records diagnostics in its log, to share after it. */
    var recordNextRun by mutableStateOf(GameFiles.controls(application).getBoolean(GameFiles.RECORD_NEXT_RUN, false))
        private set

    /** The mod import under way (its share done, 0 to 1), or null. */
    var modProgress by mutableStateOf<Float?>(null)
        private set

    /** How the last mod import ended, shown until the player closes it. */
    var modResult by mutableStateOf<ModImportResult?>(null)

    // Mods waiting for the one under way (several dropped at once), and how those before them ended.
    private val modQueue = ArrayDeque<Uri>()
    private val modResults = ArrayList<Pair<String, ModImportResult>>()

    /** A mod or texture pack from a file the player picked, in the background; the game shows it under Mods. */
    fun importMod(uri: Uri) {
        if (modProgress != null) {
            modQueue.addLast(uri)
            return
        }
        modProgress = 0f
        val main = android.os.Handler(android.os.Looper.getMainLooper())
        Thread {
            val result = runCatching {
                ModImport.import(app, uri) { share -> main.post { if (modProgress != null) modProgress = share } }
            }.getOrElse { ModImportResult(false, "", "", "", it.message ?: "That file could not be read") }
            val file = DropKind.nameAndSize(app, uri).first
            main.post {
                modProgress = null
                modResults += file to result
                val next = modQueue.removeFirstOrNull()
                if (next != null) {
                    importMod(next)
                } else {
                    modResult = if (modResults.size == 1) modResults[0].second else together(modResults)
                    modResults.clear()
                }
            }
        }.start()
    }

    // Several imports as one answer: the mods made ready, then each file that was not, with its reason.
    private fun together(results: List<Pair<String, ModImportResult>>): ModImportResult {
        val ready = results.filter { it.second.ok }.map { it.second.name }
        val failed = results.filter { !it.second.ok }.map { "${it.first}: ${it.second.message}" }
        val title = when {
            failed.isEmpty() -> "${ready.size} mods are ready"
            ready.isEmpty() -> "Those files could not be imported"
            else -> "${ready.size} of ${results.size} mods are ready"
        }
        val parts = ArrayList<String>()
        if (ready.isNotEmpty()) parts += ready.joinToString(", ") + ". Turn them on under Mods in the game's settings."
        if (failed.isNotEmpty()) parts += failed.joinToString("\n")
        return ModImportResult(failed.isEmpty(), "", "", "", parts.joinToString("\n\n"), title)
    }

    /**
     * Files dragged onto the launcher from another app, each to the importer its picker would use (DropKind): a disc
     * image to setup (the first; setup takes one), saves to Import saves (the first), Mii files to Import Miis, the
     * rest to the mod importer one after another.
     */
    fun dropped(uris: List<Uri>) {
        if (uris.isEmpty() || screen == Screen.Import) return
        viewModelScope.launch(Dispatchers.IO) {
            val kinds = uris.groupBy { runCatching { DropKind.of(app, it) }.getOrDefault(DropKind.Mod) }
            withContext(Dispatchers.Main) {
                kinds[DropKind.Disc]?.firstOrNull()?.let {
                    choose(it)
                    return@withContext
                }
                if (install == null) {
                    transferResult = TransferResult(false, "Set up the game first", "Drop your disc image here to start.")
                    return@withContext
                }
                kinds[DropKind.Saves]?.firstOrNull()?.let { importSaves(it) }
                kinds[DropKind.Miis]?.let { importMiis(it) }
                kinds[DropKind.Mod]?.forEach { importMod(it) }
            }
        }
    }

    /** A save export or import under way. */
    var saveBusy by mutableStateOf(false)
        private set

    /** How the last save or Mii move ended, shown until the player closes it. */
    var transferResult by mutableStateOf<TransferResult?>(null)

    /** A Mii import under way. */
    var miiBusy by mutableStateOf(false)
        private set

    /** The Miis the game's icon picker shows (null before the game has made its Mii file). */
    var miiCount by mutableStateOf<Int?>(null)
        private set

    fun refreshMiis() {
        viewModelScope.launch(Dispatchers.IO) {
            val count = runCatching { MiiImport.count(app) }.getOrNull()
            withContext(Dispatchers.Main) { miiCount = count }
        }
    }

    /** The Miis in the files at [uris], for the game to add at its next start. */
    fun importMiis(uris: List<Uri>) {
        if (miiBusy || uris.isEmpty()) return
        miiBusy = true
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching { MiiImport.import(app, uris) }
                .getOrElse { TransferResult(false, "The Miis could not be imported", it.message ?: "Unknown error.") }
            withContext(Dispatchers.Main) {
                miiBusy = false
                transferResult = result
            }
        }
    }

    /** Every save as one .zip at [uri] (the place the player chose). */
    fun exportSaves(uri: Uri) = transferSaves { SaveTransfer.export(app, uri) }

    /** The saves in the file at [uri] in place of the game's (the saves in place are backed up first). */
    fun importSaves(uri: Uri) = transferSaves { SaveTransfer.import(app, uri, install?.gameId.orEmpty()) }

    private fun transferSaves(work: () -> TransferResult) {
        if (saveBusy) return
        saveBusy = true
        viewModelScope.launch(Dispatchers.IO) {
            val result = runCatching(work).getOrElse { TransferResult(false, "The saves could not be moved", it.message ?: "Unknown error.") }
            val saves = Storage.saveBytes(app)
            withContext(Dispatchers.Main) {
                saveBusy = false
                saveBytes = saves
                transferResult = result
            }
        }
    }

    fun updateRecordNextRun(value: Boolean) {
        recordNextRun = value
        GameFiles.controls(app).edit().putBoolean(GameFiles.RECORD_NEXT_RUN, value).commit()
    }

    val imports = Imports.state

    init {
        if (Imports.state.value is ImportState.Running) screen = Screen.Import
    }

    /** A newer release to offer (Updates), over the launcher until the player answers. */
    var update by mutableStateOf<Release?>(null)
        private set

    /** The update's download and hand-over under way: its share done (0 to 1), or null. */
    var updateProgress by mutableStateOf<Float?>(null)
        private set

    /** Why the last Update did not reach Android's installer, shown in the prompt. */
    var updateFailure by mutableStateOf<UpdateFailure?>(null)
        private set

    /** Android lets no app install packages until the player allows it for Nebula. */
    var updateNeedsPermission by mutableStateOf(false)
        private set

    /** The check at start (Settings, Updates); on unless the player turns it off. */
    var updatesAtStart by mutableStateOf(Updates.enabled(application))
        private set

    private val updateCanceled = java.util.concurrent.atomic.AtomicBoolean(false)

    // Not now (or Skip) answered: no other prompt in this run.
    private var updateAnswered = false

    init {
        // The kept answer at once, the check's when it comes; it never holds up Play (a late answer is kept and
        // offered at the next start).
        offerUpdate(Updates.pending(app))
        val main = android.os.Handler(android.os.Looper.getMainLooper())
        Updates.begin(app) { release -> main.post { offerUpdate(release) } }
    }

    private fun offerUpdate(release: Release?) {
        // Installing replaces the app and ends a game running in its process: never offered meanwhile.
        if (release == null || updateAnswered || updateProgress != null || SaveTransfer.gameRunning(app)) return
        update = release
    }

    fun updateLater() {
        updateAnswered = true
        update = null
        updateFailure = null
        updateNeedsPermission = false
    }

    fun updateSkip() {
        update?.let { Updates.skip(app, it.version) }
        updateLater()
    }

    fun updateCancel() {
        updateCanceled.set(true)
    }

    fun updateNow() {
        val release = update ?: return
        if (updateProgress != null || SaveTransfer.gameRunning(app)) return
        if (!Updates.canInstall(app)) {
            updateNeedsPermission = true
            return
        }
        updateNeedsPermission = false
        updateFailure = null
        updateCanceled.set(false)
        updateProgress = 0f
        val main = android.os.Handler(android.os.Looper.getMainLooper())
        Thread({
            var shown = -1
            val result = runCatching {
                val apk = Updates.download(app, release, { bytes ->
                    // A step per percent reaches the screen, not one per piece read.
                    val percent = if (release.apkBytes > 0L) (bytes * 100L / release.apkBytes).toInt().coerceIn(0, 100) else 0
                    if (percent != shown) {
                        shown = percent
                        main.post { if (updateProgress != null) updateProgress = percent / 100f }
                    }
                }, updateCanceled::get)
                Updates.install(app, apk)
            }
            main.post {
                updateProgress = null
                result.onSuccess {
                    // Android's installer asks the player now (UpdateInstallReceiver) and replaces the app.
                    updateAnswered = true
                    update = null
                }
                result.onFailure { error ->
                    if (!updateCanceled.get()) updateFailure = (error as? UpdateException)?.failure ?: UpdateFailure.Download
                }
            }
        }, "nebula-update").start()
    }

    /** The system's "install unknown apps" switch for Nebula. */
    fun openInstallPermission(context: Context) {
        context.startActivity(
            Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${app.packageName}"))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        )
    }

    fun updateUpdatesAtStart(on: Boolean) {
        updatesAtStart = on
        Updates.setEnabled(app, on)
        if (!on && updateProgress == null) update = null
    }

    fun back(): Boolean {
        screen = when (screen) {
            Screen.Disc -> if (install != null) Screen.Settings else Screen.Welcome
            Screen.Settings, Screen.About -> Screen.Home
            Screen.Licenses -> Screen.About
            Screen.MotionTest -> Screen.Settings
            else -> return false
        }
        return true
    }

    /** The launcher came to the front: the game may have just ended. */
    fun resumed() {
        install = Install.read(app)
        outdated = Install.outdated(app)
        freeBytes = Storage.freeBytes(app)
        recordNextRun = GameFiles.controls(app).getBoolean(GameFiles.RECORD_NEXT_RUN, false)
        viewModelScope.launch(Dispatchers.IO) {
            val saves = Storage.saveBytes(app)
            withContext(Dispatchers.Main) { saveBytes = saves }
        }
        if (install == null && screen == Screen.Home) screen = Screen.Welcome
        // Back from the system's settings with installing allowed: the prompt offers Update again.
        if (updateNeedsPermission && Updates.canInstall(app)) updateNeedsPermission = false
        checkLastRun()
    }

    fun choose(uri: Uri) {
        val resolver = app.contentResolver
        var name = uri.lastPathSegment ?: "disc image"
        var bytes = -1L
        runCatching {
            resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { c ->
                if (c.moveToFirst()) {
                    name = c.getString(0) ?: name
                    if (!c.isNull(1)) bytes = c.getLong(1)
                }
            }
        }
        disc = DiscPick(uri, name, bytes)
        freeBytes = Storage.freeBytes(app)
        screen = Screen.Disc
        viewModelScope.launch(Dispatchers.IO) {
            val identity = runCatching {
                resolver.openFileDescriptor(uri, "r")!!.use { DiscIdentity.parse(Importer.identify(it.fd)) }
            }.getOrElse { DiscIdentity(false, "", "", "", "The disc image could not be read: ${it.message}") }
            withContext(Dispatchers.Main) {
                if (disc?.uri == uri) disc = disc?.copy(identity = identity)
            }
        }
    }

    // The game in place is replaced, so its room counts; an outdated one's too (it is removed before the import).
    val roomBytes: Long
        get() = freeBytes + (install?.bytes ?: if (outdated) GameFiles.pak(app).length() else 0L)

    val enoughSpace: Boolean
        get() = roomBytes >= Storage.GAME_BYTES + Storage.MARGIN_BYTES

    fun startImport() {
        val pick = disc ?: return
        val identity = pick.identity ?: return
        // A store the game no longer reads only takes room the new one needs.
        if (outdated) {
            Storage.removeGame(app)
            outdated = false
        }
        Imports.start(app, pick.uri, identity, pick.name)
        screen = Screen.Import
    }

    fun importDone(play: Context?) {
        Imports.acknowledge()
        install = Install.read(app)
        outdated = Install.outdated(app)
        disc = null
        screen = if (install != null) Screen.Home else Screen.Welcome
        if (play != null && install != null) launchGame(play)
    }

    fun importRetry() {
        Imports.acknowledge()
        screen = Screen.Disc
    }

    fun removeGame() {
        Storage.removeGame(app)
        install = null
        freeBytes = Storage.freeBytes(app)
        screen = Screen.Welcome
    }

    /** From the launcher's activity, so the game joins its task and ends back here. */
    fun launchGame(activity: Context) {
        GameFiles.controls(app).edit().putLong(LAUNCHED_AT, System.currentTimeMillis()).commit()
        activity.startActivity(Intent(activity, GameActivity::class.java))
    }

    private fun checkLastRun() {
        val prefs = GameFiles.controls(app)
        val launchedAt = prefs.getLong(LAUNCHED_AT, 0L)
        if (launchedAt == 0L) return
        prefs.edit().remove(LAUNCHED_AT).apply()
        val hasLog = GameFiles.log(app).isFile
        val error = GameFiles.lastError(app)
        if (error.isFile) {
            gameExit = GameExit("The game could not continue", error.readText().trim().ifEmpty { "Unknown error." }, hasLog)
            error.delete()
            return
        }
        val activities = app.getSystemService(ActivityManager::class.java)
        val exit = activities.getHistoricalProcessExitReasons(app.packageName, 0, 8)
            .firstOrNull { it.processName.endsWith(":game") && it.timestamp >= launchedAt } ?: return
        val message = when (exit.reason) {
            ApplicationExitInfo.REASON_CRASH, ApplicationExitInfo.REASON_CRASH_NATIVE ->
                "The game stopped unexpectedly. The log of that run can help find out why."
            ApplicationExitInfo.REASON_ANR -> "The game stopped responding and was closed."
            ApplicationExitInfo.REASON_LOW_MEMORY ->
                "Android closed the game to free memory. Closing other apps before playing can help."
            else -> return
        }
        gameExit = GameExit("The game closed", message, hasLog)
    }

    companion object {
        private const val LAUNCHED_AT = "launchedAt"
    }
}
