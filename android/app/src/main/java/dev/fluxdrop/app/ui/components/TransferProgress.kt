package dev.fluxdrop.app.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Data class holding the current transfer state, matching the desktop's progress display.
 */
data class TransferState(
    val progress: Float = 0f,          // 0.0 – 1.0
    val filename: String = "",
    val speedMbps: Double = 0.0,
    val transferred: Long = 0L,
    val total: Long = 0L,
    val status: String = ""
)

/**
 * A rich transfer progress card that mirrors the desktop FluxDrop progress UI.
 *
 * Shows:
 *  - Current filename being transferred
 *  - Gradient progress bar (purple ➜ red, matching desktop CSS)
 *  - Percentage, speed, and transferred/total sizes
 *  - Status text
 */
@Composable
fun TransferProgress(
    state: TransferState,
    modifier: Modifier = Modifier
) {
    val animatedProgress by animateFloatAsState(
        targetValue = state.progress.coerceIn(0f, 1f),
        animationSpec = tween(durationMillis = 300),
        label = "progress"
    )

    val pct = (animatedProgress * 100).toInt()

    Surface(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        color = Color(0xFF16213E),       // matches desktop .section-box
        tonalElevation = 2.dp
    ) {
        Column(modifier = Modifier.padding(16.dp)) {

            // ── Filename ────────────────────────────────────────
            if (state.filename.isNotEmpty()) {
                Text(
                    text = state.filename,
                    style = MaterialTheme.typography.titleSmall,
                    color = Color(0xFFE0E0E0),
                    fontWeight = FontWeight.Bold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Spacer(modifier = Modifier.height(8.dp))
            }

            // ── Gradient progress bar ───────────────────────────
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(10.dp)
            ) {
                Canvas(modifier = Modifier.matchParentSize()) {
                    // Track
                    drawRoundRect(
                        color = Color(0xFF1A1A2E),
                        cornerRadius = CornerRadius(6.dp.toPx()),
                        size = size
                    )
                    // Fill (gradient matching desktop: #533483 ➜ #e94560)
                    if (animatedProgress > 0f) {
                        drawRoundRect(
                            brush = Brush.horizontalGradient(
                                colors = listOf(Color(0xFF533483), Color(0xFFE94560))
                            ),
                            cornerRadius = CornerRadius(6.dp.toPx()),
                            size = Size(size.width * animatedProgress, size.height)
                        )
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            // ── Stats row: percentage | speed | size ───────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                // Percentage
                Text(
                    text = "$pct%",
                    style = MaterialTheme.typography.bodyMedium,
                    color = Color(0xFFE94560),
                    fontWeight = FontWeight.Bold,
                    fontSize = 15.sp
                )

                // Speed
                if (state.speedMbps > 0.0) {
                    Text(
                        text = "%.1f MB/s".format(state.speedMbps),
                        style = MaterialTheme.typography.bodySmall,
                        color = Color(0xFFA0A0B0)
                    )
                }

                // Transferred / Total
                if (state.total > 0) {
                    Text(
                        text = "${formatSize(state.transferred)} / ${formatSize(state.total)}",
                        style = MaterialTheme.typography.bodySmall,
                        color = Color(0xFFA0A0B0)
                    )
                }
            }

            // ── Status text ─────────────────────────────────────
            if (state.status.isNotEmpty()) {
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = state.status,
                    style = MaterialTheme.typography.bodySmall,
                    color = Color(0xFFA0A0C0)
                )
            }
        }
    }
}

/** Format bytes into a human-readable string. */
private fun formatSize(bytes: Long): String {
    if (bytes < 1024) return "$bytes B"
    val kb = bytes / 1024.0
    if (kb < 1024) return "%.1f KB".format(kb)
    val mb = kb / 1024.0
    if (mb < 1024) return "%.1f MB".format(mb)
    val gb = mb / 1024.0
    return "%.2f GB".format(gb)
}
