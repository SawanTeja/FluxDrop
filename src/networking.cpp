#include "networking.hpp"
#include "transfer.hpp"
#include <iostream>
#include <boost/asio.hpp>

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

void Server::start() {
    try {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0));
        
        std::string ip = get_local_ip(io_context);
        unsigned short port = acceptor.local_endpoint().port();
        
        std::cout << "Listening on " << ip << ":" << port << std::endl;
        
        tcp::socket socket(io_context);
        acceptor.accept(socket);
        
        protocol::PacketHeader header{1, 0, 42, 0};
        transfer::MessageSender::send_header(socket, header);
        transfer::MessageSender::send(socket, "Hello from server");
    } catch (std::exception& e) {
        std::cerr << "Server Exception: " << e.what() << "\n";
    }
}

void Client::connect(const std::string& ip, unsigned short port) {
    try {
        boost::asio::io_context io_context;
        tcp::socket socket(io_context);
        tcp::resolver resolver(io_context);
        boost::asio::connect(socket, resolver.resolve(ip, std::to_string(port)));
        
        std::cout << "Connected to peer\n";
        
        protocol::PacketHeader header = transfer::MessageReceiver::receive_header(socket);
        std::cout << "Received Header - Command: " << header.command 
                  << " Payload Size: " << header.payload_size 
                  << " Session ID: " << header.session_id 
                  << " Reserved: " << header.reserved << std::endl;

        std::string response = transfer::MessageReceiver::receive(socket);
        std::cout << "Received: " << response << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Client Exception: " << e.what() << "\n";
    }
}

} // namespace networking
