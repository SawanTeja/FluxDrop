#pragma once

#include <iostream>
#include <chrono>
#include <ctime>
#include <cstdio>

inline std::string fd_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&tt));
    char result[40];
    std::snprintf(result, sizeof(result), "%s.%03d", buf, static_cast<int>(ms.count()));
    return result;
}

#define FD_LOG(msg)  do { std::cerr << "[FD " << fd_timestamp() << "] " << msg << std::endl; } while(0)
#define FD_WARN(msg) do { std::cerr << "[FD WARN " << fd_timestamp() << "] " << msg << std::endl; } while(0)
#define FD_ERR(msg)  do { std::cerr << "[FD ERR  " << fd_timestamp() << "] " << msg << std::endl; } while(0)
