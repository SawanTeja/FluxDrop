#include "networking.hpp"
#include "transfer.hpp"
#include "protocol/packet.hpp"
#include "protocol/file_meta.hpp"
#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <chrono>
#include <sys/statvfs.h>
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

void Server::start(uint32_t session_id) {
    try {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0));
        
        std::string ip = get_local_ip(io_context);
        unsigned short port = acceptor.local_endpoint().port();
        
        std::cout << "Listening on " << ip << ":" << port << std::endl;
        
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
        
        std::cout << "Client connected.\n";
        running = false; // Stop broadcasting once a client connects
        if (broadcast_thread.joinable()) {
            broadcast_thread.join();
        }

        // Simulating the server requesting to send a file
        protocol::FileInfo file_info{"movie.mkv", 10 * 1024 * 1024 /* 10 MB Dummy Stream */, "video/x-matroska"};
        transfer::MessageSender::send_file_meta(socket, file_info);

        while (true) {
            protocol::PacketHeader header = transfer::MessageReceiver::receive_header(socket);
            
            // Checking for disconnect / empty read
            if (header.command == 0 && header.payload_size == 0 && header.session_id == 0) {
                std::cout << "Client disconnected.\n";
                break;
            }

            if (header.command == static_cast<uint32_t>(protocol::CommandType::PING)) {
                protocol::PacketHeader pong_header{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id, 0};
                transfer::MessageSender::send_header(socket, pong_header);
            } else if (header.command == static_cast<uint32_t>(protocol::CommandType::PONG)) { // Used here as implicit accept packet
                std::cout << "Client accepted file transfer. Sending file chunks...\n";
                transfer::MessageSender::send_file(socket, file_info.filename, header.session_id);
            } else if (header.command == static_cast<uint32_t>(protocol::CommandType::CANCEL)) {
                std::cout << "Client rejected the file transfer.\n";
                break;
            }
        }
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
        
        // Wait for server to initiate a file transfer 
        protocol::PacketHeader header = transfer::MessageReceiver::receive_header(socket);
        if (header.command == static_cast<uint32_t>(protocol::CommandType::FILE_META)) {
            protocol::FileInfo meta = transfer::MessageReceiver::receive_file_meta(socket, header.payload_size);
            
            std::cout << "Incoming file: " << meta.filename << " (" << format_size(meta.size) << ")\n";
            
            // Perform Disk Space Check
            struct statvfs stat;
            if (statvfs(".", &stat) != 0) {
                std::cerr << "Error: Could not determine available disk space.\n";
                return;
            }
            
            uint64_t available_space = stat.f_bavail * stat.f_frsize;
            if (available_space < meta.size) {
                std::cout << "Error: Insufficient disk space! Requires " << format_size(meta.size) 
                          << " but only " << format_size(available_space) << " available.\n";
                std::cout << "Rejecting file automatically.\n";
                protocol::PacketHeader reject_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0, header.session_id, 0};
                transfer::MessageSender::send_header(socket, reject_header);
                return;
            }

            // Prompt user
            std::cout << "Accept? (y/n) ";
            std::string answer;
            std::getline(std::cin, answer);
            
            if (answer == "y" || answer == "Y") {
                std::cout << "File accepted. Downloading...\n";
                protocol::PacketHeader accept_header{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id, 0};
                transfer::MessageSender::send_header(socket, accept_header);
                
                if (transfer::MessageReceiver::receive_file(socket, "downloaded_" + meta.filename, meta.size)) {
                    std::cout << "Download complete!\n";
                }
            } else {
                std::cout << "File rejected.\n";
                protocol::PacketHeader cancel_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0, header.session_id, 0};
                transfer::MessageSender::send_header(socket, cancel_header);
            }
        } else {
            std::cout << "Unexpected command received: " << header.command << "\n";
        }
    } catch (std::exception& e) {
        std::cerr << "Client Exception: " << e.what() << "\n";
    }
}

} // namespace networking
