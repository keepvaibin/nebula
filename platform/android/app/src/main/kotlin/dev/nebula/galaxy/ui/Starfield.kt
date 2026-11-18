// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy.ui

import android.animation.ValueAnimator
import android.os.SystemClock
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlin.math.PI
import kotlin.math.floor
import kotlin.math.sin
import kotlin.random.Random

private class Star(val x: Float, val y: Float, val size: Float, val phase: Float, val speed: Float, val tint: Color)

/**
 * The sky's motion as a function of the seconds it has run: every term is continuous in time, so nothing jumps however
 * long the launcher stays open (P-101).
 */
internal object Sky {
    /** Redraws a second while it moves: the stars drift a pixel in several seconds, so more is only heat. */
    const val RATE = 30

    /** How far across the sky a star has drifted (leftwards, wrapping round), from its place `start`. */
    fun starX(start: Float, speed: Float, seconds: Double): Float {
        val x = start - seconds * speed * DRIFT
        return (x - floor(x)).toFloat()
    }

    /** A star's brightness, 0.35 to 1, twinkling at its own pace. */
    fun twinkle(phase: Float, speed: Float, seconds: Double): Float =
        (0.35 + 0.65 * (0.5 + 0.5 * sin(phase + seconds * TWINKLE * speed))).toFloat()

    /** The nebula glows' slow sway, one turn in [GLOW_SECONDS]. */
    fun glowTurn(seconds: Double): Float = (2.0 * PI * (seconds / GLOW_SECONDS - floor(seconds / GLOW_SECONDS))).toFloat()

    private const val DRIFT = 0.25 / 120.0  // a quarter of the sky in two minutes at speed 1
    private const val TWINKLE = 1.0  // radians a second at speed 1
    private const val GLOW_SECONDS = 120.0
}

/**
 * The launcher's sky: two slow nebula glows and a few hundred twinkling stars drifting past. It moves at [Sky.RATE]
 * pictures a second, and is still when `moving` is false (the long import) or the system's animations are off.
 */
@Composable
fun Starfield(modifier: Modifier = Modifier, moving: Boolean = true) {
    val stars = remember {
        val random = Random(20261018)
        val tints = listOf(Color.White, Color(0xFFE6E0FF), Color(0xFFD6E6FF), Color(0xFFFFF1D6))
        List(220) {
            Star(
                x = random.nextFloat(),
                y = random.nextFloat(),
                size = 0.6f + random.nextFloat() * random.nextFloat() * 2.2f,
                phase = random.nextFloat() * 2f * PI.toFloat(),
                speed = 0.4f + random.nextFloat() * 1.4f,
                tint = tints[random.nextInt(tints.size)],
            )
        }
    }
    val start = remember { SystemClock.uptimeMillis() }
    var seconds by remember { mutableDoubleStateOf(0.0) }
    LaunchedEffect(moving) {
        if (!moving || !ValueAnimator.areAnimatorsEnabled()) return@LaunchedEffect
        while (isActive) {
            seconds = (SystemClock.uptimeMillis() - start) / 1000.0
            delay(1000L / Sky.RATE)
        }
    }
    Canvas(modifier.fillMaxSize()) {
        drawRect(Brush.verticalGradient(listOf(Night.SkyHigh, Night.Sky, Night.Sky)))
        val t = Sky.glowTurn(seconds)
        val glowA = Offset(size.width * (0.72f + 0.05f * sin(t)), size.height * (0.22f + 0.04f * sin(t * 2f)))
        drawCircle(
            Brush.radialGradient(listOf(Color(0x552B1F66), Color(0x00000000)), glowA, size.maxDimension * 0.55f),
            radius = size.maxDimension * 0.55f,
            center = glowA,
        )
        val glowB = Offset(size.width * (0.18f + 0.04f * sin(t * 3f)), size.height * (0.78f + 0.03f * sin(t)))
        drawCircle(
            Brush.radialGradient(listOf(Color(0x3315406B), Color(0x00000000)), glowB, size.maxDimension * 0.5f),
            radius = size.maxDimension * 0.5f,
            center = glowB,
        )
        for (star in stars) {
            // Each drifts left at its own pace (nearer stars faster) and wraps round.
            val x = Sky.starX(star.x, star.speed, seconds)
            val alpha = Sky.twinkle(star.phase, star.speed, seconds)
            drawCircle(star.tint.copy(alpha = alpha), radius = star.size * density, center = Offset(x * size.width, star.y * size.height))
        }
    }
}
