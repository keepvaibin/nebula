// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy.ui

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.hardware.input.InputManager
import android.net.Uri
import android.view.InputDevice
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.core.tween
import androidx.compose.animation.scaleIn
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.draganddrop.dragAndDropTarget
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.AutoAwesome
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Description
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Download
import androidx.compose.material.icons.rounded.Face
import androidx.compose.material.icons.rounded.Upload
import androidx.compose.material.icons.rounded.Share
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draganddrop.DragAndDropEvent
import androidx.compose.ui.draganddrop.DragAndDropTarget
import androidx.compose.ui.draganddrop.toAndroidDragEvent
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.nebula.galaxy.BuildConfig
import dev.nebula.galaxy.DisplayRate
import dev.nebula.galaxy.ImportState
import dev.nebula.galaxy.Imports
import dev.nebula.galaxy.LauncherModel
import dev.nebula.galaxy.R
import dev.nebula.galaxy.UpdateFailure
import dev.nebula.galaxy.Screen
import dev.nebula.galaxy.Storage
import dev.nebula.galaxy.formatBytes
import dev.nebula.galaxy.licenses
import kotlin.math.roundToInt

@Composable
fun NebulaApp(model: LauncherModel, onShareLog: () -> Unit) {
    val context = LocalContext.current
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) model.choose(uri)
    }
    val choose = { pick.launch(arrayOf("*/*")) }
    val pickMod = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) model.importMod(uri)
    }
    val chooseMod = { pickMod.launch(arrayOf("*/*")) }
    val exportTo = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri: Uri? ->
        if (uri != null) model.exportSaves(uri)
    }
    val importFrom = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) model.importSaves(uri)
    }
    val miisFrom = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris: List<Uri> ->
        model.importMiis(uris)
    }
    val saves = SaveActions(
        export = { exportTo.launch("Nebula saves ${java.text.SimpleDateFormat("yyyy-MM-dd", java.util.Locale.ROOT).format(java.util.Date())}.zip") },
        import = { importFrom.launch(arrayOf("*/*")) },
        miis = { miisFrom.launch(arrayOf("*/*")) },
    )
    BackHandler(enabled = model.screen in setOf(Screen.Disc, Screen.Settings, Screen.MotionTest, Screen.About, Screen.Licenses)) {
        model.back()
    }
    // Files dragged in from another app (split screen, a desktop mode, a Chromebook): each to its importer.
    var dropHover by remember { mutableStateOf(false) }
    val dropTarget = remember(model) {
        object : DragAndDropTarget {
            override fun onDrop(event: DragAndDropEvent): Boolean {
                dropHover = false
                val drag = event.toAndroidDragEvent()
                // The other app's files are lent for this drop, for as long as the launcher lives.
                (context as? Activity)?.requestDragAndDropPermissions(drag)
                val clip = drag.clipData ?: return false
                val uris = (0 until clip.itemCount).mapNotNull { clip.getItemAt(it).uri }
                if (uris.isEmpty()) return false
                model.dropped(uris)
                return true
            }

            override fun onEntered(event: DragAndDropEvent) {
                dropHover = true
            }

            override fun onExited(event: DragAndDropEvent) {
                dropHover = false
            }

            override fun onEnded(event: DragAndDropEvent) {
                dropHover = false
            }
        }
    }
    Box(Modifier.fillMaxSize().dragAndDropTarget(shouldStartDragAndDrop = { model.screen != Screen.Import }, target = dropTarget)) {
        Starfield()
        AnimatedContent(
            targetState = model.screen,
            transitionSpec = { (fadeIn(tween(260)) + scaleIn(tween(260), initialScale = 0.98f)) togetherWith fadeOut(tween(160)) },
            label = "screen",
            modifier = Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing),
        ) { screen ->
            when (screen) {
                Screen.Welcome -> WelcomeScreen(outdated = model.outdated, onChoose = choose)
                Screen.Disc -> DiscScreen(model, onChoose = choose)
                Screen.Import -> ImportScreen(model, onPlay = { model.importDone(context) })
                Screen.Home -> HomeScreen(model, onPlay = { model.launchGame(context) })
                Screen.Settings -> SettingsScreen(model, onChoose = choose, onChooseMod = chooseMod, saves = saves, onShareLog = onShareLog)
                Screen.MotionTest -> MotionTestScreen { model.back() }
                Screen.About -> AboutScreen(model)
                Screen.Licenses -> LicenseScreen(model)
            }
        }
        if (dropHover) DropHint(setUp = model.screen != Screen.Welcome && model.screen != Screen.Disc)
    }
    model.modResult?.let { result ->
        AlertDialog(
            onDismissRequest = { model.modResult = null },
            icon = {
                if (result.ok) Icon(Icons.Rounded.CheckCircle, contentDescription = null, tint = Night.Lavender)
                else Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = Night.Error)
            },
            title = {
                Text(
                    result.title ?: if (result.already) stringResource(R.string.nebula_mod_already, result.name)
                    else if (result.ok) stringResource(R.string.nebula_mod_ready, result.name)
                    else stringResource(R.string.nebula_mod_failed),
                )
            },
            text = {
                Text(
                    if (result.title != null) result.message
                    else if (result.already) stringResource(R.string.nebula_mod_already_detail)
                    else if (result.ok) stringResource(R.string.nebula_mod_ready_detail) +
                        (if (result.message.contains(", BUT ")) "\n\n${result.message}" else "")
                    else result.message,
                )
            },
            confirmButton = { TextButton(onClick = { model.modResult = null }) { Text(stringResource(R.string.nebula_ok)) } },
        )
    }
    model.transferResult?.let { result ->
        AlertDialog(
            onDismissRequest = { model.transferResult = null },
            icon = {
                if (result.ok) Icon(Icons.Rounded.CheckCircle, contentDescription = null, tint = Night.Lavender)
                else Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = Night.Error)
            },
            title = { Text(result.title) },
            text = { Text(result.message) },
            confirmButton = { TextButton(onClick = { model.transferResult = null }) { Text(stringResource(R.string.nebula_ok)) } },
        )
    }
    model.gameExit?.let { exit ->
        AlertDialog(
            onDismissRequest = { model.gameExit = null },
            icon = { Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = Night.Error) },
            title = { Text(exit.title) },
            text = { Text(exit.message) },
            confirmButton = { TextButton(onClick = { model.gameExit = null }) { Text(stringResource(R.string.nebula_ok)) } },
            dismissButton = if (exit.hasLog) {
                { TextButton(onClick = { onShareLog(); model.gameExit = null }) { Text(stringResource(R.string.nebula_share_the_log)) } }
            } else null,
        )
    }
    model.update?.let { release -> UpdateDialog(model, release) }
}

