// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.hardware.Sensor
import android.view.InputDevice
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.ui.res.stringResource
import dev.nebula.galaxy.R
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.nebula.galaxy.PadSensors
import kotlin.math.abs
import kotlin.math.sqrt

/**
 * Each controller's motion as Android gives it to the game (PadSensors, the same reader the game uses): the sensors it
 * lists, every sample's rate and spacing, the values on the game's axes, a pointer turned by the gyroscope as the
 * game's is, and shakes. With a report to copy.
 */
private class MotionProbe : PadSensors.Sink {
    class Pad(val id: Int) {
        var name = ""
        var ids = ""
        var gyroscope: String? = null
        var accelerometer: String? = null
        var nintendo = false
        var gyroSamples = 0L
        var accelSamples = 0L
        var lastGyroNs = 0L
        var gapMs = 0.0
        var worstGapMs = 0.0
        val gyro = FloatArray(3)   // the game's axes, radians a second
        val accel = FloatArray(3)  // the game's axes, g
        var pointerX = 0f
        var pointerY = 0f
        var shakeNs = 0L
        // Rates over the last whole second.
        var windowStartNs = 0L
        var windowGyro = 0L
        var windowAccel = 0L
        var gyroRate = 0.0
        var accelRate = 0.0
    }

    val pads = LinkedHashMap<Int, Pad>()

    @Synchronized
    override fun device(device: InputDevice, gyroscope: Sensor?, accelerometer: Sensor?) {
        val pad = pads.getOrPut(device.id) { Pad(device.id) }
        pad.name = device.name
        pad.ids = String.format(java.util.Locale.ROOT, "%04x:%04x", device.vendorId, device.productId)
        pad.nintendo = device.vendorId == 0x057e
        pad.gyroscope = gyroscope?.let(::describe)
        pad.accelerometer = accelerometer?.let(::describe)
    }

    @Synchronized
    override fun removed(deviceId: Int) {
        pads.remove(deviceId)
    }

    @Synchronized
    override fun sample(deviceId: Int, type: Int, timeNs: Long, x: Float, y: Float, z: Float) {
        val pad = pads[deviceId] ?: return
        // As the game turns them (host_window_sdl.cpp android_to_sdl_frame): x right, y up, z toward the player.
        val gx = if (pad.nintendo) -y else x
        val gy = if (pad.nintendo) z else y
        val gz = if (pad.nintendo) -x else z
        if (pad.windowStartNs == 0L) pad.windowStartNs = timeNs
        if (timeNs - pad.windowStartNs >= 1_000_000_000L) {
            val seconds = (timeNs - pad.windowStartNs) / 1e9
            pad.gyroRate = pad.windowGyro / seconds
            pad.accelRate = pad.windowAccel / seconds
            pad.windowGyro = 0
            pad.windowAccel = 0
            pad.windowStartNs = timeNs
            pad.worstGapMs = pad.gapMs
        }
        if (type == Sensor.TYPE_ACCELEROMETER) {
            pad.accelSamples++
            pad.windowAccel++
            pad.accel[0] = gx / 9.80665f
            pad.accel[1] = gy / 9.80665f
            pad.accel[2] = gz / 9.80665f
            val g = sqrt(pad.accel[0] * pad.accel[0] + pad.accel[1] * pad.accel[1] + pad.accel[2] * pad.accel[2])
            // When it came, on the clock the screen reads (a sensor's own clock may be another).
            if (abs(g - 1f) > 1.2f) pad.shakeNs = System.nanoTime()
            return
        }
        if (type != Sensor.TYPE_GYROSCOPE) return
        pad.gyroSamples++
        pad.windowGyro++
        pad.gyro[0] = gx
        pad.gyro[1] = gy
        pad.gyro[2] = gz
        if (pad.lastGyroNs != 0L && timeNs > pad.lastGyroNs) {
            val dt = (timeNs - pad.lastGyroNs) / 1e9f
            pad.gapMs = dt * 1000.0
            if (pad.gapMs > pad.worstGapMs) pad.worstGapMs = pad.gapMs
            if (dt < 0.1f) {
                // A turn of 0.6 radians (about 34 degrees) crosses half the field, as a pointer does in the game.
                pad.pointerX = (pad.pointerX - gy * dt / 0.6f).coerceIn(-1.2f, 1.2f)
                pad.pointerY = (pad.pointerY - gx * dt / 0.6f).coerceIn(-1.2f, 1.2f)
            }
        }
        pad.lastGyroNs = timeNs
    }

