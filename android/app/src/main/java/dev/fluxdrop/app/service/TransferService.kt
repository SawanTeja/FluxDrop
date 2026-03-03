package dev.fluxdrop.app.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat

class TransferService : Service() {
    private lateinit var multicastLock: WifiManager.MulticastLock
    private val NOTIFICATION_ID = 1001
    private val CHANNEL_ID = "fluxdrop_transfer_channel"

    override fun onCreate() {
        super.onCreate()
        
        // Acquire Multicast lock for UDP discovery
        val wifi = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        multicastLock = wifi.createMulticastLock("fluxdrop_discovery")
        multicastLock.setReferenceCounted(true)
        multicastLock.acquire()
        
        createNotificationChannel()
    }

    override fun onDestroy() {
        if (::multicastLock.isInitialized && multicastLock.isHeld) {
            multicastLock.release()
        }
        super.onDestroy()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val title = intent?.getStringExtra("EXTRA_TITLE") ?: "FluxDrop"
        val text = intent?.getStringExtra("EXTRA_TEXT") ?: "Running in background"
        
        val notification = createNotification(title, text)
        startForeground(NOTIFICATION_ID, notification)
        
        // If action is STOP, we stop foreground
        if (intent?.action == "ACTION_STOP_SERVICE") {
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
        
        return START_NOT_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null // We don't need IPC bound service, started service is enough
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "File Transfers",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Shows file transfer progress"
            }
            val notificationManager: NotificationManager =
                getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }

    private fun createNotification(title: String, text: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle(title)
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_upload) // default icon
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }
}
