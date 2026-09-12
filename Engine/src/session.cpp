#include "session.hpp"
#include "protocol/file_meta.hpp"
#include "protocol/packet.hpp"
#include "security.hpp"
#include "transfer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

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

        ip_ = get_local_ip(io_context);
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
                    udp_io, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
                udp_socket.set_option(boost::asio::socket_base::broadcast(true));
                boost::asio::ip::udp::endpoint broadcast_ep(boost::asio::ip::address_v4::broadcast(), DISCOVERY_PORT);
                boost::asio::ip::udp::endpoint multicast_ep(boost::asio::ip::make_address(MULTICAST_GROUP),
                                                            DISCOVERY_PORT);

                while (broadcasting) {
                    std::string msg = "FLUXDROP|" + std::to_string(info_.session_id) + "|" + std::to_string(port_) +
                                      "|" + get_instance_id();
                    udp_socket.send_to(boost::asio::buffer(msg), broadcast_ep);
                    udp_socket.send_to(boost::asio::buffer(msg), multicast_ep);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            } catch (...) {
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

    while (!stop_flag_) {
        // 1. Check send queue
        std::vector<std::string> files_to_send;
        {
            std::lock_guard<std::mutex> lock(send_mtx_);
            if (!send_queue_.empty()) {
                files_to_send = std::move(send_queue_.front());
                send_queue_.pop();
            }
        }
        if (!files_to_send.empty()) {
            process_send_batch(socket, files_to_send);
            continue;
        }

        // 2. Poll for incoming data
        boost::system::error_code ec;
        size_t available = socket.available(ec);

        if (ec) {
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
            } else if (cmd == protocol::CommandType::SESSION_END) {
                CORE_LOG("Session message loop — peer sent SESSION_END");
                if (callbacks_.on_status)
                    callbacks_.on_status("Peer ended the session.");
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // Graceful exit — try to send SESSION_END if we initiated the disconnect
    if (stop_flag_) {
        try {
            protocol::PacketHeader end{static_cast<uint32_t>(protocol::CommandType::SESSION_END), 0, info_.session_id,
                                       0};
            transfer::MessageSender::send_header(socket, end);
        } catch (...) {
            // Socket may already be closed — that's fine
        }
    }

    CORE_LOG("Session message loop ended");
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
        transfer::MessageSender::send_header(socket, reject);
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
        transfer::MessageSender::send_header(socket, reject);
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
        transfer::MessageSender::send_header(socket, reject);
        if (callbacks_.on_status)
            callbacks_.on_status("Rejected: " + relative_path.generic_string());
        return;
    }

    // Check for resume (.fluxpart file)
    uint64_t resume_offset = 0;
    std::string save_path_string = save_path.string();
    std::string part_file = save_path_string + ".fluxpart";
    std::error_code ec;
    auto part_size = fs::file_size(part_file, ec);
    if (!ec) {
        resume_offset = part_size;
        if (callbacks_.on_status)
            callbacks_.on_status("Resuming from " + format_size(resume_offset));
    }

    if (resume_offset > 0) {
        auto resume_hdr = make_resume_header(header.session_id, resume_offset);
        transfer::MessageSender::send_header(socket, resume_hdr);
    } else {
        protocol::PacketHeader accept{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id, 0};
        transfer::MessageSender::send_header(socket, accept);
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
        transfer::MessageSender::send_file_meta(socket, file_info);

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
                transfer::MessageSender::send_file(socket, job.filepath, resp.session_id, 0, callbacks_.on_progress,
                                                   &stop_flag_);
                job_done = true;
                CORE_LOG("Session — sent: " << job.filename);
                if (callbacks_.on_file_complete)
                    callbacks_.on_file_complete(job.filename);
            } else if (cmd == protocol::CommandType::RESUME) {
                uint64_t offset = decode_resume_offset(resp);
                transfer::MessageSender::send_file(socket, job.filepath, resp.session_id, offset, callbacks_.on_progress,
                                                   &stop_flag_);
                job_done = true;
                CORE_LOG("Session — sent (resumed): " << job.filename);
                if (callbacks_.on_file_complete)
                    callbacks_.on_file_complete(job.filename);
            } else if (cmd == protocol::CommandType::CANCEL || cmd == protocol::CommandType::FILE_REJECT) {
                job_done = true;
                if (callbacks_.on_status)
                    callbacks_.on_status("Peer rejected: " + job.filename);
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
    // The message loop will detect stop_flag_ and send SESSION_END before exiting.
    // Force-close the socket to unblock any in-progress I/O.
    std::lock_guard<std::mutex> lock(socket_mtx_);
    if (active_acceptor_) {
        boost::system::error_code ec;
        active_acceptor_->close(ec);
    }
    if (active_socket_) {
        boost::system::error_code ec;
        active_socket_->close(ec);
    }
}

void Session::stop() {
    CORE_LOG("Session::stop()");
    stop_flag_ = true;
    std::lock_guard<std::mutex> lock(socket_mtx_);
    if (active_acceptor_) {
        boost::system::error_code ec;
        active_acceptor_->close(ec);
    }
    if (active_socket_) {
        boost::system::error_code ec;
        active_socket_->close(ec);
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
