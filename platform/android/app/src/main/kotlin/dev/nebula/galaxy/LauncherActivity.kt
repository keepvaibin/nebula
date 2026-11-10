// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy

import android.content.Intent
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.core.content.FileProvider
import androidx.core.content.IntentCompat
import dev.nebula.galaxy.ui.NebulaApp
import dev.nebula.galaxy.ui.NebulaTheme

/** Nebula's front door: setup from the player's disc image, then play, settings and the rest. */
class LauncherActivity : ComponentActivity() {
    private val model: LauncherModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        // The sky is always dark: light system bar icons, whatever the system's theme.
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
        )
        super.onCreate(savedInstanceState)
        if (BuildConfig.DEBUG && intent?.getBooleanExtra("nebula.profile", false) == true) {
            startActivity(Intent(this, GameActivity::class.java).putExtra("nebula.profile", true))
            finish()
            return
        }
        // The app's icon while a game runs: some home screens start another launcher over it. The task as it was
        // (the game in front) is what the player wants back.
        if (!isTaskRoot && intent?.action == Intent.ACTION_MAIN && intent.hasCategory(Intent.CATEGORY_LAUNCHER)) {
            finish()
            return
        }
        // A DolphinBar plugged in (the manifest's USB filter): its permission is granted by now. A running game takes
        // the bar itself (WiimoteUsb), so the game stays in front.
        if (intent?.action == UsbManager.ACTION_USB_DEVICE_ATTACHED && (!isTaskRoot || SaveTransfer.gameRunning(this))) {
            finish()
            return
        }
        setContent {
            NebulaTheme {
                NebulaApp(model, onShareLog = ::shareLog)
            }
        }
        if (savedInstanceState == null) {
            opened(intent)
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        opened(intent)
    }

    /** "Open with Nebula" and Share to Nebula (the manifest's filters): the files go where a drop would. */
    private fun opened(intent: Intent?) {
        val uris: List<Uri> = when (intent?.action) {
            Intent.ACTION_VIEW -> listOfNotNull(intent.data)
            Intent.ACTION_SEND -> listOfNotNull(IntentCompat.getParcelableExtra(intent, Intent.EXTRA_STREAM, Uri::class.java))
            Intent.ACTION_SEND_MULTIPLE ->
                IntentCompat.getParcelableArrayListExtra(intent, Intent.EXTRA_STREAM, Uri::class.java).orEmpty()
            else -> emptyList()
        }
        if (uris.isNotEmpty()) {
            model.dropped(uris)
        }
    }

    override fun onResume() {
        super.onResume()
        if (!isFinishing) {
            model.resumed()
        }
    }

    private fun shareLog() {
        val log = GameFiles.log(this)
        if (!log.isFile) return
        val uri = FileProvider.getUriForFile(this, "$packageName.files", log)
        val send = Intent(Intent.ACTION_SEND)
            .setType("text/plain")
            .putExtra(Intent.EXTRA_STREAM, uri)
            .putExtra(Intent.EXTRA_SUBJECT, "Nebula game log")
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        startActivity(Intent.createChooser(send, "Share the game log"))
    }
}
