#include "networking.hpp"
#include "transfer.hpp"
#include "security.hpp"
#include "protocol/packet.hpp"
#include "protocol/file_meta.hpp"
#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <chrono>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <iomanip>

using boost::asio::ip::tcp;

namespace networking {

std::string get_local_ip(boost::asio::io_context& io_context) {
    try {
        boost::asio::ip::udp::socket socket(io_context);
        socket.connect(boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string("8.8.8.8"), 53));
        return socket.local_endpoint().address().to_string();
    } catch (...) {
        return "127.0.0.1";
    }
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

void Server::start(std::queue<TransferJob> jobs) {
    try {
        if (jobs.empty()) {
            std::cout << "No files to transfer.\n";
            return;
        }
        
        uint32_t session_id = jobs.front().session_id;

        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0));
        
        std::string ip = get_local_ip(io_context);
        unsigned short port = acceptor.local_endpoint().port();
        
        // Generate and display PIN
        uint16_t pin = security::generate_pin();
        std::string pin_str = std::to_string(pin);
        std::string pin_hash = security::hash_pin(pin_str);
        
        std::cout << "Listening on " << ip << ":" << port << std::endl;
        std::cout << "┌──────────────────────┐\n";
        std::cout << "│  Room PIN: " << pin_str << "       │\n";
        std::cout << "└──────────────────────┘\n";
        