/** A newer release (Updates): its notes and size, then Update, Skip this version or Not now; the download's progress. */
@Composable
private fun UpdateDialog(model: LauncherModel, release: dev.nebula.galaxy.Release) {
    val context = LocalContext.current
    val progress = model.updateProgress
    AlertDialog(
        onDismissRequest = { if (progress == null) model.updateLater() },
        icon = { Icon(Icons.Rounded.Download, contentDescription = null, tint = Night.Lavender) },
        title = { Text(stringResource(R.string.nebula_update_available, release.version)) },
        text = {
            Column {
                if (progress != null) {
                    Text(stringResource(R.string.nebula_update_downloading, release.version))
                    Gap(12.dp)
                    LinearProgressIndicator(progress = { progress }, modifier = Modifier.fillMaxWidth())
                } else {
                    val failure = model.updateFailure
                    if (model.updateNeedsPermission) {
                        Text(stringResource(R.string.nebula_update_allow), color = Night.Lavender)
                        Gap(12.dp)
                    } else if (failure != null) {
                        val reason = when (failure) {
                            UpdateFailure.Download -> R.string.nebula_update_failed
                            UpdateFailure.Rejected -> R.string.nebula_update_rejected
                            UpdateFailure.NoRoom -> R.string.nebula_update_no_room
                            UpdateFailure.Declined -> R.string.nebula_update_declined
                            UpdateFailure.Refused -> R.string.nebula_update_refused
                            UpdateFailure.Blocked -> R.string.nebula_update_blocked
                            UpdateFailure.InstallFailed -> R.string.nebula_update_install_failed
                        }
                        Text(stringResource(reason), color = Night.Error)
                        Gap(12.dp)
                    }
                    if (release.notes.isNotEmpty()) {
                        Column(Modifier.heightIn(max = 320.dp).verticalScroll(rememberScrollState())) {
                            Text(release.notes, style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                    if (release.apkBytes > 0L) {
                        Gap(12.dp)
                        Text(
                            stringResource(R.string.nebula_update_size, formatBytes(release.apkBytes)),
                            style = MaterialTheme.typography.bodySmall,
                            color = Night.TextDim,
                        )
                    }
                }
            }
        },
        confirmButton = {
            when {
                progress != null -> TextButton(onClick = model::updateCancel) { Text(stringResource(R.string.nebula_update_cancel)) }
                model.updateNeedsPermission -> TextButton(onClick = { model.openInstallPermission(context) }) {
                    Text(stringResource(R.string.nebula_update_open_settings))
                }
                else -> TextButton(onClick = model::updateNow) { Text(stringResource(R.string.nebula_update_update)) }
            }
        },
        dismissButton = if (progress == null) {
            {
                Row {
                    TextButton(onClick = model::updateSkip) { Text(stringResource(R.string.nebula_update_skip)) }
                    TextButton(onClick = model::updateLater) { Text(stringResource(R.string.nebula_update_not_now)) }
                }
            }
        } else null,
    )
}

/** Over the launcher while a file is dragged across it: what letting go does. */
@Composable
private fun DropHint(setUp: Boolean) {
    Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.6f)), contentAlignment = Alignment.Center) {
        GlassCard(Modifier.widthIn(max = 420.dp).padding(24.dp)) {
            Icon(Icons.Rounded.Download, contentDescription = null, tint = Night.Gold, modifier = Modifier.size(40.dp))
            Gap(12.dp)
            Text(stringResource(R.string.nebula_drop_to_import), style = MaterialTheme.typography.titleLarge)
            Gap(6.dp)
            Text(
                if (setUp) stringResource(R.string.nebula_drop_detail)
                else stringResource(R.string.nebula_drop_detail_setup),
                style = MaterialTheme.typography.bodyMedium, color = Night.TextDim,
            )
        }
    }
}

