#include "fluxdrop_core.h"
#include "networking.hpp"

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

#define CORE_LOG(msg) std::cerr << "[FD-CORE] " << msg << std::endl

namespace {

constexpr uint32_t kDefaultRoomId = 482913;

std::string to_protocol_relative_path(const fs::path& path) {
    return path.generic_string();
}

} // namespace

// Global Instances

static std::unique_ptr<networking::Server> g_server;
static std::unique_ptr<networking::Client> g_client;
static std::unique_ptr<networking::DiscoveryListener> g_discovery;

static std::atomic<bool> g_server_cancel_flag{false};
static std::atomic<bool> g_client_cancel_flag{false};

static std::thread g_server_thread;
static std::thread g_client_thread;

// Core API Implementation

extern "C" {

void fd_init() {
    CORE_LOG("fd_init()");
}

void fd_cleanup() {
    CORE_LOG("fd_cleanup() — stopping all");
    fd_cancel_server();
    fd_cancel_client();
    fd_stop_discovery();
    CORE_LOG("fd_cleanup() — done");
}

// Server Functions

void fd_start_server(const char** file_paths, int num_files,
                     fd_server_ready_cb ready_cb,
                     fd_server_status_cb status_cb,
                     fd_server_error_cb error_cb,
                     fd_server_progress_cb progress_cb,
                     fd_server_complete_cb complete_cb) {

    CORE_LOG("fd_start_server() — " << num_files << " paths");

    if (g_server_thread.joinable()) {
        CORE_LOG("fd_start_server() — joining previous server thread first");
        fd_cancel_server();
    }

    g_server_cancel_flag = false;

    std::queue<networking::TransferJob> jobs;
    uint32_t room_id = kDefaultRoomId;

    for (int i = 0; i < num_files; ++i) {
        fs::path path = fs::path(file_paths[i]).lexically_normal();
        if (fs::is_directory(path)) {
            fs::path base_dir = path.filename();
            if (base_dir.empty()) {
                base_dir = path.root_name();
            }
            std::error_code iter_ec;
            fs::recursive_directory_iterator end;
            for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, iter_ec);
                 it != end && !iter_ec; it.increment(iter_ec)) {
                if (it->is_regular_file()) {
                    fs::path relative = base_dir / fs::relative(it->path(), path);
                    jobs.push({it->path().string(), to_protocol_relative_path(relative), room_id});
                }
            }
        } else if (fs::is_regular_file(path)) {
            jobs.push({path.string(), to_protocol_relative_path(path.filename()), room_id});
        }
    }

    if (jobs.empty()) {
        CORE_LOG("fd_start_server() — no valid files found");
        if (error_cb) error_cb("No valid files found to send.");
        return;
    }

    CORE_LOG("fd_start_server() — " << jobs.size() << " files queued");

    networking::ServerCallbacks callbacks;
    callbacks.on_ready = [ready_cb](const std::string& ip, unsigned short port, uint16_t pin) {
        if (ready_cb) ready_cb(ip.c_str(), port, pin);
    };
    callbacks.on_status = [status_cb](const std::string& msg) {
        if (status_cb) status_cb(msg.c_str());
    };
    callbacks.on_error = [error_cb](const std::string& err) {
        if (error_cb) error_cb(err.c_str());
    };
    callbacks.on_progress = [progress_cb](const std::string& file, uint64_t transferred, uint64_t total, double speed) {
        if (progress_cb) progress_cb(file.c_str(), transferred, total, speed);
    };
    callbacks.on_complete = [complete_cb]() {
        if (complete_cb) complete_cb();
    };
    callbacks.cancel_flag = &g_server_cancel_flag;

    g_server = std::make_unique<networking::Server>();

    g_server_thread = std::thread([s = g_server.get(), jobs, callbacks]() {
        CORE_LOG("Server thread started");
        s->start_gui(jobs, callbacks);
        CORE_LOG("Server thread finished");
    });
}

