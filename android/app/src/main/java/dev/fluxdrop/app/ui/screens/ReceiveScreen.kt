package dev.fluxdrop.app.ui.screens

import android.content.Context
import android.content.Intent
import android.os.Environment
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
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
            onDismissRequest = {},
            title = { Text(text = "Incoming File") },
            text = {
                val sizeMb = incomingRequest!!.fileSize.toFloat() / (1024f * 1024f)
                Text("Accept incoming file?\n\n${incomingRequest!!.filename}\n${"%.1f".format(sizeMb)} MB")
            },
            confirmButton = {
                Button(onClick = {
                    incomingRequest?.onResponse?.invoke(true)
                    incomingRequest = null
                }) { Text("Accept") }
            },
            dismissButton = {
                OutlinedButton(onClick = {
                    incomingRequest?.onResponse?.invoke(false)
                    incomingRequest = null
                }) { Text("Reject") }
            }
        )
    }

    Column(
        modifier = modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        if (selectedDevice == null) {
            // ── Header ──
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                Text("📡", fontSize = 24.sp)
                Spacer(modifier = Modifier.width(8.dp))
                Text("Nearby Devices", style = MaterialTheme.typography.titleLarge, color = Color.White, fontWeight = FontWeight.Bold)
            }
            Spacer(modifier = Modifier.height(4.dp))
            Text("Scanning for FluxDrop senders on your network...", color = Color.LightGray, modifier = Modifier.fillMaxWidth())
            
            Spacer(modifier = Modifier.height(16.dp))

            // ── Save Location ──
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
                    shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp)
                ) {
                    Text("Change")
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // ── Device List Box ──
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .background(dev.fluxdrop.app.ui.theme.FluxBoxBackground, shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp))
                    .border(1.dp, dev.fluxdrop.app.ui.theme.FluxBorder, androidx.compose.foundation.shape.RoundedCornerShape(8.dp))
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

            // ── Manual Connection ──
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
                    modifier = Modifier.fillMaxWidth().background(dev.fluxdrop.app.ui.theme.FluxBoxBackground, shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp)).padding(16.dp)
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
            // Connection UI
            Text("Connect to ${selectedDevice!!.ip}", style = MaterialTheme.typography.titleLarge, color = Color.White)
            Spacer(modifier = Modifier.height(16.dp))

            if (transferState.progress == 0f) {
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
                        onClick = {
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
                                        if (selectedDevice == null) return false
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
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxAccent)
                    ) {
                        Text("Connect & Receive", color = Color.White)
                    }
                    
                    Spacer(modifier = Modifier.width(8.dp))
                    
                    OutlinedButton(onClick = { 
                        FluxDropCore.requestCancelClient()
                        selectedDevice = null 
                        pin = ""
                        showManualConnect = false
                    }, border = androidx.compose.foundation.BorderStroke(1.dp, dev.fluxdrop.app.ui.theme.FluxPrimary)) {
                        Text("Cancel", color = Color.White)
                    }
                }
            } else {
                if (transferState.progress > 0f) {
                    TransferProgress(state = transferState)
                }
                Spacer(modifier = Modifier.height(16.dp))
                if (transferState.progress >= 1f || status.startsWith("Error")) {
                    Button(
                        onClick = {
                            selectedDevice = null
                            transferState = TransferState()
                            pin = ""
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxAccent)
                    ) {
                        Text("Done", color = Color.White)
                    }
                }
            }

            Spacer(modifier = Modifier.height(16.dp))
            Text(status, color = Color.LightGray)
        }
    }
}
