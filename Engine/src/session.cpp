#include "session.hpp"
#include "protocol/file_meta.hpp"
#include "protocol/packet.hpp"
#include "security.hpp"
#include "transfer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

#if defined(__linux__) || defined(__ANDROID__)
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include "logger.hpp"

namespace networking {

namespace fs = std::filesystem;

namespace {

// ── Helpers (shared with networking.cpp, duplicated here to keep networking.cpp untouched) ──

uint64_t decode_resume_offset(const protocol::PacketHeader& header) {
    return (static_cast<uint64_t>(header.reserved) << 32) | header.payload_size;
}

protocol::PacketHeader make_resume_header(uint32_t session_id, uint64_t resume_offset) {
    return {static_cast<uint32_t>(protocol::CommandType::RESUME), static_cast<uint32_t>(resume_offset & 0xFFFFFFFFull),
            session_id, static_cast<uint32_t>(resume_offset >> 32)};
}

fs::path sanitize_relative_save_path(const std::string& remote_name) {
    std::string normalized = remote_name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    fs::path parsed(normalized);
    if (parsed.is_absolute() || parsed.has_root_name() || parsed.has_root_directory()) {
        throw std::runtime_error("Sender provided an absolute file path.");
    }

    fs::path sanitized;
    for (const auto& part : parsed) {
        std::string token = part.generic_string();
        if (token.empty() || token == ".") {
            continue;
        }
        if (token == "..") {
            throw std::runtime_error("Sender tried to write outside the selected folder.");
        }
        sanitized /= part;
    }

    if (sanitized.empty()) {
        throw std::runtime_error("Sender provided an empty file name.");
    }

    return sanitized.lexically_normal();
}

uint64_t available_space_for_target(const fs::path& target_path) {
    std::error_code ec;
    fs::path probe = target_path;

    if (!fs::exists(probe, ec)) {
        probe = probe.parent_path();
    }

    while (!probe.empty() && !fs::exists(probe, ec)) {
        probe = probe.parent_path();
    }

    if (probe.empty()) {
        probe = fs::current_path(ec);
    }

    const auto space_info = fs::space(probe, ec);
    return ec ? 0 : space_info.available;
}

std::string to_protocol_relative_path(const fs::path& path) {
    return path.generic_string();
}

constexpr uint32_t kDefaultSessionId = 482913;

} // anonymous namespace

// ── Session lifecycle ──

Session::Session() = default;

Session::~Session() {
    stop();
}

// ── Host ──

void Session::host(SessionCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
    stop_flag_ = false;
    connected_ = false;

    try {
        info_.session_id = kDefaultSessionId;
        info_.role = SessionRole::HOST;

        boost::asio::io_context io_context;
        boost::asio::ip::tcp::acceptor acceptor(io_context,
                                                boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));

        auto interfaces = get_network_interfaces(io_context);
        ip_ = interfaces.empty() ? "0.0.0.0" : interfaces.front();
        port_ = acceptor.local_endpoint().port();

        pin_ = security::generate_pin();
        std::string pin_str = std::to_string(pin_);
        std::string pin_hash = security::hash_pin(pin_str);

        CORE_LOG("Session::host() — listening on " << ip_ << ":" << port_);

        if (callbacks_.on_ready)
            callbacks_.on_ready(ip_, port_, pin_);

        // Broadcast for discovery (UDP broadcast + multicast)
        std::atomic<bool> broadcasting{true};
        std::thread broadcast_thread([this, &broadcasting]() {
            try {
                boost::asio::io_context udp_io;
                boost::asio::ip::udp::socket udp_socket(
                    udp_io, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), DISCOVERY_PORT));
                udp_socket.set_option(boost::asio::socket_base::reuse_address(true));
                udp_socket.set_option(boost::asio::socket_base::broadcast(true));

                auto interfaces = get_network_interfaces(udp_io);
                for (const auto& ip : interfaces) {
                    try {
                        udp_socket.set_option(boost::asio::ip::multicast::join_group(
                            boost::asio::ip::make_address(MULTICAST_GROUP).to_v4(),
                            boost::asio::ip::make_address(ip).to_v4()));
                    } catch(...) {}
                }

                udp_socket.non_blocking(true);

                // Thread to periodically broadcast RESPONSE
                std::thread prober([this, &broadcasting]() {
                    try {
                        boost::asio::io_context probe_io;
                        boost::asio::ip::udp::socket probe_socket(probe_io, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
                        probe_socket.set_option(boost::asio::socket_base::broadcast(true));

                        boost::asio::ip::udp::endpoint broadcast_ep(boost::asio::ip::address_v4::broadcast(), DISCOVERY_PORT);
                        boost::asio::ip::udp::endpoint multicast_ep(boost::asio::ip::make_address(MULTICAST_GROUP), DISCOVERY_PORT);
                        
                        while (broadcasting) {
                            std::string msg = "FLUXDROP_RESPONSE|" + std::to_string(info_.session_id) + "|" + std::to_string(port_) +
                                              "|" + get_instance_id();
                            boost::system::error_code ec;
                            probe_socket.send_to(boost::asio::buffer(msg), broadcast_ep, 0, ec);
                            probe_socket.send_to(boost::asio::buffer(msg), multicast_ep, 0, ec);

                            auto detailed = get_detailed_network_interfaces();
                            for (const auto& iface : detailed) {
                                if (!iface.broadcast_ip.empty() && iface.broadcast_ip != "255.255.255.255") {
                                    try {
                                        boost::asio::ip::udp::endpoint iface_bcast(
                                            boost::asio::ip::make_address(iface.broadcast_ip), DISCOVERY_PORT);
                                        probe_socket.send_to(boost::asio::buffer(msg), iface_bcast, 0, ec);
                                    } catch (...) {}
                                }
                            }

                            std::this_thread::sleep_for(std::chrono::seconds(1));
                        }
                    } catch(...) {}
                });

                while (broadcasting) {
                    std::array<char, 1024> recv_buf;
                    boost::asio::ip::udp::endpoint sender_endpoint;
                    boost::system::error_code ec;

                    size_t len = udp_socket.receive_from(boost::asio::buffer(recv_buf), sender_endpoint, 0, ec);

                    if (ec == boost::asio::error::would_block) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    if (ec) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }

                    std::string message(recv_buf.data(), len);
                    if (message == "FLUXDROP_DISCOVER") {
                        std::string resp = "FLUXDROP_RESPONSE|" + std::to_string(info_.session_id) + "|" + std::to_string(port_) + "|" + get_instance_id();
                        boost::system::error_code send_ec;
                        // Always send directly to the DISCOVERY_PORT where guests listen
                        boost::asio::ip::udp::endpoint target_ep(sender_endpoint.address(), DISCOVERY_PORT);
                        udp_socket.send_to(boost::asio::buffer(resp), target_ep, 0, send_ec);
                        // Also reply to the sender's ephemeral port in case they probe from there
                        if (sender_endpoint.port() != DISCOVERY_PORT) {
                            udp_socket.send_to(boost::asio::buffer(resp), sender_endpoint, 0, send_ec);
                        }
                    }
                }
                
                if (prober.joinable()) {
                    prober.join();
                }
            } catch (std::exception& e) {
                CORE_LOG("Broadcast thread exception: " << e.what());
            }
        });

        {
            std::lock_guard<std::mutex> lock(socket_mtx_);
            active_acceptor_ = &acceptor;
        }

        // Accept loop — keep waiting for a guest that authenticates correctly
        while (!stop_flag_) {
            boost::asio::ip::tcp::socket socket(io_context);

            {
                std::lock_guard<std::mutex> lock(socket_mtx_);
                active_socket_ = &socket;
            }

            acceptor.non_blocking(true);
            boost::system::error_code accept_ec;

            while (true) {
                if (stop_flag_) {
                    accept_ec = boost::asio::error::operation_aborted;
                    break;
                }
                acceptor.accept(socket, accept_ec);
                if (accept_ec == boost::asio::error::would_block || accept_ec == boost::asio::error::try_again) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                break;
            }

            if (accept_ec || stop_flag_) {
                broadcasting = false;
                if (broadcast_thread.joinable())
                    broadcast_thread.join();
                if (callbacks_.on_status)
                    callbacks_.on_status("Session cancelled.");
                {
                    std::lock_guard<std::mutex> lock(socket_mtx_);
                    active_socket_ = nullptr;
                    active_acceptor_ = nullptr;
                }
                return;
            }

            CORE_LOG("Session::host() — client connected, authenticating...");
            if (callbacks_.on_status)
                callbacks_.on_status("Device connected. Authenticating...");

            // AUTH handshake
            protocol::PacketHeader auth_header = transfer::MessageReceiver::receive_header(socket);
            if (auth_header.command != static_cast<uint32_t>(protocol::CommandType::AUTH)) {
                if (callbacks_.on_error)
                    callbacks_.on_error("Expected AUTH packet, got: " + std::to_string(auth_header.command));
                {
                    std::lock_guard<std::mutex> lock(socket_mtx_);
                    active_socket_ = nullptr;
                }
                continue;
            }

            std::vector<char> auth_buf(auth_header.payload_size);
            boost::asio::read(socket, boost::asio::buffer(auth_buf));
            std::string received_hash(auth_buf.begin(), auth_buf.end());

            if (received_hash != pin_hash) {
                CORE_LOG("Session::host() — wrong PIN");
                if (callbacks_.on_status)
                    callbacks_.on_status("Authentication FAILED. Wrong PIN.");
                protocol::PacketHeader fail{static_cast<uint32_t>(protocol::CommandType::AUTH_FAIL), 0,
                                            info_.session_id, 0};
                transfer::MessageSender::send_header(socket, fail);
                if (callbacks_.on_status)
                    callbacks_.on_status("Wrong PIN entered. Waiting for correct PIN...");
                {
                    std::lock_guard<std::mutex> lock(socket_mtx_);
                    active_socket_ = nullptr;
                }
                continue;
            }

            // Auth succeeded — stop broadcasting, enter session
            broadcasting = false;
            if (broadcast_thread.joinable())
                broadcast_thread.join();

            {
                std::lock_guard<std::mutex> lock(socket_mtx_);
                active_acceptor_ = nullptr;
            }

            CORE_LOG("Session::host() — authenticated, entering message loop");
            if (callbacks_.on_status)
                callbacks_.on_status("Authenticated! Session established.");

            protocol::PacketHeader ok{static_cast<uint32_t>(protocol::CommandType::AUTH_OK), 0, info_.session_id, 0};
            transfer::MessageSender::send_header(socket, ok);

            // Fill in peer info
            try {
                info_.peer_ip = socket.remote_endpoint().address().to_string();
                info_.peer_port = socket.remote_endpoint().port();
            } catch (...) {
                info_.peer_ip = "unknown";
                info_.peer_port = 0;
            }

            // Enter bidirectional message loop
            run_message_loop(socket);

            {
                std::lock_guard<std::mutex> lock(socket_mtx_);
                active_socket_ = nullptr;
            }
            break; // Session ended — exit
        }

        {
            std::lock_guard<std::mutex> lock(socket_mtx_);
            active_acceptor_ = nullptr;
        }

    } catch (std::exception& e) {
        if (callbacks_.on_error)
            callbacks_.on_error(std::string("Session host error: ") + e.what());
    }
}

