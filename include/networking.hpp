#pragma once

#include <string>

namespace networking {

class Server {
public:
    void start();
};

class Client {
public:
    void connect(const std::string& ip, unsigned short port);
};

} // namespace networking
