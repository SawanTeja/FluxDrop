#include "networking.hpp"
#include "protocol/file_meta.hpp"
#include "protocol/packet.hpp"
#include "security.hpp"
#include "transfer.hpp"
#include <algorithm>
#include <boost/asio.hpp>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include "logger.hpp"
#include <random>
#include <stdexcept>
#include <thread>

#ifdef __ANDROID__
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#endif

using boost::asio::ip::tcp;

namespace networking {

namespace fs = std::filesystem;

namespace {

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

} // namespace

std::vector<std::string> get_network_interfaces(boost::asio::io_context& io_context) {
    std::vector<std::string> interfaces;
#ifdef __ANDROID__
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in* sa;
    if (getifaddrs(&ifap) != -1) {
        for (ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr != nullptr && ifa->ifa_addr->sa_family == AF_INET) {
                sa = (struct sockaddr_in*)ifa->ifa_addr;
                char* addr = inet_ntoa(sa->sin_addr);
                std::string s_addr(addr);
                if (s_addr != "127.0.0.1" && std::string(ifa->ifa_name).find("dummy") == std::string::npos) {
                    interfaces.push_back(s_addr);
                }
            }
        }
        freeifaddrs(ifap);
    }
#endif
    if (interfaces.empty()) {
        try {
            boost::asio::ip::udp::socket socket(io_context);
            socket.connect(boost::asio::ip::udp::endpoint(boost::asio::ip::make_address("8.8.8.8"), 53));
            interfaces.push_back(socket.local_endpoint().address().to_string());
        } catch (...) {
            interfaces.push_back("127.0.0.1");
        }
    }
    return interfaces;
}

std::string format_size(uint64_t bytes) {
    double size = bytes;
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    while (size >= 1024 && i < 4) {
        size /= 1024;
        i++;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f%s", size, units[i]);
    return std::string(buf);
}

std::string get_instance_id() {
    static std::string instance_id;
    if (instance_id.empty()) {
        const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
        for (int i = 0; i < 16; ++i) {
            instance_id += charset[dis(gen)];
        }
    }
    return instance_id;
}

// DiscoveryListener

DiscoveryListener::~DiscoveryListener() {
    stop();
}

void DiscoveryListener::start(uint32_t room_id, DeviceFoundCallback callback) {
    if (running_)
        return;
    running_ = true;

    thread_ = std::thread([this, room_id, callback]() {
        try {
            boost::asio::io_context io_context;
            boost::asio::ip::udp::socket socket(
                io_context, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), DISCOVERY_PORT));
            socket.set_option(boost::asio::socket_base::reuse_address(true));
            
            auto interfaces = get_network_interfaces(io_context);
            for (const auto& ip : interfaces) {
                try {
                    socket.set_option(boost::asio::ip::multicast::join_group(
                        boost::asio::ip::make_address(MULTICAST_GROUP).to_v4(),
                        boost::asio::ip::make_address(ip).to_v4()));
                } catch(...) {}
            }

            socket.non_blocking(true);

            // Periodically send DISCOVER request
            std::thread prober([this]() {
                try {
                    boost::asio::io_context probe_io;
                    boost::asio::ip::udp::socket probe_socket(probe_io, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
                    probe_socket.set_option(boost::asio::socket_base::broadcast(true));
                    
                    boost::asio::ip::udp::endpoint broadcast_ep(boost::asio::ip::address_v4::broadcast(), DISCOVERY_PORT);
                    boost::asio::ip::udp::endpoint multicast_ep(boost::asio::ip::make_address(MULTICAST_GROUP), DISCOVERY_PORT);
                    
                    while (running_) {
                        std::string req = "FLUXDROP_DISCOVER";
                        boost::system::error_code ec;
                        probe_socket.send_to(boost::asio::buffer(req), broadcast_ep, 0, ec);
                        probe_socket.send_to(boost::asio::buffer(req), multicast_ep, 0, ec);
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                } catch(...) {}
            });

            while (running_) {
                std::array<char, 1024> recv_buf;
                boost::asio::ip::udp::endpoint sender_endpoint;
                boost::system::error_code ec;

                size_t len = socket.receive_from(boost::asio::buffer(recv_buf), sender_endpoint, 0, ec);

                if (ec == boost::asio::error::would_block) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }
                if (ec) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                std::string sender_ip = sender_endpoint.address().to_string();

                std::string message(recv_buf.data(), len);
                if (message.find("FLUXDROP|") == 0 || message.find("FLUXDROP_RESPONSE|") == 0) {
                    size_t first_pipe = message.find('|');
                    size_t second_pipe = message.find('|', first_pipe + 1);
                    size_t third_pipe = message.find('|', second_pipe + 1);

                    if (first_pipe != std::string::npos && second_pipe != std::string::npos) {
                        DiscoveredDevice device;
                        device.session_id = std::stoul(message.substr(first_pipe + 1, second_pipe - first_pipe - 1));
                        std::string instance_id;

                        if (third_pipe != std::string::npos) {
                            device.port = std::stoi(message.substr(second_pipe + 1, third_pipe - second_pipe - 1));
                            instance_id = message.substr(third_pipe + 1);
                        } else {
                            device.port = std::stoi(message.substr(second_pipe + 1));
                        }

                        if (instance_id == get_instance_id())
                            continue; // self
                        if (device.session_id != room_id && room_id != 0)
                            continue;

                        device.ip = sender_ip;
                        if (callback)
                            callback(device);
                    }
                }
            }
            if (prober.joinable()) {
                prober.join();
            }
        } catch (std::exception& e) {
            FD_LOG_ERR("DiscoveryListener Exception: " << e.what());
        }
    });
}