// ── Join ──

void Session::join(const std::string& ip, unsigned short port, const std::string& pin, const std::string& save_dir,
                   SessionCallbacks callbacks) {
    callbacks_ = std::move(callbacks);
    save_dir_ = save_dir;
    stop_flag_ = false;
    connected_ = false;

    try {
        info_.role = SessionRole::GUEST;

        boost::asio::io_context io_context;
        boost::asio::ip::tcp::socket socket(io_context);

        {
            std::lock_guard<std::mutex> lock(socket_mtx_);
            active_socket_ = &socket;
        }

        CORE_LOG("Session::join() — connecting to " << ip << ":" << port);

        boost::asio::ip::tcp::resolver resolver(io_context);
        boost::asio::connect(socket, resolver.resolve(ip, std::to_string(port)));

        if (callbacks_.on_status)
            callbacks_.on_status("Connected! Authenticating...");

        // Send AUTH
        std::string hashed_pin = security::hash_pin(pin);
        protocol::PacketHeader auth{static_cast<uint32_t>(protocol::CommandType::AUTH),
                                    static_cast<uint32_t>(hashed_pin.size()), 0, 0};
        transfer::MessageSender::send_header(socket, auth);
        boost::asio::write(socket, boost::asio::buffer(hashed_pin));

        // Wait for AUTH_OK or AUTH_FAIL
        protocol::PacketHeader auth_response = transfer::MessageReceiver::receive_header(socket);
        if (auth_response.command == static_cast<uint32_t>(protocol::CommandType::AUTH_FAIL)) {
            if (callbacks_.on_error)
                callbacks_.on_error("Authentication failed. Wrong PIN.");
            {
                std::lock_guard<std::mutex> lock(socket_mtx_);
                active_socket_ = nullptr;
            }
            return;
        } else if (auth_response.command != static_cast<uint32_t>(protocol::CommandType::AUTH_OK)) {
            if (callbacks_.on_error)
                callbacks_.on_error("Unexpected auth response.");
            {
                std::lock_guard<std::mutex> lock(socket_mtx_);
                active_socket_ = nullptr;
            }
            return;
        }

        CORE_LOG("Session::join() — authenticated, entering message loop");
        if (callbacks_.on_status)
            callbacks_.on_status("Authenticated! Session established.");

        // Fill in session info
        info_.session_id = auth_response.session_id;
        info_.peer_ip = ip;
        info_.peer_port = port;

        // Create save directory
        if (!save_dir_.empty()) {
            fs::create_directories(save_dir_);
        }

        // Enter bidirectional message loop
        run_message_loop(socket);

        {
            std::lock_guard<std::mutex> lock(socket_mtx_);
            active_socket_ = nullptr;
        }

    } catch (std::exception& e) {
        if (callbacks_.on_error)
            callbacks_.on_error(std::string("Session join error: ") + e.what());
        {
            std::lock_guard<std::mutex> lock(socket_mtx_);
            active_socket_ = nullptr;
        }
    }
}

