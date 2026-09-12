#include "logger.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>
#include <thread>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "FluxDropCore"
#endif

namespace fluxdrop {
namespace logger {

static std::mutex g_log_mutex;

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&tt));
    char result[40];
    std::snprintf(result, sizeof(result), "%s.%03d", buf, static_cast<int>(ms.count()));
    return result;
}

static const char* level_to_string(Level level) {
    switch (level) {
    case Level::DEBUG:
        return "DEBUG";
    case Level::INFO:
        return "INFO ";
    case Level::WARN:
        return "WARN ";
    case Level::ERR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

static const char* file_basename(const char* filepath) {
    const char* base = filepath;
    while (*filepath) {
        if (*filepath == '/' || *filepath == '\\') {
            base = filepath + 1;
        }
        filepath++;
    }
    return base;
}

void log(Level level, const char* file, int line, const std::string& msg) {
#if FD_LOGGER_ENABLED
    std::lock_guard<std::mutex> lock(g_log_mutex);

#ifdef __ANDROID__
    int android_level = ANDROID_LOG_INFO;
    switch (level) {
    case Level::DEBUG:
        android_level = ANDROID_LOG_DEBUG;
        break;
    case Level::INFO:
        android_level = ANDROID_LOG_INFO;
        break;
    case Level::WARN:
        android_level = ANDROID_LOG_WARN;
        break;
    case Level::ERR:
        android_level = ANDROID_LOG_ERROR;
        break;
    }
    __android_log_print(android_level, LOG_TAG, "[%s:%d] %s", file_basename(file), line, msg.c_str());
#else
    std::cerr << "[FD " << timestamp() << "] "
              << "[" << level_to_string(level) << "] "
              << "[" << std::this_thread::get_id() << "] "
              << "[" << file_basename(file) << ":" << line << "] " << msg << "\n";
#endif

#endif
}

} // namespace logger
} // namespace fluxdrop
