package dev.fluxdrop.app.ui.state

import androidx.compose.runtime.mutableStateOf

object SessionState {
    val isSessionActive = mutableStateOf(false)
    var autoAcceptIncoming = false
}