// ── Message loop ──

void Session::run_message_loop(boost::asio::ip::tcp::socket& socket) {
    connected_ = true;

    if (callbacks_.on_session_established)
        callbacks_.on_session_established(info_);

    CORE_LOG("Session message loop started");
    FD_LOG_INFO("[DIAG] run_message_loop() — peer=" << info_.peer_ip << ":" << info_.peer_port
                << " session_id=" << info_.session_id
                << " role=" << (info_.role == SessionRole::HOST ? "HOST" : "GUEST")
                << " socket.is_open=" << socket.is_open());

    // Enable TCP keepalive and TCP_NODELAY on the socket
    try {
        boost::asio::socket_base::keep_alive keep_alive_opt(true);
        socket.set_option(keep_alive_opt);
        boost::asio::ip::tcp::no_delay no_delay_opt(true);
        socket.set_option(no_delay_opt);
#if defined(__linux__) || defined(__ANDROID__)
        int fd = socket.native_handle();
        int idle = 2;
        int interval = 1;
        int count = 3;
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
        setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
#endif
        FD_LOG_INFO("[DIAG] TCP keepalive (aggressive) + TCP_NODELAY enabled on session socket");
    } catch (const std::exception& e) {
        FD_LOG_WARN("[DIAG] Failed to set TCP keepalive: " << e.what());
    }

    auto last_ping_time = std::chrono::steady_clock::now();

    while (!stop_flag_) {
        // 1. Check send queue
        std::vector<std::string> pending_files;
        std::vector<std::pair<std::string, std::string>> pending_files_with_names;
        {
            std::lock_guard<std::mutex> lock(send_mtx_);
            if (!send_queue_.empty()) {
                pending_files = std::move(send_queue_.front());
                send_queue_.pop();
            } else if (!send_queue_with_names_.empty()) {
                pending_files_with_names = std::move(send_queue_with_names_.front());
                send_queue_with_names_.pop();
            }
        }

        if (!pending_files.empty()) {
            FD_LOG_INFO("[DIAG] run_message_loop() — dequeued send batch: " << pending_files.size() << " file(s)");
            process_send_batch(socket, pending_files);
            continue;
        } else if (!pending_files_with_names.empty()) {
            FD_LOG_INFO("[DIAG] run_message_loop() — dequeued named send batch: " << pending_files_with_names.size() << " file(s)");
            process_send_batch_with_names(socket, pending_files_with_names);
            continue;
        }

        // 2. Poll for incoming data
        boost::system::error_code ec;
        size_t available = socket.available(ec);

        if (ec) {
            FD_LOG_ERR("[DIAG] run_message_loop() — socket.available() error: " << ec.message()
                       << " code=" << ec.value()
                       << " socket.is_open=" << socket.is_open());
            CORE_LOG("Session message loop — socket error: " << ec.message());
            if (callbacks_.on_status)
                callbacks_.on_status("Connection lost.");
            break;
        }

        if (available >= 16) {
            auto header = transfer::MessageReceiver::receive_header(socket);

            // Connection closed (all-zero header)
            if (header.command == 0 && header.payload_size == 0 && header.session_id == 0 && header.reserved == 0) {
                CORE_LOG("Session message loop — peer disconnected (zero header)");
                if (callbacks_.on_status)
                    callbacks_.on_status("Peer disconnected.");
                break;
            }

            auto cmd = static_cast<protocol::CommandType>(header.command);

            if (cmd == protocol::CommandType::FILE_META) {
                handle_incoming_file(socket, header);
            } else if (cmd == protocol::CommandType::PING) {
                protocol::PacketHeader pong{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id,
                                            0};
                transfer::MessageSender::send_header(socket, pong);
            } else if (cmd == protocol::CommandType::PONG) {
                FD_LOG_DEBUG("[DIAG] run_message_loop() — received keepalive PONG");
            } else if (cmd == protocol::CommandType::SESSION_END) {
                CORE_LOG("Session message loop — peer sent SESSION_END");
                if (callbacks_.on_status)
                    callbacks_.on_status("Peer ended the session.");
                break;
            }
        } else {
            // Idle — send keepalive PING every 5 seconds to prevent connection timeout
            auto now = std::chrono::steady_clock::now();
            auto since_last_ping = std::chrono::duration_cast<std::chrono::seconds>(now - last_ping_time).count();
            if (since_last_ping >= 5) {
                protocol::PacketHeader ping{static_cast<uint32_t>(protocol::CommandType::PING), 0, info_.session_id, 0};
                if (!transfer::MessageSender::send_header(socket, ping)) {
                    FD_LOG_ERR("[DIAG] run_message_loop() — keepalive PING failed, connection is dead");
                    if (callbacks_.on_status)
                        callbacks_.on_status("Connection lost.");
                    break;
                }
                last_ping_time = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // Graceful exit — socket will be closed by disconnect() or stop()

    CORE_LOG("Session message loop ended");
    FD_LOG_INFO("[DIAG] run_message_loop() — exited. stop_flag=" << stop_flag_.load()
                << " socket.is_open=" << socket.is_open());
    connected_ = false;

    if (callbacks_.on_session_ended)
        callbacks_.on_session_ended();
}

// ── Handle incoming file ──

void Session::handle_incoming_file(boost::asio::ip::tcp::socket& socket, const protocol::PacketHeader& header) {
    auto meta = transfer::MessageReceiver::receive_file_meta(socket, header.payload_size);

    fs::path relative_path;
    try {
        relative_path = sanitize_relative_save_path(meta.filename);
    } catch (const std::exception& ex) {
        if (callbacks_.on_error)
            callbacks_.on_error(ex.what());
        protocol::PacketHeader reject{static_cast<uint32_t>(protocol::CommandType::FILE_REJECT), 0, header.session_id,
                                      0};
        if (!transfer::MessageSender::send_header(socket, reject)) {
            return;
        }
        return;
    }

    CORE_LOG("Session — incoming file: " << meta.filename << " (" << format_size(meta.size) << ")");
    if (callbacks_.on_status)
        callbacks_.on_status("Incoming: " + relative_path.generic_string() + " (" + format_size(meta.size) + ")");

    fs::path base_dir = save_dir_.empty() ? fs::current_path() : fs::path(save_dir_);
    fs::path save_path = (base_dir / relative_path).lexically_normal();

    // Check disk space
    uint64_t avail = available_space_for_target(save_path);
    if (avail > 0 && avail < meta.size) {
        if (callbacks_.on_error)
            callbacks_.on_error("Insufficient disk space. Requires " + format_size(meta.size) + " but only " +
                                format_size(avail) + " available.");
        protocol::PacketHeader reject{static_cast<uint32_t>(protocol::CommandType::FILE_REJECT), 0, header.session_id,
                                      0};
        if (!transfer::MessageSender::send_header(socket, reject)) {
            return;
        }
        return;
    }

    // Ask user via callback
    bool accepted = true;
    if (callbacks_.on_file_offer) {
        accepted = callbacks_.on_file_offer(meta.filename, meta.size);
    }

    if (!accepted) {
        protocol::PacketHeader reject{static_cast<uint32_t>(protocol::CommandType::FILE_REJECT), 0, header.session_id,
                                      0};
        if (!transfer::MessageSender::send_header(socket, reject)) {
            return;
        }
        if (callbacks_.on_status)
            callbacks_.on_status("Rejected: " + relative_path.generic_string());
        return;
    }

    // Check for name collision (if the file exists but no .fluxpart exists, it's a completed file we shouldn't overwrite)
    std::error_code ec;
    std::string save_path_string = save_path.string();
    std::string part_file = save_path_string + ".fluxpart";
    
    if (fs::exists(save_path, ec) && !fs::exists(part_file, ec)) {
        int counter = 1;
        fs::path stem = relative_path.stem();
        fs::path ext = relative_path.extension();
        fs::path parent = relative_path.parent_path();
        
        while (fs::exists(save_path, ec) && !fs::exists(save_path.string() + ".fluxpart", ec)) {
            fs::path new_name = stem.string() + " (" + std::to_string(counter) + ")" + ext.string();
            save_path = (base_dir / parent / new_name).lexically_normal();
            counter++;
        }
        save_path_string = save_path.string();
        part_file = save_path_string + ".fluxpart";
    }

    // Check for resume (.fluxpart file)
    uint64_t resume_offset = 0;
    auto part_size = fs::file_size(part_file, ec);
    if (!ec) {
        resume_offset = part_size;
        if (callbacks_.on_status)
            callbacks_.on_status("Resuming from " + format_size(resume_offset));
    }

    if (resume_offset > 0) {
        auto resume_hdr = make_resume_header(header.session_id, resume_offset);
        if (!transfer::MessageSender::send_header(socket, resume_hdr)) {
            if (callbacks_.on_error)
                callbacks_.on_error("Connection lost while accepting file.");
            return;
        }
    } else {
        protocol::PacketHeader accept{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id, 0};
        if (!transfer::MessageSender::send_header(socket, accept)) {
            if (callbacks_.on_error)
                callbacks_.on_error("Connection lost while accepting file.");
            return;
        }
    }

    auto state = transfer::MessageReceiver::receive_file(socket, save_path_string, meta.size, resume_offset,
                                                         callbacks_.on_progress, &stop_flag_);

    if (state == transfer::TransferState::COMPLETED) {
        CORE_LOG("Session — received: " << relative_path.generic_string());
        if (callbacks_.on_status)
            callbacks_.on_status("Received: " + relative_path.generic_string());
        if (callbacks_.on_file_complete)
            callbacks_.on_file_complete(meta.filename);
    } else if (state == transfer::TransferState::CANCELLED) {
        if (callbacks_.on_status)
            callbacks_.on_status("Cancelled: " + relative_path.generic_string());
    } else if (state == transfer::TransferState::FAILED) {
        if (callbacks_.on_error)
            callbacks_.on_error("Failed to receive: " + relative_path.generic_string());
    }
}

// ── Send files ──

void Session::process_send_batch(boost::asio::ip::tcp::socket& socket, const std::vector<std::string>& file_paths) {
    sending_ = true;

    // Build transfer jobs — expand directories recursively
    std::queue<TransferJob> jobs;

    for (const auto& path_str : file_paths) {
        fs::path path = fs::path(path_str).lexically_normal();
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
                    jobs.push({it->path().string(), to_protocol_relative_path(relative), info_.session_id});
                }
            }
        } else if (fs::is_regular_file(path)) {
            jobs.push({path.string(), to_protocol_relative_path(path.filename()), info_.session_id});
        }
    }

    if (jobs.empty()) {
        if (callbacks_.on_error)
            callbacks_.on_error("No valid files found to send.");
        sending_ = false;
        return;
    }

    CORE_LOG("Session — sending " << jobs.size() << " file(s)");
    if (callbacks_.on_status)
        callbacks_.on_status("Sending " + std::to_string(jobs.size()) + " file(s)...");

    while (!jobs.empty() && !stop_flag_) {
        auto job = jobs.front();
        jobs.pop();

        std::error_code ec;
        auto fsize = fs::file_size(job.filepath, ec);
        if (ec) {
            continue;
        }

        protocol::FileInfo file_info{job.filename, fsize, "application/octet-stream"};
        if (callbacks_.on_status)
            callbacks_.on_status("Sending: " + file_info.filename);
        if (!transfer::MessageSender::send_file_meta(socket, file_info, job.session_id)) {
            if (callbacks_.on_error)
                callbacks_.on_error("Connection lost while sending file metadata.");
            stop_flag_ = true;
            break;
        }

        // Wait for peer response
        bool job_done = false;
        while (!job_done && !stop_flag_) {
            auto resp = transfer::MessageReceiver::receive_header(socket);

            // Connection closed
            if (resp.command == 0 && resp.payload_size == 0 && resp.session_id == 0 && resp.reserved == 0) {
                if (callbacks_.on_error)
                    callbacks_.on_error("Peer disconnected during send.");
                stop_flag_ = true;
                break;
            }

            auto cmd = static_cast<protocol::CommandType>(resp.command);

            if (cmd == protocol::CommandType::PONG) {
                if (!transfer::MessageSender::send_file(socket, job.filepath, resp.session_id, 0, callbacks_.on_progress,
                                                   &stop_flag_)) {
                    if (callbacks_.on_error)
                        callbacks_.on_error("Connection lost while sending: " + job.filename);
                    stop_flag_ = true;
                    job_done = true;
                    break;
                }
                job_done = true;
                CORE_LOG("Session — sent: " << job.filename);
                if (callbacks_.on_file_complete)
                    callbacks_.on_file_complete(job.filename);
            } else if (cmd == protocol::CommandType::RESUME) {
                uint64_t offset = decode_resume_offset(resp);
                if (!transfer::MessageSender::send_file(socket, job.filepath, resp.session_id, offset, callbacks_.on_progress,
                                                   &stop_flag_)) {
                    if (callbacks_.on_error)
                        callbacks_.on_error("Connection lost while sending: " + job.filename);
                    stop_flag_ = true;
                    job_done = true;
                    break;
                }
                job_done = true;
                CORE_LOG("Session — sent (resumed): " << job.filename);
                if (callbacks_.on_file_complete)
                    callbacks_.on_file_complete(job.filename);
            } else if (cmd == protocol::CommandType::CANCEL || cmd == protocol::CommandType::FILE_REJECT) {
                job_done = true;
                if (callbacks_.on_status)
                    callbacks_.on_status("Peer rejected: " + job.filename);
                if (callbacks_.on_file_complete)
                    callbacks_.on_file_complete(job.filename);
            } else if (cmd == protocol::CommandType::PING) {
                protocol::PacketHeader pong{static_cast<uint32_t>(protocol::CommandType::PONG), 0, resp.session_id, 0};
                transfer::MessageSender::send_header(socket, pong);
            }
        }
    }

    if (!stop_flag_ && callbacks_.on_status)
        callbacks_.on_status("All files sent.");

    sending_ = false;
}

