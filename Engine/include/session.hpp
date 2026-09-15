#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "networking.hpp"
#include "protocol/packet.hpp"

namespace networking {

enum class SessionRole { HOST, GUEST };

struct SessionInfo {
    uint32_t session_id;
    std::string peer_ip;
    unsigned short peer_port;
    SessionRole role;
};

struct SessionCallbacks {
    // Host-only: called when the session is ready to accept connections
    std::function<void(const std::string& ip, unsigned short port, uint16_t pin)> on_ready;

    // Called when a peer connects and authenticates successfully
    std::function<void(const SessionInfo&)> on_session_established;

    // Called when the session ends (peer disconnected or local disconnect)
    std::function<void()> on_session_ended;

    // Status messages
    std::function<void(const std::string&)> on_status;

    // Error messages
    std::function<void(const std::string&)> on_error;

    // Incoming file from peer — return true to accept, false to reject
    std::function<bool(const std::string& filename, uint64_t size)> on_file_offer;

    // Transfer progress (sending or receiving)
    ProgressCallback on_progress;

    // A single file transfer completed
    std::function<void(const std::string& filename)> on_file_complete;
};

class Session {
  public:
    Session();
    ~Session();

    // BLOCKING — call on a background thread.
    // Binds TCP, generates PIN, broadcasts for discovery, accepts one guest,
    // authenticates, then enters the bidirectional message loop.
    void host(SessionCallbacks callbacks);

    // BLOCKING — call on a background thread.
    // Connects to a host at ip:port, authenticates with PIN,
    // then enters the bidirectional message loop.
    void join(const std::string& ip, unsigned short port, const std::string& pin, const std::string& save_dir,
              SessionCallbacks callbacks);

    // Thread-safe. Enqueues files to be sent to the peer.
    // The message loop picks them up and sends them.
    void queue_send_files(const std::vector<std::string>& file_paths);
    void queue_send_files_with_names(const std::vector<std::pair<std::string, std::string>>& files);

    // Thread-safe. Set the directory where received files are saved.
    void set_save_dir(const std::string& dir);

    // Thread-safe. Sends SESSION_END to peer, then closes the connection.
    void disconnect();

    // Thread-safe. Force-closes the socket to unblock any blocking I/O.
    void stop();

    // Getters — thread-safe.
    bool is_connected() const;
    uint16_t get_pin() const;
    unsigned short get_port() const;
    std::string get_ip() const;

  private:
    // The main event loop. Alternates between checking the send queue
    // and polling for incoming packets. Runs until stop_flag_ or disconnect.
    void run_message_loop(boost::asio::ip::tcp::socket& socket);

    // Handles an incoming FILE_META from the peer: asks the user via callback,
    // sends PONG/FILE_REJECT, receives the file data.
    void handle_incoming_file(boost::asio::ip::tcp::socket& socket, const protocol::PacketHeader& header);

    // Sends a batch of files to the peer. Expands directories, sends
    // FILE_META for each file, waits for PONG/RESUME/FILE_REJECT, sends chunks.
    void process_send_batch(boost::asio::ip::tcp::socket& socket, const std::vector<std::string>& file_paths);
    void process_send_batch_with_names(boost::asio::ip::tcp::socket& socket, const std::vector<std::pair<std::string, std::string>>& files);

    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_flag_{false};
    std::atomic<bool> sending_{false};

    // Socket access for external stop()/disconnect()
    boost::asio::ip::tcp::socket* active_socket_ = nullptr;
    boost::asio::ip::tcp::acceptor* active_acceptor_ = nullptr;
    std::mutex socket_mtx_;

    SessionCallbacks callbacks_;
    SessionInfo info_{};
    std::string save_dir_;
    uint16_t pin_{0};
    unsigned short port_{0};
    std::string ip_;

    // Send queue — UI thread pushes, message loop thread pops
    std::mutex send_mtx_;
    std::queue<std::vector<std::string>> send_queue_;
    std::queue<std::vector<std::pair<std::string, std::string>>> send_queue_with_names_;
};

} // namespace networking
