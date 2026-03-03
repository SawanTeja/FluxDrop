package dev.fluxdrop.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.ui.unit.dp
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import dev.fluxdrop.app.ui.screens.ReceiveScreen
import dev.fluxdrop.app.ui.screens.SendScreen
import dev.fluxdrop.app.ui.theme.FluxDropTheme

class MainActivity : ComponentActivity() {

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        // Proceed even if some are denied, handle missing features gracefully later
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        requestPermissions()

        setContent {
            FluxDropTheme {
                FluxDropApp()
            }
        }
    }

    private fun requestPermissions() {
        val requiredPermissions = mutableListOf(
            Manifest.permission.INTERNET,
            Manifest.permission.ACCESS_WIFI_STATE,
            Manifest.permission.CHANGE_WIFI_MULTICAST_STATE,
            Manifest.permission.ACCESS_NETWORK_STATE,
            Manifest.permission.FOREGROUND_SERVICE
        )

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
             requiredPermissions.add(Manifest.permission.FOREGROUND_SERVICE_DATA_SYNC)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            requiredPermissions.add(Manifest.permission.POST_NOTIFICATIONS)
            requiredPermissions.add(Manifest.permission.READ_MEDIA_IMAGES)
            requiredPermissions.add(Manifest.permission.READ_MEDIA_VIDEO)
            requiredPermissions.add(Manifest.permission.READ_MEDIA_AUDIO)
        } else {
            requiredPermissions.add(Manifest.permission.READ_EXTERNAL_STORAGE)
        }

        val missingPermissions = requiredPermissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (missingPermissions.isNotEmpty()) {
            requestPermissionLauncher.launch(missingPermissions.toTypedArray())
        }

        // On Android 11+ we need MANAGE_EXTERNAL_STORAGE for native-layer file writes
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()) {
            val intent = Intent(
                Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:$packageName")
            )
            startActivity(intent)
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FluxDropApp() {
    var currentScreen by remember { mutableStateOf("send") }

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(
                title = { Text("FluxDrop") },
                navigationIcon = {
                    Icon(
                        painter = androidx.compose.ui.res.painterResource(id = R.drawable.fluxdroplogo),
                        contentDescription = "App Logo",
                        modifier = Modifier.padding(start = 16.dp).size(40.dp),
                        tint = androidx.compose.ui.graphics.Color.Unspecified
                    )
                }
            )
        },
        bottomBar = {
            NavigationBar {
                NavigationBarItem(
                    icon = { Text("⬆️") },
                    label = { Text("Send") },
                    selected = currentScreen == "send",
                    onClick = { currentScreen = "send" }
                )
                NavigationBarItem(
                    icon = { Text("⬇️") },
                    label = { Text("Receive") },
                    selected = currentScreen == "receive",
                    onClick = { currentScreen = "receive" }
                )
            }
        }
    ) { innerPadding ->
        if (currentScreen == "send") {
            SendScreen(modifier = Modifier.padding(innerPadding))
        } else {
            ReceiveScreen(modifier = Modifier.padding(innerPadding))
        }
    }
}