void Session::process_send_batch_with_names(boost::asio::ip::tcp::socket& socket,
                                            const std::vector<std::pair<std::string, std::string>>& files) {
    sending_ = true;

    std::queue<networking::TransferJob> jobs;
    for (const auto& file : files) {
        fs::path path(file.first);
        if (fs::is_regular_file(path) || path.string().find("/proc/self/fd/") == 0) {
            jobs.push({path.string(), file.second, info_.session_id});
        }
    }

    if (jobs.empty()) {
        if (callbacks_.on_error)
            callbacks_.on_error("No valid files found to send.");
        sending_ = false;
        return;
    }

    CORE_LOG("Session — sending " << jobs.size() << " file(s) with explicit names");
    FD_LOG_INFO("[DIAG] process_send_batch_with_names() — " << jobs.size() << " jobs"
                << " socket.is_open=" << socket.is_open()
                << " stop_flag=" << stop_flag_.load());
    if (callbacks_.on_status)
        callbacks_.on_status("Sending " + std::to_string(jobs.size()) + " file(s)...");

    while (!jobs.empty() && !stop_flag_) {
        auto job = jobs.front();
        jobs.pop();

        std::error_code ec;
        auto fsize = fs::file_size(job.filepath, ec);
        if (ec) {
            FD_LOG_WARN("[DIAG] process_send_batch_with_names() — skipping file, file_size error: "
                        << job.filepath << " — " << ec.message());
            continue;
        }

        FD_LOG_INFO("[DIAG] process_send_batch_with_names() — sending file=\"" << job.filename
                    << "\" path=" << job.filepath
                    << " size=" << fsize
                    << " socket.is_open=" << socket.is_open());

        protocol::FileInfo file_info{job.filename, fsize, "application/octet-stream"};
        if (callbacks_.on_status)
            callbacks_.on_status("Sending: " + file_info.filename);
        if (!transfer::MessageSender::send_file_meta(socket, file_info, job.session_id)) {
            if (callbacks_.on_error)
                callbacks_.on_error("Connection lost while sending file metadata.");
            stop_flag_ = true;
            break;
        }

        // Wait for peer response
        bool job_done = false;
        while (!job_done && !stop_flag_) {
            auto resp = transfer::MessageReceiver::receive_header(socket);

            if (resp.command == 0 && resp.payload_size == 0 && resp.session_id == 0 && resp.reserved == 0) {
                if (callbacks_.on_error)
                    callbacks_.on_error("Peer disconnected during send.");
                stop_flag_ = true;
                break;
            }

            auto cmd = static_cast<protocol::CommandType>(resp.command);

            if (cmd == protocol::CommandType::PONG) {
                if (!transfer::MessageSender::send_file(socket, job.filepath, resp.session_id, 0, callbacks_.on_progress,
                                                   &stop_flag_)) {
                    if (callbacks_.on_error)
                        callbacks_.on_error("Connection lost while sending: " + job.filename);
                    stop_flag_ = true;
                    job_done = true;
                    break;
                }
                job_done = true;
                CORE_LOG("Session — sent: " << job.filename);
                if (callbacks_.on_file_complete)
                    callbacks_.on_file_complete(job.filename);
            } else if (cmd == protocol::CommandType::RESUME) {
                uint64_t offset = decode_resume_offset(resp);
                if (!transfer::MessageSender::send_file(socket, job.filepath, resp.session_id, offset, callbacks_.on_progress,
                                                   &stop_flag_)) {
                    if (callbacks_.on_error)
                        callbacks_.on_error("Connection lost while sending: " + job.filename);
                    stop_flag_ = true;
                    job_done = true;
                    break;
                }
                job_done = true;
                CORE_LOG("Session — sent (resumed): " << job.filename);
                if (callbacks_.on_file_complete)
                    callbacks_.on_file_complete(job.filename);
            } else if (cmd == protocol::CommandType::CANCEL || cmd == protocol::CommandType::FILE_REJECT) {
                job_done = true;
                if (callbacks_.on_status)
                    callbacks_.on_status("Peer rejected: " + job.filename);
                if (callbacks_.on_file_complete)
                    callbacks_.on_file_complete(job.filename);
            } else if (cmd == protocol::CommandType::PING) {
                protocol::PacketHeader pong{static_cast<uint32_t>(protocol::CommandType::PONG), 0, resp.session_id, 0};
                transfer::MessageSender::send_header(socket, pong);
            }
        }
    }

    if (!stop_flag_ && callbacks_.on_status)
        callbacks_.on_status("All files sent.");

    sending_ = false;
}

