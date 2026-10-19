// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy.ui

import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

// The night sky: deep blue-black, lavender light, a star's gold.
object Night {
    val Sky = Color(0xFF05060D)
    val SkyHigh = Color(0xFF0B0D24)
    val Lavender = Color(0xFFC9BEFF)
    val LavenderDeep = Color(0xFF8B7CF6)
    val Blue = Color(0xFF84B6FF)
    val Gold = Color(0xFFFFD27A)
    val Ink = Color(0xFF1A1533)
    // Cards: the night, nearly opaque, so the stars stay behind the words.
    val Glass = Color(0xE0101328)
    val GlassEdge = Color(0x24FFFFFF)
    val TextDim = Color(0xB3FFFFFF)
    val TextFaint = Color(0x80FFFFFF)
    val Error = Color(0xFFFFB4AB)
    val Success = Color(0xFF9EE6B8)
}

private val colors = darkColorScheme(
    primary = Night.Lavender,
    onPrimary = Night.Ink,
    primaryContainer = Color(0xFF2E2766),
    onPrimaryContainer = Color(0xFFE6E0FF),
    secondary = Night.Blue,
    onSecondary = Color(0xFF0A1A33),
    tertiary = Night.Gold,
    onTertiary = Color(0xFF2B1D00),
    background = Night.Sky,
    onBackground = Color.White,
    surface = Color(0xFF101226),
    onSurface = Color.White,
    surfaceVariant = Color(0xFF1B1E38),
    onSurfaceVariant = Night.TextDim,
    surfaceContainerHigh = Color(0xFF181A33),
    surfaceContainerHighest = Color(0xFF20233F),
    outline = Color(0x40FFFFFF),
    outlineVariant = Color(0x24FFFFFF),
    error = Night.Error,
    onError = Color(0xFF690005),
)

private val typography = Typography().run {
    copy(
        displayMedium = displayMedium.copy(fontWeight = FontWeight.Bold, letterSpacing = (-0.5).sp),
        headlineMedium = headlineMedium.copy(fontWeight = FontWeight.SemiBold),
        headlineSmall = headlineSmall.copy(fontWeight = FontWeight.SemiBold),
        titleLarge = titleLarge.copy(fontWeight = FontWeight.SemiBold),
        titleMedium = titleMedium.copy(fontWeight = FontWeight.SemiBold),
        labelLarge = labelLarge.copy(fontWeight = FontWeight.SemiBold, fontSize = 16.sp),
    )
}

@Composable
fun NebulaTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = colors, typography = typography) {
        // Text and icons over the sky are white, as on the theme's surfaces.
        CompositionLocalProvider(LocalContentColor provides Color.White, content = content)
    }
}

val Mono = TextStyle(fontFeatureSettings = "tnum")
