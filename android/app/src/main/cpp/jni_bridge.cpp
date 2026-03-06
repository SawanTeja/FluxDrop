#include <jni.h>
#include <android/log.h>
#include <string>
#include <vector>
#include "fluxdrop_core.h"

#define LOG_TAG "FluxDrop-JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM* g_jvm = nullptr;
static jobject g_server_callbacks = nullptr;
static jobject g_client_callbacks = nullptr;
static jobject g_discovery_callback = nullptr;

class JniThreadState {
public:
    JNIEnv* env = nullptr;
    bool needs_detach = false;

    ~JniThreadState() {
        if (needs_detach && g_jvm) {
            g_jvm->DetachCurrentThread();
        }
    }

    JNIEnv* get_env() {
        if (env) return env;
        jint res = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            if (g_jvm->AttachCurrentThread(&env, nullptr) == 0) {
                needs_detach = true;
            } else {
                LOGE("Failed to attach thread");
                return nullptr;
            }
        }
        return env;
    }
};

static JNIEnv* get_env() {
    static thread_local JniThreadState tls_state;
    return tls_state.get_env();
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_startServer(
    JNIEnv* env, jobject /*thiz*/, jobjectArray filePaths, jobject callbackObj) {
    
    if (g_server_callbacks) {
        env->DeleteGlobalRef(g_server_callbacks);
    }
    g_server_callbacks = env->NewGlobalRef(callbackObj);

    int count = env->GetArrayLength(filePaths);
    std::vector<std::string> paths(count);
    std::vector<const char*> c_paths(count);
    for (int i = 0; i < count; i++) {
        jstring js = (jstring)env->GetObjectArrayElement(filePaths, i);
        const char* cs = env->GetStringUTFChars(js, nullptr);
        paths[i] = cs;
        c_paths[i] = paths[i].c_str();
        env->ReleaseStringUTFChars(js, cs);
        env->DeleteLocalRef(js);
    }

    fd_start_server(c_paths.data(), count,
        // on_ready
        [](const char* ip, int port, int pin) {
            JNIEnv* env = get_env();
            if (!env) return;
            jclass cls = env->GetObjectClass(g_server_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onReady", "(Ljava/lang/String;II)V");
            jstring jip = env->NewStringUTF(ip);
            env->CallVoidMethod(g_server_callbacks, mid, jip, port, pin);
            env->DeleteLocalRef(jip);
            env->DeleteLocalRef(cls);
        },
        // on_status
        [](const char* msg) {
            JNIEnv* env = get_env();
            if (!env) return;
            jclass cls = env->GetObjectClass(g_server_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onStatus", "(Ljava/lang/String;)V");
            jstring jmsg = env->NewStringUTF(msg);
            env->CallVoidMethod(g_server_callbacks, mid, jmsg);
            env->DeleteLocalRef(jmsg);
            env->DeleteLocalRef(cls);
        },
        // on_error
        [](const char* err) {
            JNIEnv* env = get_env();
            if (!env) return;
            jclass cls = env->GetObjectClass(g_server_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onError", "(Ljava/lang/String;)V");
            jstring jerr = env->NewStringUTF(err);
            env->CallVoidMethod(g_server_callbacks, mid, jerr);
            env->DeleteLocalRef(jerr);
            env->DeleteLocalRef(cls);
        },
        // on_progress
        [](const char* file, uint64_t tx, uint64_t total, double speed) {
            JNIEnv* env = get_env();
            if (!env) return;
            jclass cls = env->GetObjectClass(g_server_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onProgress", "(Ljava/lang/String;JJD)V");
            jstring jfile = env->NewStringUTF(file);
            env->CallVoidMethod(g_server_callbacks, mid, jfile, (jlong)tx, (jlong)total, (jdouble)speed);
            env->DeleteLocalRef(jfile);
            env->DeleteLocalRef(cls);
        },
        // on_complete
        []() {
            JNIEnv* env = get_env();
            if (!env) return;
            jclass cls = env->GetObjectClass(g_server_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onComplete", "()V");
            env->CallVoidMethod(g_server_callbacks, mid);
            env->DeleteLocalRef(cls);
            
            // Clean up global ref since it's done
            // Wait: fd_cancel_server removes it, but if it ends safely, we should remove
            // Actually, keep it. If they call startServer again, it replaces the ref.
            // If we delete it here, a late callback could crash.
        }
    );
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_cancelServer(JNIEnv* env, jobject) {
    fd_cancel_server();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_requestCancelServer(JNIEnv* env, jobject) {
    fd_request_cancel_server();
}

// ------ Client functions ------
extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_startDiscovery(JNIEnv* env, jobject, jlong roomId, jobject callbackObj) {
    if (g_discovery_callback) {
        env->DeleteGlobalRef(g_discovery_callback);
    }
    g_discovery_callback = env->NewGlobalRef(callbackObj);

    fd_start_discovery((uint32_t)roomId, [](const fd_device_t* dev) {
        JNIEnv* env = get_env();
        if (!env || !g_discovery_callback) return;
        jclass cls = env->GetObjectClass(g_discovery_callback);
        jmethodID mid = env->GetMethodID(cls, "onDeviceFound", "(Ljava/lang/String;IJ)V");
        jstring jip = env->NewStringUTF(dev->ip);
        env->CallVoidMethod(g_discovery_callback, mid, jip, dev->port, (jlong)dev->session_id);
        env->DeleteLocalRef(jip);
        env->DeleteLocalRef(cls);
    });
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_stopDiscovery(JNIEnv* env, jobject) {
    fd_stop_discovery();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_connect(
    JNIEnv* env, jobject, jstring jip, jint port, jstring jpin, jstring jsaveDir, jobject callbackObj) {
    
    if (g_client_callbacks) {
        env->DeleteGlobalRef(g_client_callbacks);
    }
    g_client_callbacks = env->NewGlobalRef(callbackObj);

    const char* ip = env->GetStringUTFChars(jip, nullptr);
    const char* pin = env->GetStringUTFChars(jpin, nullptr);
    const char* saveDir = env->GetStringUTFChars(jsaveDir, nullptr);

    fd_connect(ip, port, pin, saveDir,
        // on_status
        [](const char* msg) {
            JNIEnv* env = get_env();
            if (!env || !g_client_callbacks) return;
            jclass cls = env->GetObjectClass(g_client_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onStatus", "(Ljava/lang/String;)V");
            jstring jmsg = env->NewStringUTF(msg);
            env->CallVoidMethod(g_client_callbacks, mid, jmsg);
            env->DeleteLocalRef(jmsg);
            env->DeleteLocalRef(cls);
        },
        // on_error
        [](const char* err) {
            JNIEnv* env = get_env();
            if (!env || !g_client_callbacks) return;
            jclass cls = env->GetObjectClass(g_client_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onError", "(Ljava/lang/String;)V");
            jstring jerr = env->NewStringUTF(err);
            env->CallVoidMethod(g_client_callbacks, mid, jerr);
            env->DeleteLocalRef(jerr);
            env->DeleteLocalRef(cls);
        },
        // on_file_request
        [](const char* file, uint64_t size) -> bool {
            JNIEnv* env = get_env();
            if (!env || !g_client_callbacks) return true;
            jclass cls = env->GetObjectClass(g_client_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onFileRequest", "(Ljava/lang/String;J)Z");
            jstring jfile = env->NewStringUTF(file);
            jboolean result = env->CallBooleanMethod(g_client_callbacks, mid, jfile, (jlong)size);
            env->DeleteLocalRef(jfile);
            env->DeleteLocalRef(cls);
            return result == JNI_TRUE;
        },
        // on_progress
        [](const char* file, uint64_t tx, uint64_t total, double speed) {
            JNIEnv* env = get_env();
            if (!env || !g_client_callbacks) return;
            jclass cls = env->GetObjectClass(g_client_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onProgress", "(Ljava/lang/String;JJD)V");
            jstring jfile = env->NewStringUTF(file);
            env->CallVoidMethod(g_client_callbacks, mid, jfile, (jlong)tx, (jlong)total, (jdouble)speed);
            env->DeleteLocalRef(jfile);
            env->DeleteLocalRef(cls);
        },
        // on_complete
        []() {
            JNIEnv* env = get_env();
            if (!env || !g_client_callbacks) return;
            jclass cls = env->GetObjectClass(g_client_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onComplete", "()V");
            env->CallVoidMethod(g_client_callbacks, mid);
            env->DeleteLocalRef(cls);
        }
    );

    env->ReleaseStringUTFChars(jip, ip);
    env->ReleaseStringUTFChars(jpin, pin);
    env->ReleaseStringUTFChars(jsaveDir, saveDir);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_cancelClient(JNIEnv* env, jobject) {
    fd_cancel_client();
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_requestCancelClient(JNIEnv* env, jobject) {
    fd_request_cancel_client();
}
