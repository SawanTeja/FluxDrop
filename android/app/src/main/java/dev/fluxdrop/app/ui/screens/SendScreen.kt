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
import dev.fluxdrop.app.bridge.ServerCallbacks
import dev.fluxdrop.app.ui.components.TransferProgress
import dev.fluxdrop.app.ui.components.TransferState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

@Composable
fun SendScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()
    var selectedFiles by remember { mutableStateOf<List<Uri>>(emptyList()) }
    var pin by remember { mutableStateOf("") }
    var status by remember { mutableStateOf("Ready to send") }
    var transferState by remember { mutableStateOf(TransferState()) }
    var isSharing by remember { mutableStateOf(false) }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) {
            selectedFiles = uris
        }
    }

    val folderPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            selectedFiles = selectedFiles + uri
        }
    }

    Column(
        modifier = modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        if (pin.isNotEmpty()) {
            Text("Room PIN: $pin", style = MaterialTheme.typography.headlineMedium, color = Color.White)
            Spacer(modifier = Modifier.height(16.dp))
        }

        // ── Drag & Drop Zone ──
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(120.dp)
                .background(dev.fluxdrop.app.ui.theme.FluxBoxBackground, shape = androidx.compose.foundation.shape.RoundedCornerShape(16.dp))
                .border(2.dp, dev.fluxdrop.app.ui.theme.FluxBorder, androidx.compose.foundation.shape.RoundedCornerShape(16.dp))
                .clickable(enabled = !isSharing) { picker.launch(arrayOf("*/*")) },
            contentAlignment = Alignment.Center
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text("Tap to Select Files", color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                Text("or use the buttons below", color = Color.LightGray, fontSize = 14.sp)
            }
        }

        Spacer(modifier = Modifier.height(16.dp))

        // ── Action Buttons ──
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.Center
        ) {
            Button(
                onClick = { picker.launch(arrayOf("*/*")) },
                colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxAccent),
                shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp),
                enabled = !isSharing,
                modifier = Modifier.padding(end = 8.dp)
            ) {
                Text("📄 Choose Files", color = Color.White)
            }
            
            Button(
                onClick = { folderPicker.launch(null) },
                colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxAccent),
                shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp),
                enabled = !isSharing
            ) {
                Text("📁 Choose Folder", color = Color.White)
            }
        }
        
        Spacer(modifier = Modifier.height(16.dp))

        // ── File List Box ──
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .background(dev.fluxdrop.app.ui.theme.FluxBoxBackground, shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp))
                .border(1.dp, dev.fluxdrop.app.ui.theme.FluxBorder, androidx.compose.foundation.shape.RoundedCornerShape(8.dp))
                .padding(8.dp)
        ) {
            if (selectedFiles.isEmpty()) {
                Text("No files selected", color = Color.LightGray, modifier = Modifier.align(Alignment.Center))
            } else {
                LazyColumn(modifier = Modifier.fillMaxSize()) {
                    items(selectedFiles.size) { index ->
                        Text("📄 File $($index + 1)", color = Color.White, modifier = Modifier.padding(4.dp))
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(16.dp))
        Text("Status: $status", color = Color.LightGray)
        Spacer(modifier = Modifier.height(8.dp))

        if (transferState.progress > 0f) {
            TransferProgress(state = transferState)
            Spacer(modifier = Modifier.height(16.dp))
        }

        // ── Bottom Actions ──
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.Center
        ) {
            Button(
                onClick = {
                    coroutineScope.launch {
                        isSharing = true
                        status = "Processing files..."
                        val paths = withContext(Dispatchers.IO) {
                            try {
                                selectedFiles.map { uri ->
                                    copyToCache(context, uri)
                                }
                            } catch (e: Exception) {
                                emptyList<String>()
                            }
                        }
                        if (paths.isEmpty()) {
                            status = "Failed to copy files."
                            isSharing = false
                            return@launch
                        }
                        status = "Starting server..."
                        FluxDropCore.startServer(paths.toTypedArray(), object : ServerCallbacks {
                            override fun onReady(ip: String, port: Int, newPin: Int) {
                                pin = String.format("%04d", newPin)
                                status = "Waiting for receiver on $ip:$port"
                            }
                            override fun onStatus(message: String) { status = message }
                            override fun onError(error: String) {
                                status = "Error: $error"
                                isSharing = false
                            }
                            override fun onProgress(filename: String, transferred: Long, total: Long, speedMbps: Double) {
                                transferState = TransferState(
                                    progress = if (total > 0) transferred.toFloat() / total.toFloat() else 0f,
                                    filename = filename,
                                    speedMbps = speedMbps,
                                    transferred = transferred,
                                    total = total,
                                    status = "Sending..."
                                )
                            }
                            override fun onComplete() {
                                status = "Transfer Complete ✅"
                                transferState = transferState.copy(
                                    progress = 1f,
                                    status = "All files transferred!"
                                )
                                isSharing = false
                            }
                        })
                    }
                },
                enabled = !isSharing && selectedFiles.isNotEmpty(),
                colors = ButtonDefaults.buttonColors(
                    containerColor = Color.White,
                    disabledContainerColor = Color.Gray
                ),
                shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp)
            ) {
                Text(if (isSharing) "Sharing..." else "Start Sharing", color = if (!isSharing && selectedFiles.isNotEmpty()) Color.DarkGray else Color.LightGray)
            }
            
            Spacer(modifier = Modifier.width(16.dp))
            
            Button(
                onClick = {
                    if (isSharing) {
                        FluxDropCore.requestCancelServer()
                    }
                    status = "Ready to send"
                    pin = ""
                    transferState = TransferState()
                    isSharing = false
                    selectedFiles = emptyList()
                },
                colors = ButtonDefaults.buttonColors(containerColor = dev.fluxdrop.app.ui.theme.FluxRed),
                shape = androidx.compose.foundation.shape.RoundedCornerShape(8.dp)
            ) {
                Text(if (isSharing) "Cancel" else "Clear", color = Color.White)
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