void DiscoveryListener::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

// Server GUI Mode

void Server::start_gui(std::queue<TransferJob> jobs, ServerCallbacks callbacks) {
    try {
        if (jobs.empty()) {
            if (callbacks.on_error)
                callbacks.on_error("No files to transfer.");
            return;
        }

        uint32_t session_id = jobs.front().session_id;

        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0));

        auto interfaces = get_network_interfaces(io_context);
        std::string ip = interfaces.empty() ? "127.0.0.1" : interfaces.front();
        unsigned short port = acceptor.local_endpoint().port();

        uint16_t pin = security::generate_pin();
        std::string pin_str = std::to_string(pin);
        std::string pin_hash = security::hash_pin(pin_str);

        if (callbacks.on_ready)
            callbacks.on_ready(ip, port, pin);

        std::atomic<bool> broadcasting{true};
        std::thread broadcast_thread([port, session_id, &broadcasting]() {
            try {
                boost::asio::io_context udp_io;
                boost::asio::ip::udp::socket udp_socket(udp_io,
                                                        boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
                udp_socket.set_option(boost::asio::socket_base::broadcast(true));
                boost::asio::ip::udp::endpoint broadcast_ep(boost::asio::ip::address_v4::broadcast(), DISCOVERY_PORT);
                boost::asio::ip::udp::endpoint multicast_ep(boost::asio::ip::make_address(MULTICAST_GROUP),
                                                            DISCOVERY_PORT);

                while (broadcasting) {
                    std::string msg =
                        "FLUXDROP|" + std::to_string(session_id) + "|" + std::to_string(port) + "|" + get_instance_id();
                    udp_socket.send_to(boost::asio::buffer(msg), broadcast_ep);
                    udp_socket.send_to(boost::asio::buffer(msg), multicast_ep);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            } catch (...) {
            }
        });

        {
            std::lock_guard<std::mutex> lock(mtx_);
            acceptor_ = &acceptor;
        }

        while (true) {
            tcp::socket socket(io_context);

            {
                std::lock_guard<std::mutex> lock(mtx_);
                socket_ = &socket;
            }

            acceptor.non_blocking(true);
            boost::system::error_code accept_ec;
            while (true) {
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    if (stopped_) {
                        accept_ec = boost::asio::error::operation_aborted;
                        break;
                    }
                }
                if (callbacks.cancel_flag && callbacks.cancel_flag->load()) {
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

            if (accept_ec || (callbacks.cancel_flag && callbacks.cancel_flag->load())) {
                broadcasting = false;
                if (broadcast_thread.joinable())
                    broadcast_thread.join();

                if (callbacks.on_status)
                    callbacks.on_status("Sharing cancelled.");
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    socket_ = nullptr;
                }
                return;
            }

            if (callbacks.on_status)
                callbacks.on_status("Client connected. Authenticating...");

            protocol::PacketHeader auth_header = transfer::MessageReceiver::receive_header(socket);
            if (auth_header.command != static_cast<uint32_t>(protocol::CommandType::AUTH)) {
                if (callbacks.on_error)
                    callbacks.on_error("Expected AUTH packet, got: " + std::to_string(auth_header.command));
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    socket_ = nullptr;
                }
                continue;
            }

            std::vector<char> auth_buf(auth_header.payload_size);
            boost::asio::read(socket, boost::asio::buffer(auth_buf));
            std::string received_hash(auth_buf.begin(), auth_buf.end());

            if (received_hash != pin_hash) {
                if (callbacks.on_status)
                    callbacks.on_status("Authentication FAILED. Wrong PIN.");
                protocol::PacketHeader fail_header{static_cast<uint32_t>(protocol::CommandType::AUTH_FAIL), 0,
                                                   session_id, 0};
                transfer::MessageSender::send_header(socket, fail_header);
                if (callbacks.on_status)
                    callbacks.on_status("Wrong PIN entered. Waiting for correct PIN...");
                {
                    std::lock_guard<std::mutex> lock(mtx_);
                    socket_ = nullptr;
                }
                continue;
            }

            broadcasting = false;
            if (broadcast_thread.joinable())
                broadcast_thread.join();

            {
                std::lock_guard<std::mutex> lock(mtx_);
                acceptor_ = nullptr;
            }

            if (callbacks.on_status)
                callbacks.on_status("Authenticated! Sending files...");
            protocol::PacketHeader ok_header{static_cast<uint32_t>(protocol::CommandType::AUTH_OK), 0, session_id, 0};
            transfer::MessageSender::send_header(socket, ok_header);

            while (!jobs.empty()) {
                TransferJob job = jobs.front();

                std::error_code ec;
                auto fsize = std::filesystem::file_size(job.filepath, ec);
                if (ec) {
                    jobs.pop();
                    continue;
                }

                protocol::FileInfo file_info{job.filename, fsize, "application/octet-stream"};
                if (callbacks.on_status)
                    callbacks.on_status("Sending: " + file_info.filename);
                transfer::MessageSender::send_file_meta(socket, file_info, job.session_id);

                bool job_done = false;
                while (!job_done) {
                    protocol::PacketHeader header = transfer::MessageReceiver::receive_header(socket);

                    if (header.command == 0 && header.payload_size == 0 && header.session_id == 0) {
                        if (callbacks.on_error)
                            callbacks.on_error("Client disconnected.");
                        return;
                    }

                    if (header.command == static_cast<uint32_t>(protocol::CommandType::PONG)) {
                        transfer::MessageSender::send_file(socket, job.filepath, header.session_id, 0,
                                                           callbacks.on_progress, callbacks.cancel_flag);
                        job_done = true;
                    } else if (header.command == static_cast<uint32_t>(protocol::CommandType::RESUME)) {
                        uint64_t offset = decode_resume_offset(header);
                        transfer::MessageSender::send_file(socket, job.filepath, header.session_id, offset,
                                                           callbacks.on_progress, callbacks.cancel_flag);
                        job_done = true;
                    } else if (header.command == static_cast<uint32_t>(protocol::CommandType::CANCEL)) {
                        job_done = true;
                    } else if (header.command == static_cast<uint32_t>(protocol::CommandType::PING)) {
                        protocol::PacketHeader pong{static_cast<uint32_t>(protocol::CommandType::PONG), 0,
                                                    header.session_id, 0};
                        transfer::MessageSender::send_header(socket, pong);
                    }
                }
                jobs.pop();
            }

            {
                std::lock_guard<std::mutex> lock(mtx_);
                socket_ = nullptr;
            }
            break;
        }
        if (callbacks.on_complete)
            callbacks.on_complete();
    } catch (std::exception& e) {
        if (callbacks.on_error)
            callbacks.on_error(std::string("Server error: ") + e.what());
    }
}

