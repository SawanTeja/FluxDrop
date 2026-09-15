package dev.fluxdrop.app.ui.screens

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.fluxdrop.app.bridge.FluxDropCore
import dev.fluxdrop.app.bridge.SessionCallbacks
import dev.fluxdrop.app.ui.components.TransferProgress
import dev.fluxdrop.app.ui.components.TransferState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

data class IncomingFileOffer(
    val filename: String,
    val fileSize: Long,
    val onResponse: (Boolean) -> Unit
)

@Composable
fun SendScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()
    var selectedFiles by remember { mutableStateOf<List<Uri>>(emptyList()) }
    var pin by remember { mutableStateOf("") }
    var status by remember { mutableStateOf("Ready to host") }
    var transferState by remember { mutableStateOf(TransferState()) }
    var isHosting by remember { mutableStateOf(false) }
    var sessionEstablished by remember { mutableStateOf(false) }
    var peerInfo by remember { mutableStateOf("") }
    var incomingOffer by remember { mutableStateOf<IncomingFileOffer?>(null) }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) {
            selectedFiles = selectedFiles + uris
        }
    }

    DisposableEffect(Unit) {
        onDispose {
            FluxDropCore.sessionDisconnect()
        }
    }

    // Incoming file offer dialog
    if (incomingOffer != null) {
        AlertDialog(
            onDismissRequest = {},
            title = { Text("Incoming File") },
            text = {
                val sizeMb = incomingOffer!!.fileSize.toFloat() / (1024f * 1024f)
                Text("Accept incoming file?\n\n${incomingOffer!!.filename}\n${"%.1f".format(sizeMb)} MB")
            },
            confirmButton = {
                Button(onClick = {
                    incomingOffer?.onResponse?.invoke(true)
                    incomingOffer = null
                }) { Text("Accept") }
            },
            dismissButton = {
                OutlinedButton(onClick = {
                    incomingOffer?.onResponse?.invoke(false)
                    incomingOffer = null
                }) { Text("Reject") }
            }
        )
    }

    Column(
        modifier = modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        if (pin.isNotEmpty()) {
            Text("PIN: $pin", style = MaterialTheme.typography.headlineMedium, color = Color(0xFFE94560), fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(8.dp))
        }

        if (peerInfo.isNotEmpty()) {
            Text("Connected: $peerInfo", color = Color(0xFF4CAF50), fontSize = 14.sp)
            Spacer(modifier = Modifier.height(8.dp))
        }

        if (!sessionEstablished) {
            // ── Pre-session: Host button ──
            if (!isHosting) {
                Button(
                    onClick = {
                        isHosting = true
                        status = "Starting session..."
                        val downloadsDir = android.os.Environment.getExternalStoragePublicDirectory(
                            android.os.Environment.DIRECTORY_DOWNLOADS
                        ).absolutePath
                        FluxDropCore.sessionSetSaveDir(downloadsDir)
                        FluxDropCore.sessionHost(object : SessionCallbacks {
                            override fun onReady(ip: String, port: Int, newPin: Int) {
                                pin = String.format("%04d", newPin)
                                status = "Waiting on $ip:$port"
                            }
                            override fun onSessionEstablished(peerIp: String, peerPort: Int, role: Int) {
                                sessionEstablished = true
                                peerInfo = peerIp
                                status = "Session active"
                            }
                            override fun onSessionEnded() {
                                sessionEstablished = false
                                isHosting = false
                                peerInfo = ""
                                pin = ""
                                status = "Session ended"
                                transferState = TransferState()
                            }
                            override fun onStatus(message: String) { status = message }
                            override fun onError(error: String) {
                                status = "Error: $error"
                                isHosting = false
                            }
                            override fun onFileOffer(filename: String, fileSize: Long): Boolean {
                                val latch = CountDownLatch(1)
                                var accepted = false
                                incomingOffer = IncomingFileOffer(filename, fileSize) { response ->
                                    accepted = response
                                    latch.countDown()
                                }
                                try {
                                    while (latch.count > 0 && sessionEstablished) {
                                        latch.await(200, TimeUnit.MILLISECONDS)
                                    }
                                    if (!sessionEstablished) return false
                                } catch (_: InterruptedException) {
                                    return false
                                }
                                return accepted
                            }
                            override fun onProgress(filename: String, transferred: Long, total: Long, speedMbps: Double) {
                                transferState = TransferState(
                                    progress = if (total > 0) transferred.toFloat() / total.toFloat() else 0f,
                                    filename = filename,
                                    speedMbps = speedMbps,
                                    transferred = transferred,
                                    total = total,
                                    status = "Transferring..."
                                )
                            }
                            override fun onFileComplete(filename: String) {
                                status = "Completed: $filename"
                                transferState = transferState.copy(progress = 1f, status = "Done")
                            }
                        })
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxAccent),
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth().height(56.dp)
                ) {
                    Text("🖥️ Host Session", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold)
                }

                Spacer(modifier = Modifier.height(16.dp))
                Text("Status: $status", color = Color.LightGray)

            } else {
                // Waiting for guest
                Text("Status: $status", color = Color.LightGray)
                Spacer(modifier = Modifier.height(16.dp))

                Button(
                    onClick = {
                        FluxDropCore.sessionDisconnect()
                        isHosting = false
                        pin = ""
                        status = "Ready to host"
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxRed),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("Cancel", color = Color.White)
                }
            }
        } else {
            // ── In-session: file picker + send ──

            // File picker zone
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(100.dp)
                    .background(dev.fluxdrop.app.ui.theme.FluxBoxBackground, shape = RoundedCornerShape(16.dp))
                    .border(2.dp, dev.fluxdrop.app.ui.theme.FluxBorder, RoundedCornerShape(16.dp))
                    .clickable { picker.launch(arrayOf("*/*")) },
                contentAlignment = Alignment.Center
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("Tap to Select Files", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.Bold)
                    Text("to send to peer", color = Color.LightGray, fontSize = 13.sp)
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            // File list
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .background(dev.fluxdrop.app.ui.theme.FluxBoxBackground, shape = RoundedCornerShape(8.dp))
                    .border(1.dp, dev.fluxdrop.app.ui.theme.FluxBorder, RoundedCornerShape(8.dp))
                    .padding(8.dp)
            ) {
                if (selectedFiles.isEmpty()) {
                    Text("No files selected", color = Color.LightGray, modifier = Modifier.align(Alignment.Center))
                } else {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(selectedFiles.size) { index ->
                            Text("📄 File ${index + 1}", color = Color.White, modifier = Modifier.padding(4.dp))
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(12.dp))

            if (transferState.progress > 0f) {
                TransferProgress(state = transferState)
                Spacer(modifier = Modifier.height(12.dp))
            }

            Text("Status: $status", color = Color.LightGray)
            Spacer(modifier = Modifier.height(12.dp))

            // Action buttons
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.Center
            ) {
                Button(
                    onClick = {
                        coroutineScope.launch {
                            status = "Preparing files..."
                            val paths = withContext(Dispatchers.IO) {
                                try {
                                    selectedFiles.map { uri -> copyToCache(context, uri) }
                                } catch (e: Exception) {
                                    emptyList<String>()
                                }
                            }
                            if (paths.isEmpty()) {
                                status = "No files to send"
                                return@launch
                            }
                            status = "Sending ${paths.size} file(s)..."
                            transferState = TransferState()
                            FluxDropCore.sessionSendFiles(paths.toTypedArray())
                        }
                    },
                    enabled = selectedFiles.isNotEmpty(),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color.White,
                        disabledContainerColor = Color.Gray
                    ),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("Send Files", color = if (selectedFiles.isNotEmpty()) Color.DarkGray else Color.LightGray)
                }

                Spacer(modifier = Modifier.width(12.dp))

                Button(
                    onClick = {
                        FluxDropCore.sessionDisconnect()
                        sessionEstablished = false
                        isHosting = false
                        peerInfo = ""
                        pin = ""
                        status = "Ready to host"
                        transferState = TransferState()
                        selectedFiles = emptyList()
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxRed),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("Disconnect", color = Color.White)
                }
            }
        }
    }
}

private fun copyToCache(context: Context, uri: Uri): String {
    val cursor = context.contentResolver.query(uri, null, null, null, null)
    var name = "temp_file"
    if (cursor != null && cursor.moveToFirst()) {
        val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
        if (nameIndex != -1) {
            name = cursor.getString(nameIndex)
        }
        cursor.close()
    }
    val cacheFile = File(context.cacheDir, name)
    context.contentResolver.openInputStream(uri)?.use { input ->
        FileOutputStream(cacheFile).use { output ->
            input.copyTo(output)
        }
    }
    return cacheFile.absolutePath
}
