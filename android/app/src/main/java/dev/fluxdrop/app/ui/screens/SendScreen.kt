package dev.fluxdrop.app.ui.screens

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import dev.fluxdrop.app.bridge.FluxDropCore
import dev.fluxdrop.app.bridge.ServerCallbacks
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
    var progress by remember { mutableStateOf(0f) }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) {
            selectedFiles = uris
        }
    }

    Column(
        modifier = modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        if (pin.isNotEmpty()) {
            Text("Room PIN: $pin", style = MaterialTheme.typography.headlineMedium)
            Spacer(modifier = Modifier.height(16.dp))
        }

        Text("Status: $status")
        Spacer(modifier = Modifier.height(16.dp))

        if (progress > 0f) {
            LinearProgressIndicator(progress = progress, modifier = Modifier.fillMaxWidth())
            Spacer(modifier = Modifier.height(16.dp))
        }

        Button(onClick = { picker.launch(arrayOf("*/*")) }) {
            Text("Select Files")
        }

        Spacer(modifier = Modifier.height(16.dp))

        if (selectedFiles.isNotEmpty()) {
            Text("${selectedFiles.size} files selected")
            Spacer(modifier = Modifier.height(16.dp))
            Button(onClick = {
                coroutineScope.launch {
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
                        return@launch
                    }
                    status = "Starting server..."
                    FluxDropCore.startServer(paths.toTypedArray(), object : ServerCallbacks {
                        override fun onReady(ip: String, port: Int, newPin: Int) {
                            pin = String.format("%04d", newPin)
                            status = "Waiting for receiver on $ip:$port"
                        }
                        override fun onStatus(message: String) { status = message }
                        override fun onError(error: String) { status = "Error: $error" }
                        override fun onProgress(filename: String, transferred: Long, total: Long, speedMbps: Double) {
                            if (total > 0) progress = transferred.toFloat() / total.toFloat()
                        }
                        override fun onComplete() {
                            status = "Transfer Complete"
                            progress = 1f
                        }
                    })
                }
            }) {
                Text("Start Sharing")
            }
            
            Spacer(modifier = Modifier.height(8.dp))
            Button(onClick = {
                FluxDropCore.requestCancelServer()
                status = "Cancelled"
                pin = ""
                progress = 0f
            }) {
                Text("Cancel")
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
