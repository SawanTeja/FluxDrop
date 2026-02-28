#include <iostream>
#include <string>
#include <cstdint>
#include "networking.hpp"

int main(int argc, char* argv[]) {
    if (argc >= 4 && std::string(argv[1]) == "connect") {
        std::string ip = argv[2];
        unsigned short port = std::stoi(argv[3]);
        
        networking::Client client;
        client.connect(ip, port);
    } else if (argc >= 3 && std::string(argv[1]) == "join") {
        uint32_t room_id = std::stoul(argv[2]);
        networking::Client client;
        client.join(room_id);
    } else if (argc == 2) {
        uint32_t room_id = std::stoul(argv[1]);
        networking::Server server;
        server.start(room_id);
    } else {
        networking::Server server;
        server.start();
    }
    return 0;
}
