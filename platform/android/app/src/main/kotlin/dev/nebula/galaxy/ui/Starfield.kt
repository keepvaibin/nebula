// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import kotlin.math.PI
import kotlin.math.sin
import kotlin.random.Random

private class Star(val x: Float, val y: Float, val size: Float, val phase: Float, val speed: Float, val tint: Color)

/** The launcher's sky: two slow nebula glows and a few hundred twinkling stars drifting past. */
@Composable
fun Starfield(modifier: Modifier = Modifier) {
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
    val transition = rememberInfiniteTransition(label = "sky")
    val time by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(120_000, easing = LinearEasing), RepeatMode.Restart),
        label = "time",
    )
    Canvas(modifier.fillMaxSize()) {
        drawRect(Brush.verticalGradient(listOf(Night.SkyHigh, Night.Sky, Night.Sky)))
        val t = time * 2f * PI.toFloat()
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
        val twinkle = time * 120f
        for (star in stars) {
            // Each drifts left at its own pace (nearer stars faster) and wraps round.
            val x = ((star.x - time * star.speed * 0.25f) % 1f + 1f) % 1f
            val alpha = 0.35f + 0.65f * (0.5f + 0.5f * sin(star.phase + twinkle * star.speed))
            drawCircle(star.tint.copy(alpha = alpha), radius = star.size * density, center = Offset(x * size.width, star.y * size.height))
        }
    }
}
