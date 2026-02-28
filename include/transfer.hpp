#pragma once

#include <string>
#include <boost/asio.hpp>
#include "protocol/packet.hpp"

namespace transfer {

class MessageSender {
public:
    static void send(boost::asio::ip::tcp::socket& socket, const std::string& message);
    static void send_header(boost::asio::ip::tcp::socket& socket, const protocol::PacketHeader& header);
};

class MessageReceiver {
public:
    static std::string receive(boost::asio::ip::tcp::socket& socket);
    static protocol::PacketHeader receive_header(boost::asio::ip::tcp::socket& socket);
};

} // namespace transfer