/** A single column of readable width, scrolling when the screen is short (the folded phone held sideways). */
@Composable
private fun Page(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Box(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), contentAlignment = Alignment.TopCenter) {
        Column(
            modifier.widthIn(max = 620.dp).fillMaxWidth().padding(horizontal = 24.dp, vertical = 24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) { content() }
    }
}

@Composable
private fun WelcomeScreen(outdated: Boolean, onChoose: () -> Unit) {
    Page {
        Gap(24.dp)
        Mark(132.dp)
        Gap(8.dp)
        Text("Nebula", style = MaterialTheme.typography.displayMedium)
        Gap(8.dp)
        Lead(
            if (outdated) stringResource(R.string.nebula_welcome_outdated)
            else stringResource(R.string.nebula_welcome),
        )
        Gap(32.dp)
        GlassCard(Modifier.fillMaxWidth()) {
            Step(1, stringResource(R.string.nebula_step_choose), stringResource(R.string.nebula_step_choose_detail))
            Gap(18.dp)
            Step(2, stringResource(R.string.nebula_step_setup), stringResource(R.string.nebula_step_setup_detail))
            Gap(18.dp)
            Step(3, stringResource(R.string.nebula_play), stringResource(R.string.nebula_step_play_detail))
        }
        Gap(28.dp)
        PrimaryAction(stringResource(R.string.nebula_choose_disc), Icons.Rounded.FolderOpen, onChoose, Modifier.fillMaxWidth())
    }
}

