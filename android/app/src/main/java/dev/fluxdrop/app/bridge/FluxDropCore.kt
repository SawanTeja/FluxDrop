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

interface SessionCallbacks {
    fun onReady(ip: String, port: Int, pin: Int)
    fun onSessionEstablished(peerIp: String, peerPort: Int, role: Int)
    fun onSessionEnded()
    fun onStatus(message: String)
    fun onError(error: String)
    fun onFileOffer(filename: String, fileSize: Long): Boolean
    fun onProgress(filename: String, transferred: Long, total: Long, speedMbps: Double)
    fun onFileComplete(filename: String)
}

object FluxDropCore {
    init {
        try {
            System.loadLibrary("fluxdrop_jni")
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    // Legacy API
    external fun startServer(filePaths: Array<String>, callbacks: ServerCallbacks)
    external fun cancelServer()
    external fun requestCancelServer()
    
    external fun startDiscovery(roomId: Long, callback: DeviceFoundCallback)
    external fun stopDiscovery()
    
    external fun connect(ip: String, port: Int, pin: String, saveDir: String, callbacks: ClientCallbacks)
    external fun cancelClient()
    external fun requestCancelClient()

    // Session-based API
    external fun sessionHost(callbacks: SessionCallbacks)
    external fun sessionJoin(ip: String, port: Int, pin: String, saveDir: String, callbacks: SessionCallbacks)
    external fun sessionSendFiles(filePaths: Array<String>)
    external fun sessionSetSaveDir(dir: String)
    external fun sessionDisconnect()
    external fun sessionGetPin(): Int
    external fun sessionGetPort(): Int
    external fun sessionGetIp(): String
    external fun sessionIsConnected(): Boolean
}