// Client GUI Mode

void Client::connect_gui(const std::string& ip, unsigned short port, const std::string& pin,
                         const std::string& save_dir, ClientCallbacks callbacks) {
    try {
        boost::asio::io_context io_context;
        tcp::socket socket(io_context);

        struct ClientSocketGuard {
            Client* c;
            ~ClientSocketGuard() {
                std::lock_guard<std::mutex> lock(c->mtx_);
                c->socket_ = nullptr;
            }
        } cg{this};

        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (stopped_ || (callbacks.cancel_flag && callbacks.cancel_flag->load())) {
                boost::system::error_code ec;
                socket.close(ec);
            }
            socket_ = &socket;
        }
        tcp::resolver resolver(io_context);
        boost::asio::connect(socket, resolver.resolve(ip, std::to_string(port)));

        if (callbacks.on_status)
            callbacks.on_status("Connected! Authenticating...");

        std::string hashed_pin = security::hash_pin(pin);
        protocol::PacketHeader auth_header{static_cast<uint32_t>(protocol::CommandType::AUTH),
                                           static_cast<uint32_t>(hashed_pin.size()), 0, 0};
        transfer::MessageSender::send_header(socket, auth_header);
        boost::asio::write(socket, boost::asio::buffer(hashed_pin));

        protocol::PacketHeader auth_response = transfer::MessageReceiver::receive_header(socket);
        if (auth_response.command == static_cast<uint32_t>(protocol::CommandType::AUTH_FAIL)) {
            if (callbacks.on_error)
                callbacks.on_error("Authentication failed. Wrong PIN.");
            return;
        } else if (auth_response.command != static_cast<uint32_t>(protocol::CommandType::AUTH_OK)) {
            if (callbacks.on_error)
                callbacks.on_error("Unexpected auth response.");
            return;
        }

        if (callbacks.on_status)
            callbacks.on_status("Authenticated! Receiving files...");

        while (true) {
            protocol::PacketHeader header = transfer::MessageReceiver::receive_header(socket);

            if (header.command == 0 && header.payload_size == 0 && header.session_id == 0) {
                break;
            }

            if (header.command == static_cast<uint32_t>(protocol::CommandType::FILE_META)) {
                protocol::FileInfo meta = transfer::MessageReceiver::receive_file_meta(socket, header.payload_size);

                fs::path relative_path;
                try {
                    relative_path = sanitize_relative_save_path(meta.filename);
                } catch (const std::exception& ex) {
                    if (callbacks.on_error)
                        callbacks.on_error(ex.what());
                    protocol::PacketHeader reject_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0,
                                                         header.session_id, 0};
                    transfer::MessageSender::send_header(socket, reject_header);
                    continue;
                }

                if (callbacks.on_status)
                    callbacks.on_status("Receiving: " + relative_path.generic_string() + " (" + format_size(meta.size) +
                                        ")");

                fs::path base_dir = save_dir.empty() ? fs::current_path() : fs::path(save_dir);
                fs::path save_path = (base_dir / relative_path).lexically_normal();
                uint64_t available_space = available_space_for_target(save_path);
                if (available_space > 0 && available_space < meta.size) {
                    if (callbacks.on_error)
                        callbacks.on_error("Insufficient disk space. Requires " + format_size(meta.size) +
                                           " but only " + format_size(available_space) + " available.");
                    protocol::PacketHeader reject_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0,
                                                         header.session_id, 0};
                    transfer::MessageSender::send_header(socket, reject_header);
                    continue;
                }

                bool acc = true;
                if (callbacks.on_file_request) {
                    acc = callbacks.on_file_request(meta.filename, meta.size);
                }

                if (!acc) {
                    protocol::PacketHeader reject_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0,
                                                         header.session_id, 0};
                    transfer::MessageSender::send_header(socket, reject_header);
                    if (callbacks.on_status)
                        callbacks.on_status("Skipped: " + relative_path.generic_string());
                    continue;
                }

                uint64_t resume_offset = 0;
                std::string save_path_string = save_path.string();
                std::string part_file = save_path_string + ".fluxpart";
                std::error_code ec;
                auto part_size = fs::file_size(part_file, ec);
                if (!ec) {
                    resume_offset = part_size;
                    if (callbacks.on_status)
                        callbacks.on_status("Resuming from " + format_size(resume_offset));
                }

                if (resume_offset > 0) {
                    protocol::PacketHeader resume_header = make_resume_header(header.session_id, resume_offset);
                    transfer::MessageSender::send_header(socket, resume_header);
                } else {
                    protocol::PacketHeader accept{static_cast<uint32_t>(protocol::CommandType::PONG), 0,
                                                  header.session_id, 0};
                    transfer::MessageSender::send_header(socket, accept);
                }

                transfer::TransferState state = transfer::MessageReceiver::receive_file(
                    socket, save_path_string, meta.size, resume_offset, callbacks.on_progress, callbacks.cancel_flag);

                if (state == transfer::TransferState::COMPLETED) {
                    if (callbacks.on_status)
                        callbacks.on_status("Received: " + relative_path.generic_string());
                } else if (state == transfer::TransferState::CANCELLED) {
                    if (callbacks.on_status)
                        callbacks.on_status("Cancelled: " + relative_path.generic_string());
                    break;
                } else if (state == transfer::TransferState::FAILED) {
                    if (callbacks.on_error)
                        callbacks.on_error("Failed to receive: " + relative_path.generic_string());
                    break;
                }
            } else if (header.command == static_cast<uint32_t>(protocol::CommandType::PING)) {
                protocol::PacketHeader pong{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id,
                                            0};
                transfer::MessageSender::send_header(socket, pong);
            }
        }
        if (callbacks.on_complete)
            callbacks.on_complete();
    } catch (std::exception& e) {
        if (callbacks.on_error)
            callbacks.on_error(std::string("Client error: ") + e.what());
    }
}

void Server::stop() {
    std::lock_guard<std::mutex> lock(mtx_);
    stopped_ = true;
    if (acceptor_) {
        boost::system::error_code ec;
        acceptor_->close(ec);
    }
    if (socket_) {
        boost::system::error_code ec;
        socket_->close(ec);
    }
}

void Client::stop() {
    std::lock_guard<std::mutex> lock(mtx_);
    stopped_ = true;
    if (socket_) {
        boost::system::error_code ec;
        socket_->close(ec);
    }
}

} // namespace networking
