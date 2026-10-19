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

enum class Screen { Welcome, Disc, Import, Home, Settings, About, Licenses }

class LauncherModel(application: Application) : AndroidViewModel(application) {
    private val app = application

    var install by mutableStateOf(Install.read(app))
        private set
    var screen by mutableStateOf(if (install != null) Screen.Home else Screen.Welcome)
    var disc by mutableStateOf<DiscPick?>(null)
        private set
    var graphics by mutableStateOf(GraphicsSettings.read(app))
        private set
    var controls by mutableStateOf(ControlsSettings.read(app))
        private set
    var gameExit by mutableStateOf<GameExit?>(null)
    var freeBytes by mutableStateOf(Storage.freeBytes(app))
        private set
    var saveBytes by mutableStateOf(0L)
        private set
    var license by mutableStateOf<License?>(null)

    val imports = Imports.state

    init {
        if (Imports.state.value is ImportState.Running) screen = Screen.Import
    }

    fun back(): Boolean {
        screen = when (screen) {
            Screen.Disc -> if (install != null) Screen.Settings else Screen.Welcome
            Screen.Settings, Screen.About -> Screen.Home
            Screen.Licenses -> Screen.About
            else -> return false
        }
        return true
    }

    /** The launcher came to the front: the game may have just ended. */
    fun resumed() {
        install = Install.read(app)
        freeBytes = Storage.freeBytes(app)
        viewModelScope.launch(Dispatchers.IO) {
            val saves = Storage.saveBytes(app)
            withContext(Dispatchers.Main) { saveBytes = saves }
        }
        if (install == null && screen == Screen.Home) screen = Screen.Welcome
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

    val enoughSpace: Boolean
        get() = freeBytes + (install?.bytes ?: 0L) >= Storage.GAME_BYTES + Storage.MARGIN_BYTES

    fun startImport() {
        val pick = disc ?: return
        val identity = pick.identity ?: return
        Imports.start(app, pick.uri, identity, pick.name)
        screen = Screen.Import
    }

    fun importDone(play: Context?) {
        Imports.acknowledge()
        install = Install.read(app)
        disc = null
        screen = if (install != null) Screen.Home else Screen.Welcome
        if (play != null && install != null) launchGame(play)
    }

    fun importRetry() {
        Imports.acknowledge()
        screen = Screen.Disc
    }

    fun updateGraphics(value: GraphicsSettings) {
        graphics = value
        value.write(app)
    }

    fun updateControls(value: ControlsSettings) {
        controls = value
        value.write(app)
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