@Composable
private fun Step(number: Int, title: String, detail: String) {
    Row(verticalAlignment = Alignment.Top) {
        Box(
            Modifier.size(32.dp).padding(top = 2.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text("$number", style = MaterialTheme.typography.titleMedium, color = Night.Gold)
        }
        Spacer(Modifier.width(12.dp))
        Column {
            Text(title, style = MaterialTheme.typography.titleMedium)
            Text(detail, style = MaterialTheme.typography.bodyMedium, color = Night.TextDim)
        }
    }
}

@Composable
private fun DiscScreen(model: LauncherModel, onChoose: () -> Unit) {
    val disc = model.disc ?: return
    val identity = disc.identity
    val context = LocalContext.current
    // Setup's progress shows as a notification: asked for first, and setup starts whatever the answer.
    val permission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { model.startImport() }
    val startImport = {
        if (context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED) {
            model.startImport()
        } else {
            permission.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }
    Page {
        Gap(32.dp)
        GlassCard(Modifier.fillMaxWidth()) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconBadge(Icons.Rounded.Description, Night.Blue)
                Spacer(Modifier.width(14.dp))
                Column(Modifier.weight(1f)) {
                    Text(disc.name, style = MaterialTheme.typography.titleMedium, maxLines = 2)
                    if (disc.bytes > 0) Text(formatBytes(disc.bytes), style = MaterialTheme.typography.bodyMedium, color = Night.TextDim)
                }
            }
        }
        Gap(24.dp)
        when {
            identity == null -> {
                ProgressRing(0f, 96.dp, indeterminate = true) {}
                Gap(16.dp)
                Lead(stringResource(R.string.nebula_reading_disc))
            }
            identity.supported -> {
                Icon(Icons.Rounded.CheckCircle, contentDescription = null, tint = Night.Success, modifier = Modifier.size(56.dp))
                Gap(12.dp)
                // Not identity.title: the disc's own title is in capitals.
                Text("Super Mario Galaxy", style = MaterialTheme.typography.headlineMedium, textAlign = TextAlign.Center)
                Gap(4.dp)
                Lead("${identity.gameId} · North America · ${identity.format}")
                Gap(24.dp)
                val enough = model.enoughSpace
                GlassCard(Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.nebula_storage), style = MaterialTheme.typography.titleMedium)
                    Gap(4.dp)
                    Text(
                        stringResource(R.string.nebula_needs_space, formatBytes(Storage.GAME_BYTES), formatBytes(model.roomBytes)),
                        style = MaterialTheme.typography.bodyMedium,
                        color = if (enough) Night.TextDim else Night.Error,
                    )
                    if (!enough) {
                        Gap(6.dp)
                        Text(
                            stringResource(R.string.nebula_free_space),
                            style = MaterialTheme.typography.bodyMedium,
                            color = Night.Error,
                        )
                    }
                }
                Gap(28.dp)
                PrimaryAction(stringResource(R.string.nebula_set_up), Icons.Rounded.AutoAwesome, startImport, Modifier.fillMaxWidth(), enabled = enough)
                Gap(12.dp)
                SecondaryAction(stringResource(R.string.nebula_choose_another), Icons.Rounded.FolderOpen, onChoose, Modifier.fillMaxWidth())
            }
            else -> {
                Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = Night.Error, modifier = Modifier.size(56.dp))
                Gap(12.dp)
                Text(stringResource(R.string.nebula_disc_unusable), style = MaterialTheme.typography.headlineSmall, textAlign = TextAlign.Center)
                Gap(8.dp)
                Lead(identity.message.ifEmpty { stringResource(R.string.nebula_disc_unusable_detail) })
                Gap(28.dp)
                PrimaryAction(stringResource(R.string.nebula_choose_another), Icons.Rounded.FolderOpen, onChoose, Modifier.fillMaxWidth())
            }
        }
    }
}

@Composable
private fun ImportScreen(model: LauncherModel, onPlay: () -> Unit) {
    val state by model.imports.collectAsState()
    Page {
        Gap(40.dp)
        when (val s = state) {
            is ImportState.Running, ImportState.Idle -> {
                val running = s as? ImportState.Running
                val fraction = if (running != null && running.total > 0) running.done.toFloat() / running.total else 0f
                ProgressRing(fraction, 200.dp, indeterminate = running == null || running.total == 0L) {
                    Text("${(fraction * 100).toInt()}%", style = MaterialTheme.typography.displayMedium.merge(Mono))
                }
                Gap(28.dp)
                Text(stringResource(R.string.nebula_setting_up), style = MaterialTheme.typography.headlineSmall, textAlign = TextAlign.Center)
                Gap(8.dp)
                if (running != null && running.total > 0) {
                    val line = buildString {
                        append("${formatBytes(running.done)} of ${formatBytes(running.total)}")
                        if (running.bytesPerSecond > 1e6) {
                            append(" · ${formatBytes(running.bytesPerSecond.toLong())}/s")
                            val left = ((running.total - running.done) / running.bytesPerSecond).roundToInt()
                            append(" · ${if (left >= 60) "about ${(left + 30) / 60} min" else "$left s"} left")
                        }
                    }
                    Text(line, style = MaterialTheme.typography.bodyLarge.merge(Mono), color = Night.TextDim, textAlign = TextAlign.Center)
                } else {
                    Lead(stringResource(R.string.nebula_reading_disc))
                }
                Gap(12.dp)
                Text(
                    stringResource(R.string.nebula_setting_up_detail),
                    style = MaterialTheme.typography.bodyMedium,
                    color = Night.TextFaint,
                    textAlign = TextAlign.Center,
                )
                Gap(32.dp)
                SecondaryAction(stringResource(R.string.nebula_cancel), Icons.Rounded.Close, Imports::cancel)
            }
            ImportState.Finished -> {
                Icon(Icons.Rounded.CheckCircle, contentDescription = null, tint = Night.Success, modifier = Modifier.size(88.dp))
                Gap(16.dp)
                Text(stringResource(R.string.nebula_ready_to_play), style = MaterialTheme.typography.headlineMedium)
                Gap(8.dp)
                Lead(stringResource(R.string.nebula_ready_to_play_detail))
                Gap(32.dp)
                PrimaryAction(stringResource(R.string.nebula_play), Icons.Rounded.PlayArrow, onPlay, Modifier.fillMaxWidth(), tall = true)
                Gap(12.dp)
                SecondaryAction(stringResource(R.string.nebula_done), null, { model.importDone(null) }, Modifier.fillMaxWidth())
            }
            is ImportState.Failed -> {
                Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = Night.Error, modifier = Modifier.size(72.dp))
                Gap(16.dp)
                Text(stringResource(R.string.nebula_setup_failed), style = MaterialTheme.typography.headlineSmall)
                Gap(8.dp)
                Lead(s.message)
                Gap(32.dp)
                PrimaryAction(stringResource(R.string.nebula_try_again), Icons.Rounded.Refresh, model::importRetry, Modifier.fillMaxWidth())
            }
            ImportState.Cancelled -> {
                Text(stringResource(R.string.nebula_setup_cancelled), style = MaterialTheme.typography.headlineSmall)
                Gap(8.dp)
                Lead(stringResource(R.string.nebula_setup_cancelled_detail))
                Gap(32.dp)
                PrimaryAction(stringResource(R.string.nebula_back), null, model::importRetry, Modifier.fillMaxWidth())
            }
        }
    }
}

