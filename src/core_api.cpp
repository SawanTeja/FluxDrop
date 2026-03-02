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

// ----------------------------------------------------------------------------
// Global Instances
// ----------------------------------------------------------------------------

// A quick and dirty way to manage the singleton-ish nature of the C API
static std::unique_ptr<networking::Server> g_server;
static std::unique_ptr<networking::Client> g_client;
static std::unique_ptr<networking::DiscoveryListener> g_discovery;

static std::atomic<bool> g_server_cancel_flag{false};
static std::atomic<bool> g_client_cancel_flag{false};

static std::thread g_server_thread;
static std::thread g_client_thread;

// ----------------------------------------------------------------------------
// Core API Implementation
// ----------------------------------------------------------------------------

extern "C" {

void fd_init() {
    // Initialization, if any e.g. networking startup for Windows
}

void fd_cleanup() {
    fd_cancel_server();
    fd_cancel_client();
    fd_stop_discovery();
}

// ----------------------------------------------------------------------------
// Server Functions
// ----------------------------------------------------------------------------

void fd_start_server(const char** file_paths, int num_files,
                     fd_server_ready_cb ready_cb,
                     fd_server_status_cb status_cb,
                     fd_server_error_cb error_cb,
                     fd_server_progress_cb progress_cb,
                     fd_server_complete_cb complete_cb) {

    if (g_server_thread.joinable()) {
        fd_cancel_server(); // stop existing
    }

    g_server_cancel_flag = false;

    // Parse files into TransferJobs
    std::queue<networking::TransferJob> jobs;
    uint32_t room_id = 482913; // default room ID for now

    for (int i = 0; i < num_files; ++i) {
        fs::path path(file_paths[i]);
        if (fs::is_directory(path)) {
            std::string base_dir = path.filename().string();
            for (const auto& entry : fs::recursive_directory_iterator(path)) {
                if (entry.is_regular_file()) {
                    fs::path relative = base_dir / fs::relative(entry.path(), path);
                    jobs.push({entry.path().string(), relative.string(), room_id});
                }
            }
        } else if (fs::is_regular_file(path)) {
            jobs.push({path.string(), path.filename().string(), room_id});
        }
    }

    if (jobs.empty()) {
        if (error_cb) error_cb("No valid files found to send.");
        return;
    }

    // Set up callbacks
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

    // Start in background thread so C API is non-blocking
    g_server_thread = std::thread([s = g_server.get(), jobs, callbacks]() {
        s->start_gui(jobs, callbacks);
    });
}

void fd_cancel_server() {
    g_server_cancel_flag = true;
    if (g_server) {
        g_server->stop();
    }
    if (g_server_thread.joinable()) {
        g_server_thread.join(); 
    }
    g_server.reset();
}

// ----------------------------------------------------------------------------
// Client Functions
// ----------------------------------------------------------------------------

void fd_start_discovery(uint32_t room_id, fd_client_device_found_cb found_cb) {
    if (!g_discovery) {
        g_discovery = std::make_unique<networking::DiscoveryListener>();
    }
    g_discovery->start([found_cb](const networking::DiscoveredDevice& d) {
        if (found_cb) {
            // Keep IP string alive for the duration of the callback
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

    if (g_client_thread.joinable()) {
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
        return true; // default true
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
        fs::create_directories(dir_str);
        c->connect_gui(ip_str, port, pin_str, dir_str, callbacks);
    });
}

void fd_cancel_client() {
    g_client_cancel_flag = true;
    if (g_client) {
        g_client->stop();
    }
    if (g_client_thread.joinable()) {
        g_client_thread.join();
    }
    g_client.reset();
}

} // extern "C"
