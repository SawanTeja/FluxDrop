#pragma once

#include <string>
#include <cstdint>
#include <queue>
#include <functional>
#include <thread>
#include <atomic>

namespace networking {

struct TransferJob {
    std::string filepath;
    std::string filename;
    uint32_t session_id;
};

struct DiscoveredDevice {
    std::string ip;
    unsigned short port;
    uint32_t session_id;
};

using DeviceFoundCallback = std::function<void(const DiscoveredDevice&)>;

// Progress callback: filename, bytes_transferred, bytes_total, speed_mbps
using ProgressCallback = std::function<void(const std::string&, uint64_t, uint64_t, double)>;
using StatusCallback = std::function<void(const std::string&)>;

struct ServerCallbacks {
    std::function<void(const std::string& ip, unsigned short port, uint16_t pin)> on_ready;
    StatusCallback on_status;
    ProgressCallback on_progress;
    std::function<void()> on_complete;
    std::function<void(const std::string&)> on_error;
};

struct ClientCallbacks {
    StatusCallback on_status;
    ProgressCallback on_progress;
    std::function<void()> on_complete;
    std::function<void(const std::string&)> on_error;
};

class DiscoveryListener {
public:
    ~DiscoveryListener();
    void start(DeviceFoundCallback callback);
    void stop();
    bool is_running() const { return running_; }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
};

class Server {
public:
    void start(std::queue<TransferJob> jobs);
    void start_gui(std::queue<TransferJob> jobs, ServerCallbacks callbacks);
};

class Client {
public:
    void connect(const std::string& ip, unsigned short port);
    void join(uint32_t room_id);
    void connect_gui(const std::string& ip, unsigned short port,
                     const std::string& pin, ClientCallbacks callbacks);
};

} // namespace networking