@Composable
private fun HomeScreen(model: LauncherModel, onPlay: () -> Unit) {
    val install = model.install
    val controllers = rememberControllers()
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val wide = maxWidth >= 720.dp && maxWidth > maxHeight * 0.9f
        val hero: @Composable () -> Unit = {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Mark(if (wide) 168.dp else 128.dp)
                Gap(4.dp)
                Text("Super Mario Galaxy", style = MaterialTheme.typography.headlineMedium, textAlign = TextAlign.Center)
                Gap(4.dp)
                Text(
                    listOfNotNull(install?.gameId, install?.format).joinToString(" · ").ifEmpty { stringResource(R.string.nebula_ready) },
                    style = MaterialTheme.typography.bodyMedium,
                    color = Night.TextDim,
                )
                Gap(12.dp)
                InputChip(controllers)
            }
        }
        val actions: @Composable () -> Unit = {
            Column(Modifier.fillMaxWidth()) {
                PrimaryAction(stringResource(R.string.nebula_play), Icons.Rounded.PlayArrow, onPlay, Modifier.fillMaxWidth(), tall = true)
                Gap(20.dp)
                GlassCard(Modifier.fillMaxWidth(), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 6.dp)) {
                    NavigationRow(Icons.Rounded.Tune, stringResource(R.string.nebula_settings), stringResource(R.string.nebula_settings_detail)) { model.screen = Screen.Settings }
                    HorizontalDivider(color = Night.GlassEdge, modifier = Modifier.padding(horizontal = 20.dp))
                    NavigationRow(Icons.Rounded.Info, stringResource(R.string.nebula_about), stringResource(R.string.nebula_version_licenses, BuildConfig.VERSION_NAME)) { model.screen = Screen.About }
                }
            }
        }
        if (wide) {
            Row(
                Modifier.fillMaxSize().padding(horizontal = 48.dp, vertical = 24.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(48.dp),
            ) {
                Box(Modifier.weight(1f), contentAlignment = Alignment.Center) { hero() }
                Box(Modifier.weight(1f).widthIn(max = 480.dp), contentAlignment = Alignment.Center) { actions() }
            }
        } else {
            Page {
                Gap(28.dp)
                hero()
                Gap(36.dp)
                actions()
            }
        }
    }
}

@Composable
private fun InputChip(controllers: List<String>) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Icon(
            if (controllers.isEmpty()) Icons.Rounded.TouchApp else Icons.Rounded.SportsEsports,
            contentDescription = null,
            tint = Night.Gold,
            modifier = Modifier.size(18.dp),
        )
        Spacer(Modifier.width(8.dp))
        Text(
            if (controllers.isEmpty()) stringResource(R.string.nebula_touch_controls) else controllers.first(),
            style = MaterialTheme.typography.bodyMedium,
            color = Night.TextDim,
        )
    }
}

