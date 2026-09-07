#pragma once

#include <string>
#include <sstream>

#if !defined(NDEBUG) || defined(FLUXDROP_DEBUG)
#define FD_LOGGER_ENABLED 1
#else
#define FD_LOGGER_ENABLED 0
#endif

namespace fluxdrop {
namespace logger {

enum class Level {
    DEBUG,
    INFO,
    WARN,
    ERR
};

void log(Level level, const char* file, int line, const std::string& msg);

} // namespace logger
} // namespace fluxdrop

#if FD_LOGGER_ENABLED

#define FD_LOG_DEBUG(msg) do { std::ostringstream _fd_oss; _fd_oss << msg; ::fluxdrop::logger::log(::fluxdrop::logger::Level::DEBUG, __FILE__, __LINE__, _fd_oss.str()); } while(0)
#define FD_LOG_INFO(msg)  do { std::ostringstream _fd_oss; _fd_oss << msg; ::fluxdrop::logger::log(::fluxdrop::logger::Level::INFO,  __FILE__, __LINE__, _fd_oss.str()); } while(0)
#define FD_LOG_WARN(msg)  do { std::ostringstream _fd_oss; _fd_oss << msg; ::fluxdrop::logger::log(::fluxdrop::logger::Level::WARN,  __FILE__, __LINE__, _fd_oss.str()); } while(0)
#define FD_LOG_ERR(msg)   do { std::ostringstream _fd_oss; _fd_oss << msg; ::fluxdrop::logger::log(::fluxdrop::logger::Level::ERR,   __FILE__, __LINE__, _fd_oss.str()); } while(0)

// Backward compatibility macros
#define FD_LOG(msg)  FD_LOG_INFO(msg)
#define FD_WARN(msg) FD_LOG_WARN(msg)
#define FD_ERR(msg)  FD_LOG_ERR(msg)
#define CORE_LOG(msg) FD_LOG_INFO("[FD-CORE] " << msg)

#else

#define FD_LOG_DEBUG(msg) do {} while(0)
#define FD_LOG_INFO(msg)  do {} while(0)
#define FD_LOG_WARN(msg)  do {} while(0)
#define FD_LOG_ERR(msg)   do {} while(0)

#define FD_LOG(msg)  do {} while(0)
#define FD_WARN(msg) do {} while(0)
#define FD_ERR(msg)  do {} while(0)
#define CORE_LOG(msg) do {} while(0)

#endif
