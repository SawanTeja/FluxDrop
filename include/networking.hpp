#pragma once

#include <string>
#include <cstdint>

namespace networking {

class Server {
public:
    void start(uint32_t session_id = 482913);
};

class Client {
public:
    void connect(const std::string& ip, unsigned short port);
    void join(uint32_t room_id);
};

} // namespace networking