/** The connected controllers' names, kept current. */
@Composable
private fun rememberControllers(): List<String> {
    val context = LocalContext.current
    var names by remember { mutableStateOf(controllerNames()) }
    DisposableEffect(Unit) {
        val input = context.getSystemService(InputManager::class.java)
        val listener = object : InputManager.InputDeviceListener {
            override fun onInputDeviceAdded(deviceId: Int) { names = controllerNames() }
            override fun onInputDeviceRemoved(deviceId: Int) { names = controllerNames() }
            override fun onInputDeviceChanged(deviceId: Int) { names = controllerNames() }
        }
        input.registerInputDeviceListener(listener, null)
        onDispose { input.unregisterInputDeviceListener(listener) }
    }
    return names
}

private fun controllerNames(): List<String> = InputDevice.getDeviceIds().toList().mapNotNull { id ->
    val device = InputDevice.getDevice(id) ?: return@mapNotNull null
    val sources = device.sources
    val pad = (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
        (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    if (pad && !device.isVirtual) device.name else null
}.distinct()

/**
 * Settings, Performance: the last benchmark's verdict (or that the run did not finish), the recommended settings that
 * differ from the ones it ran with, current then recommended, and Apply (Benchmark.apply, config.ini).
 */
@Composable
private fun BenchmarkRows(model: LauncherModel) {
    val result = model.benchmark
    val verdict = when {
        model.benchmarkUnfinished -> stringResource(R.string.nebula_benchmark_unfinished)
        result == null -> return
        !result.complete -> stringResource(R.string.nebula_benchmark_unfinished) + if (result.failure.isNotEmpty()) " " + result.failure else ""
        result.cpuBound || result.verdict == "cpu_bound" -> stringResource(R.string.nebula_benchmark_cpu)
        result.fits || result.verdict == "fits" -> stringResource(R.string.nebula_benchmark_fits)
        else -> stringResource(R.string.nebula_benchmark_below)
    }
    Text(verdict, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(horizontal = 20.dp, vertical = 8.dp))
    val changes = result?.changes.orEmpty()
    if (changes.isEmpty()) return
    Text(
        stringResource(R.string.nebula_benchmark_recommended),
        style = MaterialTheme.typography.titleSmall,
        modifier = Modifier.padding(start = 20.dp, end = 20.dp, top = 8.dp),
    )
    Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 4.dp)) {
        Spacer(Modifier.weight(1f))
        Text(stringResource(R.string.nebula_benchmark_current), Modifier.width(112.dp), style = MaterialTheme.typography.labelMedium)
        Text(stringResource(R.string.nebula_benchmark_recommended_value), Modifier.width(112.dp), style = MaterialTheme.typography.labelMedium)
    }
    for (change in changes) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(benchmarkLabel(change.key), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
            Text(
                benchmarkValue(change.key, change.current),
                Modifier.width(112.dp),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(benchmarkValue(change.key, change.recommended), Modifier.width(112.dp), style = MaterialTheme.typography.bodyMedium)
        }
    }
    Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp)) {
        TextButton(onClick = { model.applyBenchmark() }, enabled = !model.benchmarkApplied) { Text(stringResource(R.string.nebula_apply)) }
    }
    if (model.benchmarkApplyFailed) {
        Text(
            stringResource(R.string.nebula_settings_not_saved, "config.ini"),
            color = Night.Error,
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(start = 20.dp, end = 20.dp, bottom = 8.dp),
        )
    }
}

@Composable
private fun benchmarkLabel(key: String): String = when (key) {
    "internal_height" -> stringResource(R.string.nebula_setting_internal_resolution)
    "msaa_samples" -> "MSAA"
    "ambient_occlusion" -> stringResource(R.string.nebula_setting_ambient_occlusion)
    "enhanced_lighting" -> stringResource(R.string.nebula_setting_lighting)
    "aa" -> stringResource(R.string.nebula_setting_aa)
    "smaa_quality" -> stringResource(R.string.nebula_setting_aa_quality)
    "bloom" -> stringResource(R.string.nebula_setting_bloom)
    else -> key
}

// A config.ini value as the game's menu names it (the contract: internal_height 0 matches the display, aa 1 is SMAA).
@Composable
private fun benchmarkValue(key: String, value: Int): String = when (key) {
    "internal_height" -> if (value == 0) stringResource(R.string.nebula_match_display) else "${value}p"
    "msaa_samples" -> if (value <= 1) stringResource(R.string.nebula_off) else "${value}x"
    "ambient_occlusion" -> stringResource(if (value != 0) R.string.nebula_on else R.string.nebula_off)
    "enhanced_lighting" -> stringResource(if (value != 0) R.string.nebula_lighting_enhanced else R.string.nebula_lighting_per_pixel)
    "aa" -> when (value) {
        0 -> stringResource(R.string.nebula_off)
        1 -> "SMAA"
        2 -> stringResource(R.string.nebula_aa_auto)
        3 -> "FXAA"
        else -> value.toString()
    }
    "smaa_quality" -> when (value) {
        0 -> stringResource(R.string.nebula_quality_low)
        1 -> stringResource(R.string.nebula_quality_medium)
        2 -> stringResource(R.string.nebula_quality_high)
        3 -> stringResource(R.string.nebula_quality_ultra)
        else -> value.toString()
    }
    "bloom" -> "$value%"
    else -> value.toString()
}