void fd_cancel_server() {
    CORE_LOG("fd_cancel_server() — blocking cancel");
    g_server_cancel_flag = true;
    if (g_server) {
        g_server->stop();
    }
    if (g_server_thread.joinable()) {
        CORE_LOG("fd_cancel_server() — joining thread...");
        g_server_thread.join();
        CORE_LOG("fd_cancel_server() — thread joined");
    }
    g_server.reset();
}

void fd_request_cancel_server() {
    CORE_LOG("fd_request_cancel_server() — non-blocking cancel");
    g_server_cancel_flag = true;
    if (g_server) {
        g_server->stop();
    }
}

// Client Functions

void fd_start_discovery(uint32_t room_id, fd_client_device_found_cb found_cb) {
    CORE_LOG("fd_start_discovery() — room " << room_id);
    if (!g_discovery) {
        g_discovery = std::make_unique<networking::DiscoveryListener>();
    }
    g_discovery->start(room_id, [found_cb](const networking::DiscoveredDevice& d) {
        if (found_cb) {
            static thread_local std::string ip_storage;
            ip_storage = d.ip;
            fd_device_t dev;
            dev.session_id = d.session_id;
            dev.port = d.port;
            dev.ip = ip_storage.c_str();
            found_cb(&dev);
        }
    });
}

void fd_stop_discovery() {
    CORE_LOG("fd_stop_discovery()");
    if (g_discovery) {
        g_discovery->stop();
        g_discovery.reset();
    }
}

void fd_connect(const char* ip, int port, const char* pin, const char* save_dir,
                fd_client_status_cb status_cb,
                fd_client_error_cb error_cb,
                fd_client_file_request_cb file_request_cb,
                fd_client_progress_cb progress_cb,
                fd_client_complete_cb complete_cb) {

    CORE_LOG("fd_connect() — " << (ip ? ip : "null") << ":" << port);

    if (g_client_thread.joinable()) {
        CORE_LOG("fd_connect() — joining previous client thread first");
        fd_cancel_client();
    }

    g_client_cancel_flag = false;

    networking::ClientCallbacks callbacks;
    callbacks.on_status = [status_cb](const std::string& msg) {
        if (status_cb) status_cb(msg.c_str());
    };
    callbacks.on_error = [error_cb](const std::string& err) {
        if (error_cb) error_cb(err.c_str());
    };
    callbacks.on_file_request = [file_request_cb](const std::string& file, uint64_t size) -> bool {
        if (file_request_cb) return file_request_cb(file.c_str(), size);
        return true;
    };
    callbacks.on_progress = [progress_cb](const std::string& file, uint64_t transferred, uint64_t total, double speed) {
        if (progress_cb) progress_cb(file.c_str(), transferred, total, speed);
    };
    callbacks.on_complete = [complete_cb]() {
        if (complete_cb) complete_cb();
    };
    callbacks.cancel_flag = &g_client_cancel_flag;

    std::string ip_str = ip ? ip : "";
    std::string pin_str = pin ? pin : "";
    std::string dir_str = save_dir ? save_dir : "";

    g_client = std::make_unique<networking::Client>();

    g_client_thread = std::thread([c = g_client.get(), ip_str, port, pin_str, dir_str, callbacks]() {
        CORE_LOG("Client thread started — connecting to " << ip_str << ":" << port);
        if (!dir_str.empty()) {
            fs::create_directories(dir_str);
        }
        c->connect_gui(ip_str, port, pin_str, dir_str, callbacks);
        CORE_LOG("Client thread finished");
    });
}

void fd_cancel_client() {
    CORE_LOG("fd_cancel_client() — blocking cancel");
    g_client_cancel_flag = true;
    if (g_client) {
        g_client->stop();
    }
    if (g_client_thread.joinable()) {
        CORE_LOG("fd_cancel_client() — joining thread...");
        g_client_thread.join();
        CORE_LOG("fd_cancel_client() — thread joined");
    }
    g_client.reset();
}

void fd_request_cancel_client() {
    CORE_LOG("fd_request_cancel_client() — non-blocking cancel");
    g_client_cancel_flag = true;
    if (g_client) {
        g_client->stop();
    }
}

} // extern "C"
