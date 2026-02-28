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

void MessageSender::send_header(boost::asio::ip::tcp::socket& socket, const protocol::PacketHeader& header) {
    try {
        auto buf = protocol::serialize_header(header);
        boost::asio::write(socket, boost::asio::buffer(buf));
    } catch (std::exception& e) {
        std::cerr << "MessageSender Exception: " << e.what() << "\n";
    }
}

void MessageSender::send_file_meta(boost::asio::ip::tcp::socket& socket, const protocol::FileInfo& info) {
    try {
        nlohmann::json j = info;
        std::string payload = j.dump();

        protocol::PacketHeader header{
            static_cast<uint32_t>(protocol::CommandType::FILE_META),
            static_cast<uint32_t>(payload.size()),
            0, 0
        };
        
        send_header(socket, header);
        boost::asio::write(socket, boost::asio::buffer(payload));
    } catch (std::exception& e) {
        std::cerr << "MessageSender Exception (meta): " << e.what() << "\n";
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

protocol::PacketHeader MessageReceiver::receive_header(boost::asio::ip::tcp::socket& socket) {
    protocol::PacketHeader empty_header{0, 0, 0, 0};
    try {
        std::array<uint8_t, 16> buf;
        boost::asio::read(socket, boost::asio::buffer(buf));
        return protocol::deserialize_header(buf);
    } catch (std::exception& e) {
        std::cerr << "MessageReceiver Exception: " << e.what() << "\n";
        return empty_header;
    }
}

protocol::FileInfo MessageReceiver::receive_file_meta(boost::asio::ip::tcp::socket& socket, uint32_t payload_size) {
    protocol::FileInfo info;
    try {
        std::vector<char> buf(payload_size);
        boost::asio::read(socket, boost::asio::buffer(buf));
        
        std::string payload(buf.begin(), buf.end());
        nlohmann::json j = nlohmann::json::parse(payload);
        info = j.get<protocol::FileInfo>();
    } catch (std::exception& e) {
        std::cerr << "MessageReceiver Exception (meta): " << e.what() << "\n";
    }
    return info;
}

} // namespace transfer
