#pragma once

#include <string>
#include <boost/asio.hpp>

namespace transfer {

class MessageSender {
public:
    static void send(boost::asio::ip::tcp::socket& socket, const std::string& message);
};

class MessageReceiver {
public:
    static std::string receive(boost::asio::ip::tcp::socket& socket);
};

} // namespace transfer