    @Synchronized
    fun recenter() {
        for (pad in pads.values) {
            pad.pointerX = 0f
            pad.pointerY = 0f
        }
    }

    class View(
        val id: Int, val name: String, val ids: String, val nintendo: Boolean, val gyroscope: String?, val accelerometer: String?,
        val gyroSamples: Long, val accelSamples: Long, val gyroRate: Double, val accelRate: Double, val worstGapMs: Double,
        val gyro: List<Float>, val accel: List<Float>, val pointerX: Float, val pointerY: Float, val shaking: Boolean,
    )

    @Synchronized
    fun snapshot(nowNs: Long): List<View> = pads.values.map { p ->
        View(
            p.id, p.name, p.ids, p.nintendo, p.gyroscope, p.accelerometer, p.gyroSamples, p.accelSamples, p.gyroRate, p.accelRate,
            p.worstGapMs, p.gyro.toList(), p.accel.toList(), p.pointerX, p.pointerY,
            p.shakeNs != 0L && nowNs - p.shakeNs < 400_000_000L,
        )
    }

    private fun describe(sensor: Sensor): String =
        "${sensor.name} (${sensor.vendor}), fastest ${if (sensor.minDelay > 0) "${sensor.minDelay} µs" else "not listed"}, " +
            "range ${String.format(java.util.Locale.ROOT, "%.1f", sensor.maximumRange)}"
}

private fun kernelVersion(): String = System.getProperty("os.version")?.substringBefore('-') ?: "?"

// Linux before 6.1: hid-nintendo without the IMU (Android's android12-5.10 and android13/14-5.15 kernels).
private fun oldNintendoDriver(): Boolean {
    val parts = kernelVersion().split('.').mapNotNull { it.toIntOrNull() }
    if (parts.size < 2) return false
    return parts[0] < 6 || (parts[0] == 6 && parts[1] < 1)
}

private fun report(views: List<MotionProbe.View>): String = buildString {
    append("Nebula motion test, Android ${android.os.Build.VERSION.RELEASE} (API ${android.os.Build.VERSION.SDK_INT}), ")
    append("${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}, Linux ${System.getProperty("os.version")}\n")
    if (views.isEmpty()) append("No controller connected.\n")
    for (v in views) {
        append("${v.name} ${v.ids}\n")
        append("  gyroscope: ${v.gyroscope ?: "none listed by Android"}\n")
        append("  accelerometer: ${v.accelerometer ?: "none listed by Android"}\n")
        append(String.format(java.util.Locale.ROOT, "  samples: gyro %d (%.0f/s), accel %d (%.0f/s), longest gap %.1f ms\n",
            v.gyroSamples, v.gyroRate, v.accelSamples, v.accelRate, v.worstGapMs))
        append(String.format(java.util.Locale.ROOT, "  gyro %.3f %.3f %.3f rad/s, accel %.3f %.3f %.3f g\n",
            v.gyro[0], v.gyro[1], v.gyro[2], v.accel[0], v.accel[1], v.accel[2]))
    }
}

@Composable
fun MotionTestScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val probe = remember { MotionProbe() }
    DisposableEffect(Unit) {
        val sensors = PadSensors(context, probe)
        sensors.start()
        onDispose { sensors.stop() }
    }
    var views by remember { mutableStateOf(emptyList<MotionProbe.View>()) }
    LaunchedEffect(Unit) {
        while (true) {
            withFrameNanos { views = probe.snapshot(System.nanoTime()) }
        }
    }
    var copied by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize()) {
        TopBar(stringResource(R.string.MotionTest), onBack)
        Box(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), contentAlignment = Alignment.TopCenter) {
            Column(Modifier.widthIn(max = 680.dp).fillMaxWidth().padding(horizontal = 16.dp).padding(bottom = 32.dp)) {
                Lead(stringResource(R.string.MotionTestLead))
                Gap(12.dp)
                if (views.isEmpty()) {
                    GlassCard(Modifier.fillMaxWidth()) {
                        Text(stringResource(R.string.NoController), style = MaterialTheme.typography.titleMedium)
                        Text(
                            stringResource(R.string.PairController),
                            style = MaterialTheme.typography.bodyMedium, color = Night.TextDim,
                        )
                    }
                }
                for (v in views) {
                    PadCard(v) { probe.recenter() }
                    Gap(12.dp)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    SecondaryAction(stringResource(R.string.Recenter), Icons.Rounded.Refresh, { probe.recenter() })
                    SecondaryAction(stringResource(if (copied) R.string.Copied else R.string.CopyReport), Icons.Rounded.ContentCopy, {
                        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        clipboard.setPrimaryClip(ClipData.newPlainText("Nebula motion test", report(views)))
                        copied = true
                    })
                }
            }
        }
    }
}

