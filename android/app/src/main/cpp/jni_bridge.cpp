#include <jni.h>
#include <string>
#include <vector>
#include "fluxdrop_core.h"
#include "logger.hpp"

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
                FD_LOG_ERR("Failed to attach thread");
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

// ------ Session-based API ------
static jobject g_session_callbacks = nullptr;

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_sessionHost(JNIEnv* env, jobject, jobject callbackObj) {
    if (g_session_callbacks) {
        env->DeleteGlobalRef(g_session_callbacks);
    }
    g_session_callbacks = env->NewGlobalRef(callbackObj);

    fd_session_host(
        // on_ready
        [](const char* ip, int port, int pin) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onReady", "(Ljava/lang/String;II)V");
            jstring jip = env->NewStringUTF(ip);
            env->CallVoidMethod(g_session_callbacks, mid, jip, port, pin);
            env->DeleteLocalRef(jip);
            env->DeleteLocalRef(cls);
        },
        // on_session_established
        [](const fd_session_info_t* info) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onSessionEstablished", "(Ljava/lang/String;II)V");
            jstring jip = env->NewStringUTF(info->peer_ip);
            env->CallVoidMethod(g_session_callbacks, mid, jip, info->peer_port, info->role);
            env->DeleteLocalRef(jip);
            env->DeleteLocalRef(cls);
        },
        // on_session_ended
        []() {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onSessionEnded", "()V");
            env->CallVoidMethod(g_session_callbacks, mid);
            env->DeleteLocalRef(cls);
        },
        // on_status
        [](const char* msg) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onStatus", "(Ljava/lang/String;)V");
            jstring jmsg = env->NewStringUTF(msg);
            env->CallVoidMethod(g_session_callbacks, mid, jmsg);
            env->DeleteLocalRef(jmsg);
            env->DeleteLocalRef(cls);
        },
        // on_error
        [](const char* err) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onError", "(Ljava/lang/String;)V");
            jstring jerr = env->NewStringUTF(err);
            env->CallVoidMethod(g_session_callbacks, mid, jerr);
            env->DeleteLocalRef(jerr);
            env->DeleteLocalRef(cls);
        },
        // on_file_offer
        [](const char* file, uint64_t size) -> bool {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return true;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onFileOffer", "(Ljava/lang/String;J)Z");
            jstring jfile = env->NewStringUTF(file);
            jboolean result = env->CallBooleanMethod(g_session_callbacks, mid, jfile, (jlong)size);
            env->DeleteLocalRef(jfile);
            env->DeleteLocalRef(cls);
            return result == JNI_TRUE;
        },
        // on_progress
        [](const char* file, uint64_t tx, uint64_t total, double speed) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onProgress", "(Ljava/lang/String;JJD)V");
            jstring jfile = env->NewStringUTF(file);
            env->CallVoidMethod(g_session_callbacks, mid, jfile, (jlong)tx, (jlong)total, (jdouble)speed);
            env->DeleteLocalRef(jfile);
            env->DeleteLocalRef(cls);
        },
        // on_file_complete
        [](const char* file) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onFileComplete", "(Ljava/lang/String;)V");
            jstring jfile = env->NewStringUTF(file);
            env->CallVoidMethod(g_session_callbacks, mid, jfile);
            env->DeleteLocalRef(jfile);
            env->DeleteLocalRef(cls);
        }
    );
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_sessionJoin(
    JNIEnv* env, jobject, jstring jip, jint port, jstring jpin, jstring jsaveDir, jobject callbackObj) {

    if (g_session_callbacks) {
        env->DeleteGlobalRef(g_session_callbacks);
    }
    g_session_callbacks = env->NewGlobalRef(callbackObj);

    const char* ip = env->GetStringUTFChars(jip, nullptr);
    const char* pin = env->GetStringUTFChars(jpin, nullptr);
    const char* saveDir = env->GetStringUTFChars(jsaveDir, nullptr);

    fd_session_join(ip, port, pin, saveDir,
        // on_session_established
        [](const fd_session_info_t* info) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onSessionEstablished", "(Ljava/lang/String;II)V");
            jstring jip = env->NewStringUTF(info->peer_ip);
            env->CallVoidMethod(g_session_callbacks, mid, jip, info->peer_port, info->role);
            env->DeleteLocalRef(jip);
            env->DeleteLocalRef(cls);
        },
        // on_session_ended
        []() {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onSessionEnded", "()V");
            env->CallVoidMethod(g_session_callbacks, mid);
            env->DeleteLocalRef(cls);
        },
        // on_status
        [](const char* msg) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onStatus", "(Ljava/lang/String;)V");
            jstring jmsg = env->NewStringUTF(msg);
            env->CallVoidMethod(g_session_callbacks, mid, jmsg);
            env->DeleteLocalRef(jmsg);
            env->DeleteLocalRef(cls);
        },
        // on_error
        [](const char* err) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onError", "(Ljava/lang/String;)V");
            jstring jerr = env->NewStringUTF(err);
            env->CallVoidMethod(g_session_callbacks, mid, jerr);
            env->DeleteLocalRef(jerr);
            env->DeleteLocalRef(cls);
        },
        // on_file_offer
        [](const char* file, uint64_t size) -> bool {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return true;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onFileOffer", "(Ljava/lang/String;J)Z");
            jstring jfile = env->NewStringUTF(file);
            jboolean result = env->CallBooleanMethod(g_session_callbacks, mid, jfile, (jlong)size);
            env->DeleteLocalRef(jfile);
            env->DeleteLocalRef(cls);
            return result == JNI_TRUE;
        },
        // on_progress
        [](const char* file, uint64_t tx, uint64_t total, double speed) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onProgress", "(Ljava/lang/String;JJD)V");
            jstring jfile = env->NewStringUTF(file);
            env->CallVoidMethod(g_session_callbacks, mid, jfile, (jlong)tx, (jlong)total, (jdouble)speed);
            env->DeleteLocalRef(jfile);
            env->DeleteLocalRef(cls);
        },
        // on_file_complete
        [](const char* file) {
            JNIEnv* env = get_env();
            if (!env || !g_session_callbacks) return;
            jclass cls = env->GetObjectClass(g_session_callbacks);
            jmethodID mid = env->GetMethodID(cls, "onFileComplete", "(Ljava/lang/String;)V");
            jstring jfile = env->NewStringUTF(file);
            env->CallVoidMethod(g_session_callbacks, mid, jfile);
            env->DeleteLocalRef(jfile);
            env->DeleteLocalRef(cls);
        }
    );

    env->ReleaseStringUTFChars(jip, ip);
    env->ReleaseStringUTFChars(jpin, pin);
    env->ReleaseStringUTFChars(jsaveDir, saveDir);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_sessionSendFiles(JNIEnv* env, jobject, jobjectArray filePaths, jobjectArray fileNames) {
    int count = env->GetArrayLength(filePaths);
    int names_count = env->GetArrayLength(fileNames);
    if (count != names_count) return;

    std::vector<std::string> paths(count);
    std::vector<std::string> names(count);
    std::vector<const char*> c_paths(count);
    std::vector<const char*> c_names(count);
    for (int i = 0; i < count; i++) {
        jstring jp = (jstring)env->GetObjectArrayElement(filePaths, i);
        jstring jn = (jstring)env->GetObjectArrayElement(fileNames, i);
        const char* cp = env->GetStringUTFChars(jp, nullptr);
        const char* cn = env->GetStringUTFChars(jn, nullptr);
        paths[i] = cp;
        names[i] = cn;
        c_paths[i] = paths[i].c_str();
        c_names[i] = names[i].c_str();
        env->ReleaseStringUTFChars(jp, cp);
        env->ReleaseStringUTFChars(jn, cn);
        env->DeleteLocalRef(jp);
        env->DeleteLocalRef(jn);
    }
    fd_session_send_files_with_names(c_paths.data(), c_names.data(), count);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_sessionSetSaveDir(JNIEnv* env, jobject, jstring jdir) {
    const char* dir = env->GetStringUTFChars(jdir, nullptr);
    fd_session_set_save_dir(dir);
    env->ReleaseStringUTFChars(jdir, dir);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_sessionDisconnect(JNIEnv* env, jobject) {
    fd_session_disconnect();
}

extern "C" JNIEXPORT jint JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_sessionGetPin(JNIEnv* env, jobject) {
    return fd_session_get_pin();
}

extern "C" JNIEXPORT jint JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_sessionGetPort(JNIEnv* env, jobject) {
    return fd_session_get_port();
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_sessionGetIp(JNIEnv* env, jobject) {
    const char* ip = fd_session_get_ip();
    return env->NewStringUTF(ip);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_dev_fluxdrop_app_bridge_FluxDropCore_sessionIsConnected(JNIEnv* env, jobject) {
    return fd_session_is_connected() ? JNI_TRUE : JNI_FALSE;
}