// ── Public methods ──

void Session::queue_send_files(const std::vector<std::string>& file_paths) {
    std::lock_guard<std::mutex> lock(send_mtx_);
    send_queue_.push(file_paths);
    CORE_LOG("Session::queue_send_files() — " << file_paths.size() << " path(s) queued");
}

void Session::queue_send_files_with_names(const std::vector<std::pair<std::string, std::string>>& files) {
    std::lock_guard<std::mutex> lock(send_mtx_);
    send_queue_with_names_.push(files);
    CORE_LOG("Session::queue_send_files_with_names() — " << files.size() << " path(s) queued");
}

void Session::set_save_dir(const std::string& dir) {
    save_dir_ = dir;
    if (!save_dir_.empty()) {
        std::error_code ec;
        fs::create_directories(save_dir_, ec);
    }
}

void Session::disconnect() {
    CORE_LOG("Session::disconnect()");
    stop_flag_ = true;
    std::lock_guard<std::mutex> lock(socket_mtx_);
    if (active_socket_) {
        try {
            protocol::PacketHeader end{static_cast<uint32_t>(protocol::CommandType::SESSION_END), 0, info_.session_id, 0};
            transfer::MessageSender::send_header(*active_socket_, end);
        } catch (...) {}
        boost::system::error_code ec;
        active_socket_->close(ec);
    }
    if (active_acceptor_) {
        boost::system::error_code ec;
        active_acceptor_->close(ec);
    }
}

void Session::stop() {
    CORE_LOG("Session::stop()");
    stop_flag_ = true;
    std::lock_guard<std::mutex> lock(socket_mtx_);
    if (active_socket_) {
        try {
            protocol::PacketHeader end{static_cast<uint32_t>(protocol::CommandType::SESSION_END), 0, info_.session_id, 0};
            transfer::MessageSender::send_header(*active_socket_, end);
        } catch (...) {}
        boost::system::error_code ec;
        active_socket_->close(ec);
    }
    if (active_acceptor_) {
        boost::system::error_code ec;
        active_acceptor_->close(ec);
    }
}

bool Session::is_connected() const {
    return connected_;
}

uint16_t Session::get_pin() const {
    return pin_;
}

unsigned short Session::get_port() const {
    return port_;
}

std::string Session::get_ip() const {
    return ip_;
}

} // namespace networking