@Composable
private fun PadCard(v: MotionProbe.View, onRecenter: () -> Unit) {
    GlassCard(Modifier.fillMaxWidth()) {
        Text(v.name, style = MaterialTheme.typography.titleMedium)
        Text(v.ids, style = MaterialTheme.typography.bodySmall, color = Night.TextFaint)
        Gap(10.dp)
        val works = v.gyroscope != null && v.gyroSamples > 0
        val rate = String.format(java.util.Locale.getDefault(), "%.0f", v.gyroRate)
        val verdict = when {
            v.gyroscope == null && v.nintendo && oldNintendoDriver() -> stringResource(R.string.MotionOldDriver, kernelVersion())
            v.gyroscope == null -> stringResource(R.string.MotionNoGyro)
            v.gyroSamples == 0L -> stringResource(R.string.MotionNoSamples)
            v.gyroRate in 1.0..59.0 -> stringResource(R.string.MotionCoarse, rate)
            else -> stringResource(R.string.MotionWorks, rate)
        }
        Text(verdict, style = MaterialTheme.typography.bodyMedium, color = if (works) Night.Success else Night.Gold)
        Gap(12.dp)
        if (v.gyroscope != null) {
            Box(
                Modifier.fillMaxWidth().aspectRatio(16f / 9f).clip(RoundedCornerShape(14.dp))
                    .background(Color.Black.copy(alpha = 0.45f))
                    .pointerInput(Unit) { detectTapGestures { onRecenter() } },
            ) {
                Canvas(Modifier.fillMaxSize()) {
                    val center = Offset(size.width / 2f, size.height / 2f)
                    drawLine(Night.GlassEdge, Offset(0f, center.y), Offset(size.width, center.y), 1f)
                    drawLine(Night.GlassEdge, Offset(center.x, 0f), Offset(center.x, size.height), 1f)
                    val dot = Offset(center.x + v.pointerX * size.width / 2f, center.y + v.pointerY * size.width / 2f)
                    drawCircle(if (v.shaking) Night.Gold else Night.Lavender, radius = 14.dp.toPx(), center = dot)
                }
                if (v.shaking) {
                    Text(stringResource(R.string.Shake), color = Night.Gold, style = MaterialTheme.typography.labelLarge,
                        modifier = Modifier.align(Alignment.TopEnd).padding(12.dp))
                }
            }
            Gap(12.dp)
        }
        val mono = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace, fontSize = 12.sp, lineHeight = 17.sp)
        Text(
            String.format(java.util.Locale.ROOT,
                "gyro   %7.1f %7.1f %7.1f deg/s  %4.0f/s\naccel  %7.2f %7.2f %7.2f g      %4.0f/s\nlongest gap %.1f ms, samples %d + %d",
                Math.toDegrees(v.gyro[0].toDouble()), Math.toDegrees(v.gyro[1].toDouble()), Math.toDegrees(v.gyro[2].toDouble()),
                v.gyroRate, v.accel[0], v.accel[1], v.accel[2], v.accelRate, v.worstGapMs, v.gyroSamples, v.accelSamples),
            style = mono, color = Night.TextDim,
        )
        Gap(6.dp)
        Text(stringResource(R.string.Gyroscope, v.gyroscope ?: stringResource(R.string.NoneListed)), style = mono, color = Night.TextFaint)
        Text(stringResource(R.string.Accelerometer, v.accelerometer ?: stringResource(R.string.NoneListed)), style = mono, color = Night.TextFaint)
    }
}