/** The file pickers: saves exported to a new .zip or imported from a file, Miis from any number of files. */
private class SaveActions(val export: () -> Unit, val import: () -> Unit, val miis: () -> Unit)

@Composable
private fun SettingsScreen(
    model: LauncherModel,
    onChoose: () -> Unit,
    onChooseMod: () -> Unit,
    saves: SaveActions,
    onShareLog: () -> Unit,
) {
    var confirmRemove by remember { mutableStateOf(false) }
    var confirmImport by remember { mutableStateOf(false) }
    LaunchedEffect(model.saveBytes) { model.refreshMiis() }
    Column(Modifier.fillMaxSize()) {
        TopBar(stringResource(R.string.nebula_settings)) { model.back() }
        Box(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), contentAlignment = Alignment.TopCenter) {
            Column(Modifier.widthIn(max = 680.dp).fillMaxWidth().padding(bottom = 32.dp)) {
                SectionTitle(stringResource(R.string.nebula_storage))
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    val install = model.install
                    NavigationRow(
                        Icons.Rounded.FolderOpen,
                        stringResource(R.string.nebula_set_up_again),
                        stringResource(R.string.nebula_set_up_again_detail),
                        onChoose,
                    )
                    if (install != null) {
                        NavigationRow(
                            Icons.Rounded.Delete,
                            stringResource(R.string.nebula_remove_game),
                            stringResource(R.string.nebula_remove_game_detail, formatBytes(install.bytes), formatBytes(model.saveBytes)),
                        ) { confirmRemove = true }
                    }
                }

                SectionTitle(stringResource(R.string.nebula_saves))
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    NavigationRow(
                        Icons.Rounded.Upload,
                        stringResource(R.string.nebula_export_saves),
                        if (model.saveBusy) stringResource(R.string.nebula_working) else stringResource(R.string.nebula_export_saves_detail),
                    ) { if (!model.saveBusy) saves.export() }
                    NavigationRow(
                        Icons.Rounded.Download,
                        stringResource(R.string.nebula_import_saves),
                        stringResource(R.string.nebula_import_saves_detail),
                    ) { if (!model.saveBusy) confirmImport = true }
                }

                SectionTitle(stringResource(R.string.nebula_miis))
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    NavigationRow(
                        Icons.Rounded.Face,
                        stringResource(R.string.nebula_import_miis),
                        when {
                            model.miiBusy -> stringResource(R.string.nebula_working)
                            else -> stringResource(R.string.nebula_import_miis_detail) +
                                (model.miiCount?.let { " · " + stringResource(R.string.nebula_mii_count, it.toString()) } ?: "")
                        },
                    ) { if (!model.miiBusy) saves.miis() }
                }

                SectionTitle(stringResource(R.string.nebula_controllers))
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    NavigationRow(
                        Icons.Rounded.SportsEsports,
                        stringResource(R.string.nebula_motion_test),
                        stringResource(R.string.nebula_motion_test_detail),
                    ) { model.screen = Screen.MotionTest }
                }

                SectionTitle(stringResource(R.string.nebula_mods))
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 4.dp)) {
                    val progress = model.modProgress
                    NavigationRow(
                        Icons.Rounded.Add,
                        stringResource(R.string.nebula_import_mod),
                        if (progress != null) stringResource(R.string.nebula_importing, (progress * 100).roundToInt().toString())
                        else stringResource(R.string.nebula_import_mod_detail),
                    ) { if (progress == null) onChooseMod() }
                    if (progress != null) {
                        LinearProgressIndicator(
                            progress = { progress },
                            modifier = Modifier.fillMaxWidth().padding(start = 20.dp, end = 20.dp, bottom = 14.dp),
                        )
                    }
                }

                if (model.install != null) {
                    SectionTitle(stringResource(R.string.nebula_performance))
                    GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                        val activity = LocalContext.current
                        NavigationRow(
                            Icons.Rounded.Speed,
                            stringResource(R.string.nebula_benchmark),
                            stringResource(R.string.nebula_benchmark_about),
                        ) { model.startBenchmark(activity) }
                        BenchmarkRows(model)
                    }
                }

                // A store's install updates through the store: no check to turn on or off.
                val appContext = LocalContext.current
                if (!remember(appContext) { dev.nebula.galaxy.Updates.fromStore(appContext) }) {
                    SectionTitle(stringResource(R.string.nebula_updates))
                    GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                        SwitchRow(stringResource(R.string.nebula_updates_at_start), null, model.updatesAtStart) { model.updateUpdatesAtStart(it) }
                    }
                }

                SectionTitle(stringResource(R.string.nebula_help))
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    SwitchRow(
                        stringResource(R.string.nebula_record_diagnostics),
                        stringResource(R.string.nebula_record_diagnostics_detail),
                        model.recordNextRun,
                    ) { model.updateRecordNextRun(it) }
                    NavigationRow(Icons.Rounded.Share, stringResource(R.string.nebula_share_log), stringResource(R.string.nebula_share_log_detail), onShareLog)
                }
            }
        }
    }
    if (confirmImport) {
        AlertDialog(
            onDismissRequest = { confirmImport = false },
            icon = { Icon(Icons.Rounded.Download, contentDescription = null) },
            title = { Text(stringResource(R.string.nebula_replace_saves)) },
            text = { Text(stringResource(R.string.nebula_replace_saves_detail)) },
            confirmButton = { TextButton(onClick = { confirmImport = false; saves.import() }) { Text(stringResource(R.string.nebula_choose_file)) } },
            dismissButton = { TextButton(onClick = { confirmImport = false }) { Text(stringResource(R.string.nebula_cancel)) } },
        )
    }
    if (confirmRemove) {
        AlertDialog(
            onDismissRequest = { confirmRemove = false },
            icon = { Icon(Icons.Rounded.Delete, contentDescription = null) },
            title = { Text(stringResource(R.string.nebula_remove_game_question)) },
            text = { Text(stringResource(R.string.nebula_remove_game_question_detail)) },
            confirmButton = {
                TextButton(onClick = { confirmRemove = false; model.removeGame() }) { Text(stringResource(R.string.nebula_remove), color = Night.Error) }
            },
            dismissButton = { TextButton(onClick = { confirmRemove = false }) { Text(stringResource(R.string.nebula_keep)) } },
        )
    }
}

