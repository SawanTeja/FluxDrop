#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>

// We will implement the bridge in Phase 2
// For now, this stub allows CMake verification

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    // Return JNI version 1.6
    return JNI_VERSION_1_6;
}
