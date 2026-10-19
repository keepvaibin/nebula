// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.nebula.galaxy.R

/** A pane of frosted glass over the sky. */
@Composable
fun GlassCard(modifier: Modifier = Modifier, padding: PaddingValues = PaddingValues(20.dp), content: @Composable ColumnScope.() -> Unit) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(28.dp),
        color = Night.Glass,
        contentColor = Color.White,
        border = BorderStroke(1.dp, Night.GlassEdge),
    ) {
        Column(Modifier.padding(padding), content = content)
    }
}

/** Nebula's mark with its glow. */
@Composable
fun Mark(size: Dp) {
    Box(contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(size)) {
            drawCircle(Brush.radialGradient(listOf(Color(0x66B9A4FF), Color(0x00000000))), radius = this.size.minDimension / 2f)
        }
        Image(painterResource(R.drawable.nebula_mark), contentDescription = null, modifier = Modifier.size(size))
    }
}

@Composable
fun PrimaryAction(text: String, icon: ImageVector?, onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true, tall: Boolean = false) {
    Button(
        onClick = onClick,
        enabled = enabled,
        modifier = modifier.heightIn(min = if (tall) 64.dp else 56.dp),
        shape = RoundedCornerShape(if (tall) 32.dp else 28.dp),
        colors = ButtonDefaults.buttonColors(containerColor = Night.Lavender, contentColor = Night.Ink),
        contentPadding = PaddingValues(horizontal = 28.dp, vertical = 12.dp),
    ) {
        if (icon != null) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(if (tall) 28.dp else 22.dp))
            Spacer(Modifier.width(10.dp))
        }
        Text(text, style = if (tall) MaterialTheme.typography.titleLarge else MaterialTheme.typography.labelLarge)
    }
}

@Composable
fun SecondaryAction(text: String, icon: ImageVector?, onClick: () -> Unit, modifier: Modifier = Modifier, ink: Color = Color.White) {
    OutlinedButton(
        onClick = onClick,
        modifier = modifier.heightIn(min = 52.dp),
        shape = RoundedCornerShape(26.dp),
        border = BorderStroke(1.dp, Night.GlassEdge),
        colors = ButtonDefaults.outlinedButtonColors(contentColor = ink),
        contentPadding = PaddingValues(horizontal = 22.dp, vertical = 10.dp),
    ) {
        if (icon != null) {
            Icon(icon, contentDescription = null, modifier = Modifier.size(20.dp))
            Spacer(Modifier.width(8.dp))
        }
        Text(text, style = MaterialTheme.typography.labelLarge)
    }
}

/** A row that opens something: icon, title, line of detail. */
@Composable
fun NavigationRow(icon: ImageVector, title: String, detail: String?, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(role = Role.Button, onClick = onClick)
            .padding(horizontal = 20.dp, vertical = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconBadge(icon)
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            if (detail != null) Text(detail, style = MaterialTheme.typography.bodyMedium, color = Night.TextDim)
        }
    }
}

@Composable
fun IconBadge(icon: ImageVector, tint: Color = Night.Lavender) {
    Box(
        Modifier
            .size(40.dp)
            .background(tint.copy(alpha = 0.14f), CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = tint, modifier = Modifier.size(22.dp))
    }
}

@Composable
fun TopBar(title: String, onBack: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Back")
        }
        Spacer(Modifier.width(4.dp))
        Text(title, style = MaterialTheme.typography.headlineSmall)
    }
}

@Composable
fun SectionTitle(text: String) {
    Text(
        text.uppercase(),
        style = MaterialTheme.typography.labelMedium,
        color = Night.Lavender,
        modifier = Modifier.padding(start = 24.dp, top = 24.dp, bottom = 8.dp),
    )
}

@Composable
fun SwitchRow(title: String, detail: String?, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(role = Role.Switch) { onChange(!checked) }
            .padding(horizontal = 20.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            if (detail != null) Text(detail, style = MaterialTheme.typography.bodyMedium, color = Night.TextDim)
        }
        Spacer(Modifier.width(16.dp))
        Switch(
            checked = checked,
            onCheckedChange = onChange,
            colors = SwitchDefaults.colors(checkedTrackColor = Night.Lavender, checkedThumbColor = Night.Ink),
        )
    }
}

@Composable
fun SliderRow(title: String, value: Float, range: ClosedFloatingPointRange<Float>, label: (Float) -> String, onChange: (Float) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(title, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
            Text(label(value), style = MaterialTheme.typography.bodyMedium.merge(Mono), color = Night.TextDim)
        }
        Slider(
            value = value,
            onValueChange = onChange,
            valueRange = range,
            colors = SliderDefaults.colors(thumbColor = Night.Lavender, activeTrackColor = Night.Lavender),
        )
    }
}

/** One choice of several, as a row with a ring that fills when chosen. */
@Composable
fun ChoiceRow(title: String, detail: String?, selected: Boolean, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(role = Role.RadioButton, onClick = onClick)
            .padding(horizontal = 20.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .size(22.dp)
                .background(if (selected) Night.Lavender else Color.Transparent, CircleShape)
                .padding(if (selected) 6.dp else 0.dp),
            contentAlignment = Alignment.Center,
        ) {
            if (selected) {
                Box(Modifier.size(10.dp).background(Night.Ink, CircleShape))
            } else {
                Canvas(Modifier.size(22.dp)) { drawCircle(Night.TextFaint, style = Stroke(2.dp.toPx())) }
            }
        }
        Spacer(Modifier.width(16.dp))
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            if (detail != null) Text(detail, style = MaterialTheme.typography.bodyMedium, color = Night.TextDim)
        }
    }
}

/** A ring that fills clockwise from the top, with whatever goes in its middle. */
@Composable
fun ProgressRing(progress: Float, size: Dp, indeterminate: Boolean, content: @Composable () -> Unit) {
    Box(Modifier.size(size), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(size)) {
            val width = 10.dp.toPx()
            val inset = width / 2f
            val arcSize = Size(this.size.width - width, this.size.height - width)
            drawArc(Color(0x1FFFFFFF), 0f, 360f, false, Offset(inset, inset), arcSize, style = Stroke(width))
            val sweep = if (indeterminate) 40f else 360f * progress.coerceIn(0f, 1f)
            if (sweep > 0f) {
                drawArc(
                    Brush.sweepGradient(listOf(Night.Blue, Night.Lavender, Night.Gold, Night.Blue)),
                    -90f, sweep, false, Offset(inset, inset), arcSize, style = Stroke(width, cap = StrokeCap.Round),
                )
            }
        }
        content()
    }
}

@Composable
fun Centered(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center, content = content)
}

@Composable
fun Lead(text: String, modifier: Modifier = Modifier) {
    Text(text, style = MaterialTheme.typography.bodyLarge, color = Night.TextDim, textAlign = TextAlign.Center, modifier = modifier)
}

@Composable
fun Gap(height: Dp) = Spacer(Modifier.height(height))