@Composable
private fun AboutScreen(model: LauncherModel) {
    Column(Modifier.fillMaxSize()) {
        TopBar(stringResource(R.string.nebula_about_title)) { model.back() }
        Page {
            Mark(112.dp)
            Text("Nebula", style = MaterialTheme.typography.headlineMedium)
            Text(stringResource(R.string.nebula_version, BuildConfig.VERSION_NAME), style = MaterialTheme.typography.bodyMedium, color = Night.TextDim)
            Gap(20.dp)
            Lead(stringResource(R.string.nebula_about_text))
            Gap(24.dp)
            Text(
                stringResource(R.string.nebula_licenses).uppercase(),
                style = MaterialTheme.typography.labelMedium,
                color = Night.Lavender,
                modifier = Modifier.fillMaxWidth().padding(start = 8.dp, bottom = 8.dp),
            )
            GlassCard(Modifier.fillMaxWidth(), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 6.dp)) {
                licenses.forEachIndexed { index, license ->
                    if (index > 0) HorizontalDivider(color = Night.GlassEdge, modifier = Modifier.padding(horizontal = 20.dp))
                    NavigationRow(Icons.Rounded.Description, license.name, license.terms) {
                        model.license = license
                        model.screen = Screen.Licenses
                    }
                }
            }
        }
    }
}

@Composable
private fun LicenseScreen(model: LauncherModel) {
    val license = model.license ?: return
    val context = LocalContext.current
    val paragraphs = remember(license.file) {
        runCatching {
            context.assets.open("licenses/${license.file}").bufferedReader().use { it.readText() }
        }.getOrElse { "This license text is missing from the app." }.split("\n\n")
    }
    Column(Modifier.fillMaxSize()) {
        TopBar(license.name) { model.back() }
        LazyColumn(Modifier.fillMaxSize().padding(horizontal = 20.dp)) {
            items(paragraphs) { paragraph ->
                Text(
                    paragraph,
                    style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace, fontSize = 12.sp, lineHeight = 17.sp),
                    color = Night.TextDim,
                    modifier = Modifier.padding(bottom = 12.dp),
                )
            }
        }
    }
}
