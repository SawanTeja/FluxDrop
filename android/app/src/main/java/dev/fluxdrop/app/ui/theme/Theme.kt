package dev.fluxdrop.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val FluxBackground = Color(0xFF1E213A)
val FluxSurface = Color(0xFF191C32)
val FluxPrimary = Color(0xFF5A3E84)
val FluxAccent = Color(0xFF3B82F6)
val FluxRed = Color(0xFFEF4444)
val FluxBoxBackground = Color(0xFF182F52)
val FluxBorder = Color(0xFF4A3482)

private val DarkColorScheme = darkColorScheme(
    primary = FluxPrimary,
    secondary = FluxAccent,
    background = FluxBackground,
    surface = FluxSurface,
    onBackground = Color.White,
    onSurface = Color.White
)

@Composable
fun FluxDropTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColorScheme, 
        content = content
    )
}

