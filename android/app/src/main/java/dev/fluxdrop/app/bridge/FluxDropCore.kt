package dev.fluxdrop.app.bridge

interface DeviceFoundCallback {
    fun onDeviceFound(ip: String, port: Int, sessionId: Long)
}

interface ServerCallbacks {
    fun onReady(ip: String, port: Int, pin: Int)
    fun onStatus(message: String)
    fun onError(error: String)
    fun onProgress(filename: String, transferred: Long, total: Long, speedMbps: Double)
    fun onComplete()
}

interface ClientCallbacks {
    fun onStatus(message: String)
    fun onError(error: String)
    fun onFileRequest(filename: String, fileSize: Long): Boolean
    fun onProgress(filename: String, transferred: Long, total: Long, speedMbps: Double)
    fun onComplete()
}

object FluxDropCore {
    init {
        try {
            System.loadLibrary("fluxdrop_jni")
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    external fun startServer(filePaths: Array<String>, callbacks: ServerCallbacks)
    external fun cancelServer()
    external fun requestCancelServer()
    
    external fun startDiscovery(roomId: Long, callback: DeviceFoundCallback)
    external fun stopDiscovery()
    
    external fun connect(ip: String, port: Int, pin: String, saveDir: String, callbacks: ClientCallbacks)
    external fun cancelClient()
    external fun requestCancelClient()
}
