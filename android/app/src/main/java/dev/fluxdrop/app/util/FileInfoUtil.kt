package dev.fluxdrop.app.util

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns

data class SelectedFileInfo(
    val uri: Uri,
    val name: String,
    val size: Long
)

fun getFileInfo(context: Context, uri: Uri): SelectedFileInfo {
    var name = "Unknown File"
    var size = 0L
    
    val cursor = context.contentResolver.query(uri, null, null, null, null)
    if (cursor != null && cursor.moveToFirst()) {
        val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
        if (nameIndex != -1) {
            name = cursor.getString(nameIndex)
        }
        val sizeIndex = cursor.getColumnIndex(OpenableColumns.SIZE)
        if (sizeIndex != -1 && !cursor.isNull(sizeIndex)) {
            size = cursor.getLong(sizeIndex)
        }
        cursor.close()
    }
    return SelectedFileInfo(uri, name, size)
}
