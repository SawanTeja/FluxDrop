#include "transfer.hpp"
#include <iostream>
#include <vector>

namespace transfer {

void MessageSender::send(boost::asio::ip::tcp::socket& socket, const std::string& message) {
    try {
        std::string msg = message + "\n";
        boost::asio::write(socket, boost::asio::buffer(msg));
    } catch (std::exception& e) {
        std::cerr << "MessageSender Exception: " << e.what() << "\n";
    }
}

std::string MessageReceiver::receive(boost::asio::ip::tcp::socket& socket) {
    try {
        boost::asio::streambuf buf;
        boost::asio::read_until(socket, buf, '\n');
        
        std::istream is(&buf);
        std::string message;
        std::getline(is, message);
        return message;
    } catch (std::exception& e) {
        std::cerr << "MessageReceiver Exception: " << e.what() << "\n";
        return "";
    }
}

} // namespace transfer