        // --- UDP Broadcast Thread ---
        std::atomic<bool> running{true};
        std::thread broadcast_thread([port, session_id, &running]() {
            try {
                boost::asio::io_context udp_io_context;
                boost::asio::ip::udp::socket udp_socket(udp_io_context, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
                udp_socket.set_option(boost::asio::socket_base::broadcast(true));
                
                boost::asio::ip::udp::endpoint broadcast_endpoint(boost::asio::ip::address_v4::broadcast(), 45454);
                
                while (running) {
                    std::string message = "FLUXDROP|" + std::to_string(session_id) + "|" + std::to_string(port);
                    udp_socket.send_to(boost::asio::buffer(message), broadcast_endpoint);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            } catch (...) {}
        });

        // --- TCP Acceptor ---
        tcp::socket socket(io_context);
        acceptor.accept(socket);
        
        std::cout << "Client connected. Awaiting PIN authentication...\n";
        running = false;
        if (broadcast_thread.joinable()) {
            broadcast_thread.join();
        }
        
        // --- PIN Authentication ---
        protocol::PacketHeader auth_header = transfer::MessageReceiver::receive_header(socket);
        if (auth_header.command != static_cast<uint32_t>(protocol::CommandType::AUTH)) {
            std::cerr << "Expected AUTH packet, got command: " << auth_header.command << "\n";
            return;
        }
        
        // Read the hashed PIN payload
        std::vector<char> auth_buf(auth_header.payload_size);
        boost::asio::read(socket, boost::asio::buffer(auth_buf));
        std::string received_hash(auth_buf.begin(), auth_buf.end());
        
        if (received_hash != pin_hash) {
            std::cout << "Authentication FAILED. Wrong PIN.\n";
            protocol::PacketHeader fail_header{static_cast<uint32_t>(protocol::CommandType::AUTH_FAIL), 0, session_id, 0};
            transfer::MessageSender::send_header(socket, fail_header);
            return;
        }
        
        std::cout << "Authentication successful!\n";
        protocol::PacketHeader ok_header{static_cast<uint32_t>(protocol::CommandType::AUTH_OK), 0, session_id, 0};
        transfer::MessageSender::send_header(socket, ok_header);

        while (!jobs.empty()) {
            TransferJob job = jobs.front();
            
            struct stat file_stat;
            if (stat(job.filepath.c_str(), &file_stat) != 0) {
                std::cerr << "File not found: " << job.filepath << "\n";
                jobs.pop();
                continue;
            }
            
            protocol::FileInfo file_info{job.filename, static_cast<uint64_t>(file_stat.st_size), "application/octet-stream"};
            std::cout << "Initiating transfer for " << file_info.filename << "...\n";
            transfer::MessageSender::send_file_meta(socket, file_info);

            bool job_done = false;
            while (!job_done) {
                protocol::PacketHeader header = transfer::MessageReceiver::receive_header(socket);
                
                // Checking for disconnect / empty read
                if (header.command == 0 && header.payload_size == 0 && header.session_id == 0) {
                    std::cout << "Client disconnected.\n";
                    return; // abort all jobs
                }

                if (header.command == static_cast<uint32_t>(protocol::CommandType::PING)) {
                    protocol::PacketHeader pong_header{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id, 0};
                    transfer::MessageSender::send_header(socket, pong_header);
                } else if (header.command == static_cast<uint32_t>(protocol::CommandType::PONG)) { // Implicit Accept
                    std::cout << "Client accepted " << file_info.filename << ". Sending...\n";
                    transfer::MessageSender::send_file(socket, job.filepath, header.session_id);
                    job_done = true;
                } else if (header.command == static_cast<uint32_t>(protocol::CommandType::RESUME)) {
                    uint64_t resume_offset = header.payload_size;
                    std::cout << "Client requested RESUME for " << file_info.filename << " from offset: " << resume_offset << " bytes.\n";
                    transfer::MessageSender::send_file(socket, job.filepath, header.session_id, resume_offset);
                    job_done = true;
                } else if (header.command == static_cast<uint32_t>(protocol::CommandType::CANCEL)) {
                    std::cout << "Client rejected " << file_info.filename << ".\n";
                    job_done = true; // Move to next job
                }
            }
            jobs.pop();
        }
        std::cout << "All transfers completed.\n";
    } catch (std::exception& e) {
        std::cerr << "Server Exception: " << e.what() << "\n";
    }
}

void Client::join(uint32_t room_id) {
    try {
        boost::asio::io_context io_context;
        boost::asio::ip::udp::socket socket(io_context, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 45454));
        socket.set_option(boost::asio::socket_base::reuse_address(true));
        
        std::cout << "Scanning for room " << room_id << " broadcasts...\n";
        
        while (true) {
            std::array<char, 1024> recv_buf;
            boost::asio::ip::udp::endpoint sender_endpoint;
            size_t len = socket.receive_from(boost::asio::buffer(recv_buf), sender_endpoint);
            std::string message(recv_buf.data(), len);
            
            if (message.find("FLUXDROP|") == 0) {
                // Parse format: FLUXDROP|<session>|<port>
                size_t first_pipe = message.find('|');
                size_t second_pipe = message.find('|', first_pipe + 1);
                
                if (first_pipe != std::string::npos && second_pipe != std::string::npos) {
                    uint32_t received_session = std::stoul(message.substr(first_pipe + 1, second_pipe - first_pipe - 1));
                    unsigned short port = std::stoi(message.substr(second_pipe + 1));
                    
                    if (received_session == room_id) {
                        std::string target_ip = sender_endpoint.address().to_string();
                        std::cout << "Found host: " << target_ip << " room " << room_id << "\n";
                        connect(target_ip, port);
                        return;
                    }
                }
            }
        }
    } catch (std::exception& e) {
        std::cerr << "UDP Listener Exception: " << e.what() << "\n";
    }
}

