// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.IBinder
import android.os.PowerManager
import android.os.SystemClock
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Setup's progress, shared by the service doing it and the screen showing it. */
sealed interface ImportState {
    data object Idle : ImportState
    data class Running(val done: Long, val total: Long, val bytesPerSecond: Double) : ImportState
    data class Failed(val message: String) : ImportState
    data object Cancelled : ImportState
    data object Finished : ImportState
}

object Imports {
    private val mutable = MutableStateFlow<ImportState>(ImportState.Idle)
    val state: StateFlow<ImportState> = mutable.asStateFlow()

    @Volatile
    internal var cancelRequested = false

    internal fun publish(state: ImportState) {
        mutable.value = state
    }

    fun acknowledge() {
        if (mutable.value !is ImportState.Running) mutable.value = ImportState.Idle
    }

    fun start(context: Context, uri: Uri, identity: DiscIdentity, sourceName: String) {
        cancelRequested = false
        mutable.value = ImportState.Running(0, 0, 0.0)
        val intent = Intent(context, ImportService::class.java)
            .setData(uri)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            .putExtra(ImportService.EXTRA_GAME_ID, identity.gameId)
            .putExtra(ImportService.EXTRA_TITLE, identity.title)
            .putExtra(ImportService.EXTRA_FORMAT, identity.format)
            .putExtra(ImportService.EXTRA_SOURCE, sourceName)
        context.startForegroundService(intent)
    }

    fun cancel() {
        cancelRequested = true
    }
}

/** Copies the game from the player's disc image into game.pak, in the foreground, with its progress as a notification. */
class ImportService : Service() {
    companion object {
        const val EXTRA_GAME_ID = "gameId"
        const val EXTRA_TITLE = "title"
        const val EXTRA_FORMAT = "format"
        const val EXTRA_SOURCE = "source"
        private const val CHANNEL = "setup"
        private const val NOTIFICATION = 1
    }

    @Volatile
    private var worker: Thread? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val uri = intent?.data
        if (uri == null || worker != null) {
            if (worker == null) stopSelf(startId)
            return START_NOT_STICKY
        }
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL, getString(R.string.import_channel), NotificationManager.IMPORTANCE_LOW)
        )
        startForeground(NOTIFICATION, notification(0, 0), ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        val gameId = intent.getStringExtra(EXTRA_GAME_ID).orEmpty()
        val title = intent.getStringExtra(EXTRA_TITLE).orEmpty()
        val format = intent.getStringExtra(EXTRA_FORMAT).orEmpty()
        val source = intent.getStringExtra(EXTRA_SOURCE).orEmpty()
        worker = Thread({ run(uri, gameId, title, format, source) }, "Nebula setup").apply { start() }
        return START_NOT_STICKY
    }

    private fun run(uri: Uri, gameId: String, title: String, format: String, source: String) {
        val wake = getSystemService(PowerManager::class.java)
            .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Nebula:setup")
        wake.acquire(60 * 60 * 1000L)
        val manager = getSystemService(NotificationManager::class.java)
        try {
            val output = GameFiles.pak(this)
            Storage.removeGame(this)
            val descriptor = contentResolver.openFileDescriptor(uri, "r")
                ?: throw IllegalStateException("the disc image could not be opened")
            val started = SystemClock.elapsedRealtime()
            var lastScreen = 0L
            var lastNotice = 0L
            val error = descriptor.use { file ->
                Importer.build(file.fd, output.path) { done, total ->
                    val now = SystemClock.elapsedRealtime()
                    if (now - lastScreen >= 100 || done == total) {
                        lastScreen = now
                        val seconds = (now - started) / 1000.0
                        Imports.publish(ImportState.Running(done, total, if (seconds > 0.5) done / seconds else 0.0))
                    }
                    if (now - lastNotice >= 1000) {
                        lastNotice = now
                        manager.notify(NOTIFICATION, notification(done, total))
                    }
                    !Imports.cancelRequested
                }
            }
            when {
                error == null -> {
                    Install(gameId, title, format, source, output.length(), System.currentTimeMillis()).write(this)
                    Imports.publish(ImportState.Finished)
                }
                Imports.cancelRequested -> Imports.publish(ImportState.Cancelled)
                else -> Imports.publish(ImportState.Failed(sentence(error)))
            }
        } catch (failure: Exception) {
            Imports.publish(ImportState.Failed(sentence(failure.message ?: failure.toString())))
        } finally {
            if (wake.isHeld) wake.release()
            worker = null
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
    }

    private fun sentence(text: String) = text.replaceFirstChar { it.uppercase() }.let { if (it.endsWith(".")) it else "$it." }

    private fun notification(done: Long, total: Long): Notification {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, LauncherActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE,
        )
        val percent = if (total > 0) (done * 100 / total).toInt() else 0
        return Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(getString(R.string.import_notification_title))
            .setContentText(if (total > 0) "$percent% · ${formatBytes(done)} of ${formatBytes(total)}" else "Reading the disc image")
            .setProgress(100, percent, total == 0L)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(open)
            .setCategory(Notification.CATEGORY_PROGRESS)
            .build()
    }
}
