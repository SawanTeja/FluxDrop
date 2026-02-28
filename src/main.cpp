#include <iostream>
#include <string>
#include "networking.hpp"

int main(int argc, char* argv[]) {
    if (argc >= 4 && std::string(argv[1]) == "connect") {
        std::string ip = argv[2];
        unsigned short port = std::stoi(argv[3]);
        
        networking::Client client;
        client.connect(ip, port);
    } else {
        networking::Server server;
        server.start();
    }
    return 0;
}
