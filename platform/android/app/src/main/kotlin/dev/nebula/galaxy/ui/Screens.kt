// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy.ui

import android.Manifest
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
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
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
import androidx.compose.material.icons.rounded.Share
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.nebula.galaxy.BuildConfig
import dev.nebula.galaxy.ControlsSettings
import dev.nebula.galaxy.DisplayRate
import dev.nebula.galaxy.GraphicsSettings
import dev.nebula.galaxy.ImportState
import dev.nebula.galaxy.Imports
import dev.nebula.galaxy.LauncherModel
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
    BackHandler(enabled = model.screen in setOf(Screen.Disc, Screen.Settings, Screen.About, Screen.Licenses)) {
        model.back()
    }
    Box(Modifier.fillMaxSize()) {
        Starfield()
        AnimatedContent(
            targetState = model.screen,
            transitionSpec = { (fadeIn(tween(260)) + scaleIn(tween(260), initialScale = 0.98f)) togetherWith fadeOut(tween(160)) },
            label = "screen",
            modifier = Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing),
        ) { screen ->
            when (screen) {
                Screen.Welcome -> WelcomeScreen(onChoose = choose)
                Screen.Disc -> DiscScreen(model, onChoose = choose)
                Screen.Import -> ImportScreen(model, onPlay = { model.importDone(context) })
                Screen.Home -> HomeScreen(model, onPlay = { model.launchGame(context) })
                Screen.Settings -> SettingsScreen(model, onChoose = choose, onShareLog = onShareLog)
                Screen.About -> AboutScreen(model)
                Screen.Licenses -> LicenseScreen(model)
            }
        }
    }
    model.gameExit?.let { exit ->
        AlertDialog(
            onDismissRequest = { model.gameExit = null },
            icon = { Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = Night.Error) },
            title = { Text(exit.title) },
            text = { Text(exit.message) },
            confirmButton = { TextButton(onClick = { model.gameExit = null }) { Text("OK") } },
            dismissButton = if (exit.hasLog) {
                { TextButton(onClick = { onShareLog(); model.gameExit = null }) { Text("Share the log") } }
            } else null,
        )
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
private fun WelcomeScreen(onChoose: () -> Unit) {
    Page {
        Gap(24.dp)
        Mark(132.dp)
        Gap(8.dp)
        Text("Nebula", style = MaterialTheme.typography.displayMedium)
        Gap(8.dp)
        Lead("Super Mario Galaxy, running natively on this device.")
        Gap(32.dp)
        GlassCard(Modifier.fillMaxWidth()) {
            Step(1, "Choose your disc image", "The copy you made of your own Wii disc: ISO, RVZ, WIA, WBFS, CISO or GCZ.")
            Gap(18.dp)
            Step(2, "Nebula sets the game up", "It copies the game into its own storage (about 3.4 GB). The disc image is only read.")
            Gap(18.dp)
            Step(3, "Play", "With the touch controls, or a controller.")
        }
        Gap(28.dp)
        PrimaryAction("Choose disc image", Icons.Rounded.FolderOpen, onChoose, Modifier.fillMaxWidth())
        Gap(20.dp)
        Text(
            "Nebula contains no Nintendo code or data.",
            style = MaterialTheme.typography.bodySmall,
            color = Night.TextFaint,
            textAlign = TextAlign.Center,
        )
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
                Lead("Reading the disc image…")
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
                    Text("Storage", style = MaterialTheme.typography.titleMedium)
                    Gap(4.dp)
                    Text(
                        "Needs ${formatBytes(Storage.GAME_BYTES)} · ${formatBytes(model.freeBytes)} free",
                        style = MaterialTheme.typography.bodyMedium,
                        color = if (enough) Night.TextDim else Night.Error,
                    )
                    if (!enough) {
                        Gap(6.dp)
                        Text(
                            "Free up some space on this device, then try again.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = Night.Error,
                        )
                    }
                }
                Gap(28.dp)
                PrimaryAction("Set up", Icons.Rounded.AutoAwesome, startImport, Modifier.fillMaxWidth(), enabled = enough)
                Gap(12.dp)
                SecondaryAction("Choose another", Icons.Rounded.FolderOpen, onChoose, Modifier.fillMaxWidth())
            }
            else -> {
                Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = Night.Error, modifier = Modifier.size(56.dp))
                Gap(12.dp)
                Text("This disc image can't be used", style = MaterialTheme.typography.headlineSmall, textAlign = TextAlign.Center)
                Gap(8.dp)
                Lead(identity.message.ifEmpty { "It is not the North American Super Mario Galaxy (RMGE01)." })
                Gap(28.dp)
                PrimaryAction("Choose another", Icons.Rounded.FolderOpen, onChoose, Modifier.fillMaxWidth())
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
                Text("Setting up Super Mario Galaxy", style = MaterialTheme.typography.headlineSmall, textAlign = TextAlign.Center)
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
                    Lead("Reading the disc image…")
                }
                Gap(12.dp)
                Text(
                    "You can leave Nebula meanwhile: setup carries on, with its progress in the notifications.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = Night.TextFaint,
                    textAlign = TextAlign.Center,
                )
                Gap(32.dp)
                SecondaryAction("Cancel", Icons.Rounded.Close, Imports::cancel)
            }
            ImportState.Finished -> {
                Icon(Icons.Rounded.CheckCircle, contentDescription = null, tint = Night.Success, modifier = Modifier.size(88.dp))
                Gap(16.dp)
                Text("Ready to play", style = MaterialTheme.typography.headlineMedium)
                Gap(8.dp)
                Lead("Super Mario Galaxy is set up. You can delete the disc image if you no longer need it.")
                Gap(32.dp)
                PrimaryAction("Play", Icons.Rounded.PlayArrow, onPlay, Modifier.fillMaxWidth(), tall = true)
                Gap(12.dp)
                SecondaryAction("Done", null, { model.importDone(null) }, Modifier.fillMaxWidth())
            }
            is ImportState.Failed -> {
                Icon(Icons.Rounded.ErrorOutline, contentDescription = null, tint = Night.Error, modifier = Modifier.size(72.dp))
                Gap(16.dp)
                Text("Setup didn't finish", style = MaterialTheme.typography.headlineSmall)
                Gap(8.dp)
                Lead(s.message)
                Gap(32.dp)
                PrimaryAction("Try again", Icons.Rounded.Refresh, model::importRetry, Modifier.fillMaxWidth())
            }
            ImportState.Cancelled -> {
                Text("Setup cancelled", style = MaterialTheme.typography.headlineSmall)
                Gap(8.dp)
                Lead("Nothing was kept. You can start again whenever you like.")
                Gap(32.dp)
                PrimaryAction("Back", null, model::importRetry, Modifier.fillMaxWidth())
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
                    listOfNotNull(install?.gameId, install?.format).joinToString(" · ").ifEmpty { "Ready" },
                    style = MaterialTheme.typography.bodyMedium,
                    color = Night.TextDim,
                )
                Gap(12.dp)
                InputChip(controllers)
            }
        }
        val actions: @Composable () -> Unit = {
            Column(Modifier.fillMaxWidth()) {
                PrimaryAction("Play", Icons.Rounded.PlayArrow, onPlay, Modifier.fillMaxWidth(), tall = true)
                Gap(20.dp)
                GlassCard(Modifier.fillMaxWidth(), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 6.dp)) {
                    NavigationRow(Icons.Rounded.Tune, "Settings", "Graphics, touch controls, storage") { model.screen = Screen.Settings }
                    HorizontalDivider(color = Night.GlassEdge, modifier = Modifier.padding(horizontal = 20.dp))
                    NavigationRow(Icons.Rounded.Info, "About Nebula", "Version ${BuildConfig.VERSION_NAME}, licenses") { model.screen = Screen.About }
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
            if (controllers.isEmpty()) "Touch controls" else controllers.first(),
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

@Composable
private fun SettingsScreen(model: LauncherModel, onChoose: () -> Unit, onShareLog: () -> Unit) {
    var confirmRemove by remember { mutableStateOf(false) }
    val graphics = model.graphics
    val controls = model.controls
    Column(Modifier.fillMaxSize()) {
        TopBar("Settings") { model.back() }
        Box(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), contentAlignment = Alignment.TopCenter) {
            Column(Modifier.widthIn(max = 680.dp).fillMaxWidth().padding(bottom = 32.dp)) {
                SectionTitle("Graphics")
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    Text(
                        "Internal resolution",
                        style = MaterialTheme.typography.titleMedium,
                        modifier = Modifier.padding(start = 20.dp, top = 10.dp, end = 20.dp),
                    )
                    Text(
                        "Sharper pictures take more power. Takes effect the next time the game starts.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = Night.TextDim,
                        modifier = Modifier.padding(horizontal = 20.dp, vertical = 4.dp),
                    )
                    for (scale in 1..GraphicsSettings.MAX_SCALE) {
                        ChoiceRow(
                            "${scale}×" + if (scale == 1) " (the Wii's)" else "",
                            "${640 * scale} × ${528 * scale}",
                            !graphics.matchScreen && graphics.scale == scale,
                        ) { model.updateGraphics(graphics.copy(scale = scale, matchScreen = false)) }
                    }
                    ChoiceRow("Match the screen", "The smallest that covers this display", graphics.matchScreen) {
                        model.updateGraphics(graphics.copy(matchScreen = true))
                    }
                    SwitchRow("Menus and HUD at the screen's resolution", "Sharp text at any internal resolution",
                        graphics.hudAtScreen) {
                        model.updateGraphics(graphics.copy(hudAtScreen = it))
                    }
                    val refresh = LocalView.current.display?.let { DisplayRate.fastestMode(it).refreshRate.roundToInt() } ?: 60
                    if (refresh > 61) {
                        HorizontalDivider(color = Night.GlassEdge, modifier = Modifier.padding(horizontal = 20.dp, vertical = 6.dp))
                        Text(
                            "Frame rate",
                            style = MaterialTheme.typography.titleMedium,
                            modifier = Modifier.padding(start = 20.dp, top = 10.dp, end = 20.dp),
                        )
                        ChoiceRow("60 fps", "The game's own rate", !graphics.displayRate) {
                            model.updateGraphics(graphics.copy(displayRate = false))
                        }
                        ChoiceRow("$refresh fps", "Frames in between are drawn from the game's motion. More power.", graphics.displayRate) {
                            model.updateGraphics(graphics.copy(displayRate = true))
                        }
                    }
                    HorizontalDivider(color = Night.GlassEdge, modifier = Modifier.padding(horizontal = 20.dp, vertical = 6.dp))
                    Text(
                        "Anti-aliasing",
                        style = MaterialTheme.typography.titleMedium,
                        modifier = Modifier.padding(start = 20.dp, top = 10.dp, end = 20.dp),
                    )
                    val msaa = graphics.msaa > 1
                    ChoiceRow("Off", null, graphics.aa == "off" && !msaa) {
                        model.updateGraphics(graphics.copy(aa = "off", msaa = 1))
                    }
                    ChoiceRow("FXAA", "Lightest", graphics.aa == "fxaa" && !msaa) {
                        model.updateGraphics(graphics.copy(aa = "fxaa", msaa = 1))
                    }
                    ChoiceRow("SMAA", "Cleaner edges", graphics.aa == "smaa" && !msaa) {
                        model.updateGraphics(graphics.copy(aa = "smaa", msaa = 1))
                    }
                    ChoiceRow("MSAA 2x", "Geometry edges sampled twice. More power.", graphics.msaa == 2) {
                        model.updateGraphics(graphics.copy(aa = "off", msaa = 2))
                    }
                    ChoiceRow("MSAA 4x", "Smoothest geometry edges. Most power.", graphics.msaa == 4) {
                        model.updateGraphics(graphics.copy(aa = "off", msaa = 4))
                    }
                    HorizontalDivider(color = Night.GlassEdge, modifier = Modifier.padding(horizontal = 20.dp, vertical = 6.dp))
                    Text(
                        "Enlarging to the screen",
                        style = MaterialTheme.typography.titleMedium,
                        modifier = Modifier.padding(start = 20.dp, top = 10.dp, end = 20.dp),
                    )
                    ChoiceRow("Bilinear", null, !graphics.fsr) { model.updateGraphics(graphics.copy(fsr = false)) }
                    ChoiceRow("FSR 1", "Sharper edges when enlarging", graphics.fsr) {
                        model.updateGraphics(graphics.copy(fsr = true))
                    }
                    SliderRow("Sharpening", graphics.sharpness / 100f, 0f..1f, { "${(it * 100).roundToInt()}%" }) {
                        model.updateGraphics(graphics.copy(sharpness = (it * 100).roundToInt()))
                    }
                    HorizontalDivider(color = Night.GlassEdge, modifier = Modifier.padding(horizontal = 20.dp, vertical = 6.dp))
                    SwitchRow("VSync", "Frames wait for the display. Off lowers latency.", graphics.vsync) {
                        model.updateGraphics(graphics.copy(vsync = it))
                    }
                }

                SectionTitle("Touch controls")
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    ChoiceRow("Automatic", "Hidden while a controller is used; touch the screen to bring them back", controls.mode == "auto") {
                        model.updateControls(controls.copy(mode = "auto"))
                    }
                    ChoiceRow("Always", null, controls.mode == "always") { model.updateControls(controls.copy(mode = "always")) }
                    ChoiceRow("Never", "The screen is only the pointer", controls.mode == "never") {
                        model.updateControls(controls.copy(mode = "never"))
                    }
                    HorizontalDivider(color = Night.GlassEdge, modifier = Modifier.padding(horizontal = 20.dp, vertical = 6.dp))
                    SliderRow("Opacity", controls.opacity, 0.2f..1f, { "${(it * 100).roundToInt()}%" }) {
                        model.updateControls(controls.copy(opacity = it))
                    }
                    SliderRow("Size", controls.scale, 0.75f..1.4f, { "${(it * 100).roundToInt()}%" }) {
                        model.updateControls(controls.copy(scale = it))
                    }
                    SwitchRow("Vibration", "A tap you can feel on each press", controls.haptics) {
                        model.updateControls(controls.copy(haptics = it))
                    }
                }

                SectionTitle("Storage")
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    val install = model.install
                    NavigationRow(
                        Icons.Rounded.FolderOpen,
                        "Set up again",
                        "From another disc image",
                        onChoose,
                    )
                    if (install != null) {
                        NavigationRow(
                            Icons.Rounded.Delete,
                            "Remove the game",
                            "${formatBytes(install.bytes)} · your saves (${formatBytes(model.saveBytes)}) are kept",
                        ) { confirmRemove = true }
                    }
                }

                SectionTitle("Help")
                GlassCard(Modifier.fillMaxWidth().padding(horizontal = 16.dp), padding = androidx.compose.foundation.layout.PaddingValues(vertical = 8.dp)) {
                    NavigationRow(Icons.Rounded.Share, "Share the game's log", "The last run's, to report a problem", onShareLog)
                }
            }
        }
    }
    if (confirmRemove) {
        AlertDialog(
            onDismissRequest = { confirmRemove = false },
            icon = { Icon(Icons.Rounded.Delete, contentDescription = null) },
            title = { Text("Remove the game?") },
            text = { Text("Your saves stay. To play again, set the game up from your disc image.") },
            confirmButton = {
                TextButton(onClick = { confirmRemove = false; model.removeGame() }) { Text("Remove", color = Night.Error) }
            },
            dismissButton = { TextButton(onClick = { confirmRemove = false }) { Text("Keep") } },
        )
    }
}

@Composable
private fun AboutScreen(model: LauncherModel) {
    Column(Modifier.fillMaxSize()) {
        TopBar("About") { model.back() }
        Page {
            Mark(112.dp)
            Text("Nebula", style = MaterialTheme.typography.headlineMedium)
            Text("Version ${BuildConfig.VERSION_NAME}", style = MaterialTheme.typography.bodyMedium, color = Night.TextDim)
            Gap(20.dp)
            Lead(
                "Nebula runs Super Mario Galaxy natively: the game's code is Petari's decompilation, compiled for this " +
                    "device, drawing with Vulkan. It contains no Nintendo code or data. The game's files come from " +
                    "your own disc. Nebula is not affiliated with or endorsed by Nintendo.",
            )
            Gap(24.dp)
            Text(
                "Open source licenses".uppercase(),
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
