package dev.fluxdrop.app.ui.screens

import android.content.Context
import android.content.Intent
import android.os.Environment
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.fluxdrop.app.bridge.DeviceFoundCallback
import dev.fluxdrop.app.bridge.FluxDropCore
import dev.fluxdrop.app.bridge.ClientCallbacks
import dev.fluxdrop.app.ui.components.TransferProgress
import dev.fluxdrop.app.ui.components.TransferState
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

data class DiscoveredDevice(val ip: String, val port: Int, val sessionId: Long)

data class IncomingFileRequest(
    val filename: String,
    val fileSize: Long,
    val onResponse: (Boolean) -> Unit
)

private const val PREFS_NAME = "fluxdrop_prefs"
private const val KEY_SAVE_DIR = "receive_save_dir"

@Composable
fun ReceiveScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val prefs = remember { context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE) }

    val defaultDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).absolutePath

    var devices by remember { mutableStateOf(listOf<DiscoveredDevice>()) }
    var status by remember { mutableStateOf("Scanning for devices...") }
    var selectedDevice by remember { mutableStateOf<DiscoveredDevice?>(null) }
    var pin by remember { mutableStateOf("") }
    var transferState by remember { mutableStateOf(TransferState()) }
    var saveDir by remember { mutableStateOf(prefs.getString(KEY_SAVE_DIR, defaultDir) ?: defaultDir) }
    var incomingRequest by remember { mutableStateOf<IncomingFileRequest?>(null) }

    var manualIp by remember { mutableStateOf("") }
    var manualPort by remember { mutableStateOf("") }
    var showManualConnect by remember { mutableStateOf(false) }

    // Folder picker launcher
    val folderPickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        uri?.let {
            // Take persistable permission so we can access the folder across app restarts
            val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            context.contentResolver.takePersistableUriPermission(it, flags)

            // Convert content URI to a filesystem path for the native layer
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

    LaunchedEffect(selectedDevice) {
        FluxDropCore.stopDiscovery()
        if (selectedDevice == null) {
            devices = emptyList()
            FluxDropCore.startDiscovery(482913, object : DeviceFoundCallback {
                override fun onDeviceFound(ip: String, port: Int, sessionId: Long) {
                    val newDevice = DiscoveredDevice(ip, port, sessionId)
                    if (!devices.contains(newDevice)) {
                        devices = devices + newDevice
                    }
                }
            })
        } else {
            status = "Ready to connect to ${selectedDevice?.ip}"
        }
    }

    DisposableEffect(Unit) {
        onDispose {
            FluxDropCore.stopDiscovery()
            FluxDropCore.requestCancelClient()
        }
    }

    if (incomingRequest != null) {
        AlertDialog(
            onDismissRequest = {
                // Ignore tapping outside - user must explicitly Accept or Reject
            },
            title = {
                Text(text = "Incoming File")
            },
            text = {
                val sizeMb = incomingRequest!!.fileSize.toFloat() / (1024f * 1024f)
                Text("Accept incoming file?\n\n${incomingRequest!!.filename}\n${"%.1f".format(sizeMb)} MB")
            },
            confirmButton = {
                Button(
                    onClick = {
                        incomingRequest?.onResponse?.invoke(true)
                        incomingRequest = null
                    }
                ) {
                    Text("Accept")
                }
            },
            dismissButton = {
                OutlinedButton(
                    onClick = {
                        incomingRequest?.onResponse?.invoke(false)
                        incomingRequest = null
                    }
                ) {
                    Text("Reject")
                }
            }
        )
    }

    Column(
        modifier = modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // ── Save location picker (always visible) ──
        Card(
            modifier = Modifier.fillMaxWidth().padding(bottom = 12.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant
            )
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    "Save to",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Spacer(modifier = Modifier.height(4.dp))
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        text = saveDir,
                        style = MaterialTheme.typography.bodyMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    OutlinedButton(onClick = { folderPickerLauncher.launch(null) }) {
                        Text("Change")
                    }
                }
            }
        }

        if (selectedDevice == null) {
            Text("Status: $status")
            Spacer(modifier = Modifier.height(16.dp))
            
            LazyColumn(modifier = Modifier.weight(1f)) {
                items(devices) { device ->
                    Card(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(vertical = 4.dp)
                            .clickable { selectedDevice = device },
                        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp)
                    ) {
                        Column(modifier = Modifier.padding(16.dp)) {
                            Text("Device at ${device.ip}", style = MaterialTheme.typography.titleMedium)
                            Text("Port: ${device.port}", style = MaterialTheme.typography.bodyMedium)
                        }
                    }
                }
            }

            // ── Manual "Connect by IP" fallback ──
            Spacer(modifier = Modifier.height(12.dp))
            Text(
                "💡 Can't find your device?",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(4.dp))

            if (!showManualConnect) {
                OutlinedButton(onClick = { showManualConnect = true }) {
                    Text("🔗 Connect by IP")
                }
            } else {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant
                    )
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        OutlinedTextField(
                            value = manualIp,
                            onValueChange = { manualIp = it },
                            label = { Text("IP address") },
                            placeholder = { Text("e.g. 192.168.43.1") },
                            singleLine = true,
                            modifier = Modifier.fillMaxWidth()
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        OutlinedTextField(
                            value = manualPort,
                            onValueChange = { manualPort = it },
                            label = { Text("Port") },
                            placeholder = { Text("Shown on sender") },
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
                                enabled = manualIp.isNotBlank() && (manualPort.toIntOrNull() ?: 0) > 0
                            ) {
                                Text("Connect")
                            }
                            Spacer(modifier = Modifier.width(8.dp))
                            OutlinedButton(onClick = {
                                showManualConnect = false
                                manualIp = ""
                                manualPort = ""
                            }) {
                                Text("Cancel")
                            }
                        }
                    }
                }
            }
        } else {
            Text("Connecting to ${selectedDevice?.ip}")
            Spacer(modifier = Modifier.height(16.dp))
            
            OutlinedTextField(
                value = pin,
                onValueChange = { pin = it },
                label = { Text("Enter PIN") },
                singleLine = true
            )
            
            Spacer(modifier = Modifier.height(16.dp))
            Text("Status: $status")

            // ── Transfer progress (desktop-style) ──
            if (transferState.progress > 0f) {
                Spacer(modifier = Modifier.height(12.dp))
                TransferProgress(state = transferState)
            }

            Spacer(modifier = Modifier.height(16.dp))

            Row {
                Button(onClick = {
                    // Ensure save directory exists
                    File(saveDir).mkdirs()
                    FluxDropCore.connect(selectedDevice!!.ip, selectedDevice!!.port, pin, saveDir, object : ClientCallbacks {
                        override fun onStatus(message: String) { status = message }
                        override fun onError(error: String) { status = "Error: $error" }
                        override fun onFileRequest(filename: String, fileSize: Long): Boolean {
                            val latch = CountDownLatch(1)
                            var accepted = false
                            
                            incomingRequest = IncomingFileRequest(filename, fileSize) { response ->
                                accepted = response
                                latch.countDown()
                            }
                            
                            try {
                                while (latch.count > 0 && selectedDevice != null) {
                                    latch.await(200, TimeUnit.MILLISECONDS)
                                }
                                if (selectedDevice == null) {
                                    return false
                                }
                            } catch (e: InterruptedException) {
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
                                status = "Receiving..."
                            )
                        }
                        override fun onComplete() {
                            status = "Transfer Complete ✅"
                            transferState = transferState.copy(
                                progress = 1f,
                                status = "All files received!"
                            )
                        }
                    })
                }) {
                    Text("Connect")
                }
                
                Spacer(modifier = Modifier.width(8.dp))
                
                Button(onClick = {
                    FluxDropCore.requestCancelClient()
                    selectedDevice = null
                    status = "Scanning for devices..."
                    pin = ""
                    transferState = TransferState()
                    incomingRequest = null
                }) {
                    Text("Cancel")
                }
            }
        }
    }
}
