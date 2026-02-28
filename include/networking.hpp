#pragma once

#include <string>
#include <cstdint>
#include <queue>

namespace networking {

struct TransferJob {
    std::string filepath;
    std::string filename;
    uint32_t session_id;
};

class Server {
public:
    void start(std::queue<TransferJob> jobs);
};

class Client {
public:
    void connect(const std::string& ip, unsigned short port);
    void join(uint32_t room_id);
};

} // namespace networking