void Client::connect(const std::string& ip, unsigned short port) {
    try {
        boost::asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);
        boost::asio::connect(socket, resolver.resolve(ip, std::to_string(port)));
        std::cout << "Connected to peer!\n";
        
        // --- PIN Authentication ---
        std::cout << "Enter room PIN: ";
        std::string pin_input;
        std::getline(std::cin, pin_input);
        
        std::string hashed_pin = security::hash_pin(pin_input);
        
        // Send AUTH packet with hashed PIN as payload
        protocol::PacketHeader auth_header{
            static_cast<uint32_t>(protocol::CommandType::AUTH),
            static_cast<uint32_t>(hashed_pin.size()),
            0, 0
        };
        transfer::MessageSender::send_header(socket, auth_header);
        boost::asio::write(socket, boost::asio::buffer(hashed_pin));
        
        // Wait for AUTH response
        protocol::PacketHeader auth_response = transfer::MessageReceiver::receive_header(socket);
        if (auth_response.command == static_cast<uint32_t>(protocol::CommandType::AUTH_FAIL)) {
            std::cout << "Authentication failed. Wrong PIN.\n";
            return;
        } else if (auth_response.command != static_cast<uint32_t>(protocol::CommandType::AUTH_OK)) {
            std::cout << "Unexpected response during authentication.\n";
            return;
        }
        std::cout << "Authenticated! Waiting for file streams...\n";
        
        while (true) {
            protocol::PacketHeader header = transfer::MessageReceiver::receive_header(socket);
            
            if (header.command == 0 && header.payload_size == 0 && header.session_id == 0) {
                std::cout << "Server closed connection (All files sent or aborted).\n";
                break;
            }

            if (header.command == static_cast<uint32_t>(protocol::CommandType::FILE_META)) {
                protocol::FileInfo meta = transfer::MessageReceiver::receive_file_meta(socket, header.payload_size);
                
                std::cout << "\nIncoming file: " << meta.filename << " (" << format_size(meta.size) << ")\n";
                
                // Perform Disk Space Check
                struct statvfs disk_stat;
                if (statvfs(".", &disk_stat) != 0) {
                    std::cerr << "Error: Could not determine available disk space.\n";
                    break;
                }
                
                uint64_t available_space = disk_stat.f_bavail * disk_stat.f_frsize;
                if (available_space < meta.size) {
                    std::cout << "Error: Insufficient disk space! Requires " << format_size(meta.size) 
                              << " but only " << format_size(available_space) << " available.\n";
                    std::cout << "Rejecting file automatically.\n";
                    protocol::PacketHeader reject_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0, header.session_id, 0};
                    transfer::MessageSender::send_header(socket, reject_header);
                    continue; // Skip to next file
                }

                // Check if .fluxpart already exists to resume
                uint64_t resume_offset = 0;
                struct stat file_stat;
                std::string part_file = meta.filename + ".fluxpart";
                if (stat(part_file.c_str(), &file_stat) == 0) {
                    resume_offset = file_stat.st_size;
                    std::cout << "Found partial download. " << format_size(resume_offset) << " out of " << format_size(meta.size) << " downloaded.\n";
                }

                // Prompt user
                std::cout << "Accept? (y/n) ";
                std::string answer;
                std::getline(std::cin, answer);
                
                if (answer == "y" || answer == "Y") {
                    if (resume_offset > 0) {
                        std::cout << "Resuming file transfer from offset...\n";
                        protocol::PacketHeader resume_header{static_cast<uint32_t>(protocol::CommandType::RESUME), static_cast<uint32_t>(resume_offset), header.session_id, 0};
                        transfer::MessageSender::send_header(socket, resume_header);
                    } else {
                        std::cout << "File accepted. Downloading...\n";
                        protocol::PacketHeader accept_header{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id, 0};
                        transfer::MessageSender::send_header(socket, accept_header);
                    }
                    
                    transfer::TransferState state = transfer::MessageReceiver::receive_file(socket, meta.filename, meta.size, resume_offset);
                    if (state == transfer::TransferState::COMPLETED) {
                        std::cout << "Download complete!\n";
                    } else if (state == transfer::TransferState::CANCELLED) {
                        std::cout << "Download successfully cancelled.\n";
                    } else {
                        std::cout << "Download failed or interrupted. Leaving .fluxpart for future resume.\n";
                        break;
                    }
                } else {
                    std::cout << "File rejected.\n";
                    protocol::PacketHeader cancel_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0, header.session_id, 0};
                    transfer::MessageSender::send_header(socket, cancel_header);
                }
            } else if (header.command == static_cast<uint32_t>(protocol::CommandType::PING)) {
                protocol::PacketHeader pong_header{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id, 0};
                transfer::MessageSender::send_header(socket, pong_header);
            }
        }
    } catch (std::exception& e) {
        std::cerr << "Client Exception: " << e.what() << "\n";
    }
}

} // namespace networking
