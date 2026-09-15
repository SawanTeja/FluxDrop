package dev.fluxdrop.app.ui.screens

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Environment
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import dev.fluxdrop.app.bridge.DeviceFoundCallback
import dev.fluxdrop.app.bridge.FluxDropCore
import dev.fluxdrop.app.bridge.SessionCallbacks
import dev.fluxdrop.app.ui.components.TransferProgress
import dev.fluxdrop.app.ui.components.TransferState
import dev.fluxdrop.app.ui.state.SessionState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

data class DiscoveredDevice(val ip: String, val port: Int, val sessionId: Long)

private const val PREFS_NAME = "fluxdrop_prefs"
private const val KEY_SAVE_DIR = "receive_save_dir"

@Composable
fun ReceiveScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()
    val prefs = remember { context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE) }

    val defaultDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).absolutePath

    var devices by remember { mutableStateOf(listOf<DiscoveredDevice>()) }
    var status by remember { mutableStateOf("Scanning for devices...") }
    var selectedDevice by remember { mutableStateOf<DiscoveredDevice?>(null) }
    var pin by remember { mutableStateOf("") }
    var transferState by remember { mutableStateOf(TransferState()) }
    var saveDir by remember { mutableStateOf(prefs.getString(KEY_SAVE_DIR, defaultDir) ?: defaultDir) }
    var incomingOffer by remember { mutableStateOf<IncomingFileOffer?>(null) }
    var sessionEstablished by remember { mutableStateOf(false) }
    var peerInfo by remember { mutableStateOf("") }

    // For sending files back in session
    var selectedFilesToSend by remember { mutableStateOf<List<Uri>>(emptyList()) }

    var manualIp by remember { mutableStateOf("") }
    var manualPort by remember { mutableStateOf("") }
    var showManualConnect by remember { mutableStateOf(false) }

    // File picker for sending files back
    val filePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) {
            selectedFilesToSend = selectedFilesToSend + uris
        }
    }

    // Folder picker for save directory
    val folderPickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        uri?.let {
            val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            context.contentResolver.takePersistableUriPermission(it, flags)
            val docId = android.provider.DocumentsContract.getTreeDocumentId(it)
            val path = if (docId.startsWith("primary:")) {
                Environment.getExternalStorageDirectory().absolutePath + "/" + docId.removePrefix("primary:")
            } else {
                it.path?.replace("/tree/primary:", Environment.getExternalStorageDirectory().absolutePath + "/")
                    ?: defaultDir
            }
            saveDir = path
            prefs.edit().putString(KEY_SAVE_DIR, path).apply()
        }
    }

    // Discovery management
    LaunchedEffect(selectedDevice, sessionEstablished) {
        if (!sessionEstablished && selectedDevice == null) {
            FluxDropCore.stopDiscovery()
            devices = emptyList()
            FluxDropCore.startDiscovery(482913, object : DeviceFoundCallback {
                override fun onDeviceFound(ip: String, port: Int, sessionId: Long) {
                    val newDevice = DiscoveredDevice(ip, port, sessionId)
                    if (!devices.contains(newDevice)) {
                        devices = devices + newDevice
                    }
                }
            })
        }
    }

    DisposableEffect(Unit) {
        onDispose {
            FluxDropCore.stopDiscovery()
            FluxDropCore.sessionDisconnect()
        }
    }

    // Incoming file offer dialog
    if (incomingOffer != null) {
        AlertDialog(
            onDismissRequest = {},
            title = { Text(text = "Incoming File") },
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

    // Helper to start joining a device
    fun joinDevice(ip: String, port: Int, pinStr: String) {
        FluxDropCore.stopDiscovery()
        File(saveDir).mkdirs()
        status = "Connecting..."
        SessionState.isSessionActive.value = true

        FluxDropCore.sessionJoin(ip, port, pinStr, saveDir, object : SessionCallbacks {
            override fun onReady(ip: String, port: Int, pin: Int) {}
            override fun onSessionEstablished(peerIp: String, peerPort: Int, role: Int) {
                sessionEstablished = true
                peerInfo = peerIp
                status = "Session active"
            }
            override fun onSessionEnded() {
                sessionEstablished = false
                selectedDevice = null
                peerInfo = ""
                pin = ""
                status = "Session ended"
                transferState = TransferState()
                selectedFilesToSend = emptyList()
                SessionState.isSessionActive.value = false
                SessionState.autoAcceptIncoming = false
                SessionState.currentTransferDeferred?.complete(Unit)
            }
            override fun onStatus(message: String) { status = message }
            override fun onError(error: String) {
                status = "Error: $error"
                sessionEstablished = false
                selectedDevice = null
                SessionState.isSessionActive.value = false
                SessionState.currentTransferDeferred?.complete(Unit)
            }
            override fun onFileOffer(filename: String, fileSize: Long): Boolean {
                if (SessionState.autoAcceptIncoming) return true

                val latch = CountDownLatch(1)
                var accepted = false
                incomingOffer = IncomingFileOffer(filename, fileSize) { response ->
                    accepted = response
                    if (response) SessionState.autoAcceptIncoming = true
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
                SessionState.currentTransferDeferred?.complete(Unit)
            }
        })
    }

    Column(
        modifier = modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        if (!sessionEstablished) {
            if (selectedDevice == null) {
                // ── Discovery view ──
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                    Text("📡", fontSize = 24.sp)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Nearby Devices", style = MaterialTheme.typography.titleLarge, color = Color.White, fontWeight = FontWeight.Bold)
                }
                Spacer(modifier = Modifier.height(4.dp))
                Text("Scanning for FluxDrop hosts on your network...", color = Color.LightGray, modifier = Modifier.fillMaxWidth())

                Spacer(modifier = Modifier.height(16.dp))

                // Save location
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("📁", fontSize = 18.sp)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Save to: ", color = Color.LightGray)
                    Text(
                        text = saveDir,
                        color = Color.Gray,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    OutlinedButton(
                        onClick = { folderPickerLauncher.launch(null) },
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = Color.White),
                        border = androidx.compose.foundation.BorderStroke(1.dp, dev.fluxdrop.app.ui.theme.FluxPrimary),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text("Change")
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                // Device list
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .weight(1f)
                        .background(dev.fluxdrop.app.ui.theme.FluxBoxBackground, shape = RoundedCornerShape(8.dp))
                        .border(1.dp, dev.fluxdrop.app.ui.theme.FluxBorder, RoundedCornerShape(8.dp))
                        .padding(8.dp)
                ) {
                    if (devices.isEmpty()) {
                        Text("No devices found", color = Color.LightGray, modifier = Modifier.align(Alignment.Center))
                    } else {
                        LazyColumn(modifier = Modifier.fillMaxSize()) {
                            items(devices) { device ->
                                Card(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(vertical = 4.dp)
                                        .clickable { selectedDevice = device },
                                    colors = CardDefaults.cardColors(containerColor = dev.fluxdrop.app.ui.theme.FluxAccent),
                                    elevation = CardDefaults.cardElevation(defaultElevation = 0.dp)
                                ) {
                                    Column(modifier = Modifier.padding(16.dp)) {
                                        Text("💻 Device at ${device.ip}", style = MaterialTheme.typography.titleMedium, color = Color.White)
                                        Text("Port: ${device.port}", style = MaterialTheme.typography.bodyMedium, color = Color.LightGray)
                                    }
                                }
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                // Manual connection
                Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text("💡 Can't find your device?", color = Color.LightGray)
                }
                Spacer(modifier = Modifier.height(8.dp))

                if (!showManualConnect) {
                    TextButton(onClick = { showManualConnect = true }) {
                        Text("🔗 Connect by IP", color = Color.White)
                    }
                } else {
                    Box(
                        modifier = Modifier.fillMaxWidth().background(dev.fluxdrop.app.ui.theme.FluxBoxBackground, shape = RoundedCornerShape(8.dp)).padding(16.dp)
                    ) {
                        Column {
                            OutlinedTextField(
                                value = manualIp,
                                onValueChange = { manualIp = it },
                                label = { Text("IP address", color = Color.LightGray) },
                                colors = OutlinedTextFieldDefaults.colors(
                                    focusedTextColor = Color.White,
                                    unfocusedTextColor = Color.White,
                                    focusedBorderColor = dev.fluxdrop.app.ui.theme.FluxPrimary,
                                    unfocusedBorderColor = Color.Gray
                                ),
                                singleLine = true,
                                modifier = Modifier.fillMaxWidth()
                            )
                            Spacer(modifier = Modifier.height(8.dp))
                            OutlinedTextField(
                                value = manualPort,
                                onValueChange = { manualPort = it },
                                label = { Text("Port", color = Color.LightGray) },
                                colors = OutlinedTextFieldDefaults.colors(
                                    focusedTextColor = Color.White,
                                    unfocusedTextColor = Color.White,
                                    focusedBorderColor = dev.fluxdrop.app.ui.theme.FluxPrimary,
                                    unfocusedBorderColor = Color.Gray
                                ),
                                singleLine = true,
                                modifier = Modifier.fillMaxWidth()
                            )
                            Spacer(modifier = Modifier.height(12.dp))
                            Row {
                                Button(
                                    onClick = {
                                        val port = manualPort.toIntOrNull() ?: 0
                                        if (manualIp.isNotBlank() && port > 0) {
                                            selectedDevice = DiscoveredDevice(manualIp, port, 0L)
                                        }
                                    },
                                    enabled = manualIp.isNotBlank() && (manualPort.toIntOrNull() ?: 0) > 0,
                                    colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxAccent)
                                ) {
                                    Text("Connect", color = Color.White)
                                }
                                Spacer(modifier = Modifier.width(8.dp))
                                OutlinedButton(onClick = {
                                    showManualConnect = false
                                    manualIp = ""
                                    manualPort = ""
                                }, border = androidx.compose.foundation.BorderStroke(1.dp, dev.fluxdrop.app.ui.theme.FluxPrimary)) {
                                    Text("Cancel", color = Color.White)
                                }
                            }
                        }
                    }
                }

            } else {
                // ── PIN entry for selected device ──
                Text("Connect to ${selectedDevice!!.ip}", style = MaterialTheme.typography.titleLarge, color = Color.White)
                Spacer(modifier = Modifier.height(16.dp))

                OutlinedTextField(
                    value = pin,
                    onValueChange = { pin = it },
                    label = { Text("Enter PIN", color = Color.LightGray) },
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedTextColor = Color.White,
                        unfocusedTextColor = Color.White,
                        focusedBorderColor = dev.fluxdrop.app.ui.theme.FluxPrimary,
                        unfocusedBorderColor = Color.Gray
                    ),
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
                Spacer(modifier = Modifier.height(16.dp))

                Row {
                    Button(
                        onClick = { joinDevice(selectedDevice!!.ip, selectedDevice!!.port, pin) },
                        colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxAccent)
                    ) {
                        Text("Join Session", color = Color.White)
                    }

                    Spacer(modifier = Modifier.width(8.dp))

                    OutlinedButton(onClick = {
                        selectedDevice = null
                        pin = ""
                        showManualConnect = false
                    }, border = androidx.compose.foundation.BorderStroke(1.dp, dev.fluxdrop.app.ui.theme.FluxPrimary)) {
                        Text("Back", color = Color.White)
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))
                Text("Status: $status", color = Color.LightGray)
            }

        } else {
            // ── In-session view ──
            Text("Connected: $peerInfo", color = Color(0xFF4CAF50), fontSize = 16.sp, fontWeight = FontWeight.Bold)
            Spacer(modifier = Modifier.height(12.dp))

            // File picker for sending back
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(80.dp)
                    .background(dev.fluxdrop.app.ui.theme.FluxBoxBackground, shape = RoundedCornerShape(12.dp))
                    .border(2.dp, dev.fluxdrop.app.ui.theme.FluxBorder, RoundedCornerShape(12.dp))
                    .clickable { filePicker.launch(arrayOf("*/*")) },
                contentAlignment = Alignment.Center
            ) {
                Text("📄 Tap to select files to send back", color = Color.White, fontSize = 15.sp)
            }

            if (selectedFilesToSend.isNotEmpty()) {
                Spacer(modifier = Modifier.height(8.dp))
                Text("${selectedFilesToSend.size} file(s) selected", color = Color.LightGray)
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
                if (selectedFilesToSend.isNotEmpty()) {
                    Button(
                        onClick = {
                            coroutineScope.launch {
                                val queue = selectedFilesToSend.toList()
                                if (queue.isEmpty()) return@launch

                                for ((index, uri) in queue.withIndex()) {
                                    if (!sessionEstablished) break
                                    status = "Sending file ${index + 1} of ${queue.size}..."
                                    transferState = TransferState()

                                    val tempPath = withContext(Dispatchers.IO) { copyToCacheReceive(context, uri) }
                                    if (tempPath.isEmpty()) continue

                                    SessionState.currentTransferDeferred = kotlinx.coroutines.CompletableDeferred()
                                    FluxDropCore.sessionSendFiles(arrayOf(tempPath))

                                    try {
                                        SessionState.currentTransferDeferred?.await()
                                    } catch (e: Exception) {
                                        // Cancelled or error
                                    }

                                    withContext(Dispatchers.IO) {
                                        File(tempPath).delete()
                                    }
                                }
                                if (sessionEstablished) {
                                    status = "All files sent"
                                    transferState = TransferState()
                                    selectedFilesToSend = emptyList()
                                }
                            }
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = Color.White),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        Text("Send Files", color = Color.DarkGray)
                    }

                    Spacer(modifier = Modifier.width(12.dp))
                }

                Button(
                    onClick = {
                        FluxDropCore.sessionDisconnect()
                        sessionEstablished = false
                        selectedDevice = null
                        peerInfo = ""
                        pin = ""
                        status = "Scanning for devices..."
                        transferState = TransferState()
                        selectedFilesToSend = emptyList()
                        SessionState.isSessionActive.value = false
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

private fun copyToCacheReceive(context: Context, uri: Uri): String {
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
