package dev.fluxdrop.app.ui.screens

import android.os.Environment
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.fluxdrop.app.bridge.DeviceFoundCallback
import dev.fluxdrop.app.bridge.FluxDropCore
import dev.fluxdrop.app.bridge.ClientCallbacks
import java.io.File

data class DiscoveredDevice(val ip: String, val port: Int, val sessionId: Long)

@Composable
fun ReceiveScreen(modifier: Modifier = Modifier) {
    var devices by remember { mutableStateOf(listOf<DiscoveredDevice>()) }
    var status by remember { mutableStateOf("Scanning for devices...") }
    var selectedDevice by remember { mutableStateOf<DiscoveredDevice?>(null) }
    var pin by remember { mutableStateOf("") }
    var progress by remember { mutableStateOf(0f) }

    LaunchedEffect(selectedDevice) {
        if (selectedDevice == null) {
            devices = emptyList() // Clear devices on rescan mapping
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
            FluxDropCore.requestCancelClient()
        }
    }

    Column(
        modifier = modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        if (selectedDevice == null) {
            Text("Status: $status")
            Spacer(modifier = Modifier.height(16.dp))
            
            LazyColumn {
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
            
            if (progress > 0f) {
                LinearProgressIndicator(progress = progress, modifier = Modifier.fillMaxWidth())
            }

            Spacer(modifier = Modifier.height(16.dp))

            Row {
                Button(onClick = {
                    val saveDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).absolutePath
                    FluxDropCore.connect(selectedDevice!!.ip, selectedDevice!!.port, pin, saveDir, object : ClientCallbacks {
                        override fun onStatus(message: String) { status = message }
                        override fun onError(error: String) { status = "Error: $error" }
                        override fun onFileRequest(filename: String, fileSize: Long): Boolean = true
                        override fun onProgress(filename: String, transferred: Long, total: Long, speedMbps: Double) {
                            if (total > 0) progress = transferred.toFloat() / total.toFloat()
                        }
                        override fun onComplete() {
                            status = "Transfer Complete"
                            progress = 1f
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
                    progress = 0f
                }) {
                    Text("Cancel")
                }
            }
        }
    }
}
