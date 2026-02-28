#include "networking.hpp"
#include "transfer.hpp"
#include "protocol/packet.hpp"
#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <chrono>

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

        while (true) {
            protocol::PacketHeader header = transfer::MessageReceiver::receive_header(socket);
            
            // Checking for disconnect / empty read
            if (header.command == 0 && header.payload_size == 0 && header.session_id == 0) {
                std::cout << "Client disconnected.\n";
                break;
            }

            if (header.command == static_cast<uint32_t>(protocol::CommandType::PING)) {
                std::cout << "Received PING, sending PONG...\n";
                protocol::PacketHeader pong_header{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id, 0};
                transfer::MessageSender::send_header(socket, pong_header);
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
        
        std::cout << "Connected to peer\n";
        
        uint32_t session_id = 42; // arbitrary connection session id

        while (true) {
            std::cout << "Sending PING...\n";
            protocol::PacketHeader ping_header{static_cast<uint32_t>(protocol::CommandType::PING), 0, session_id, 0};
            transfer::MessageSender::send_header(socket, ping_header);
            
            protocol::PacketHeader header = transfer::MessageReceiver::receive_header(socket);
            if (header.command == static_cast<uint32_t>(protocol::CommandType::PONG)) {
                std::cout << "Received PONG from server.\n";
            } else {
                std::cout << "Failed to receive valid PONG, disconnecting.\n";
                break;
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    } catch (std::exception& e) {
        std::cerr << "Client Exception: " << e.what() << "\n";
    }
}

} // namespace networking
