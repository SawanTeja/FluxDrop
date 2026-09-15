package dev.fluxdrop.app.ui.state

import androidx.compose.runtime.mutableStateOf

import kotlinx.coroutines.CompletableDeferred

object SessionState {
    val isSessionActive = mutableStateOf(false)
    var autoAcceptIncoming = false
    var currentTransferDeferred: CompletableDeferred<Unit>? = null
}
