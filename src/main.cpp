#include <iostream>
#include <string>
#include <cstdint>
#include <queue>
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
    } else {
        uint32_t room_id = 482913; // default
        std::queue<networking::TransferJob> jobs;
        
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            size_t slash = arg.find_last_of("/\\");
            std::string filename = (slash == std::string::npos) ? arg : arg.substr(slash + 1);
            jobs.push({arg, filename, room_id});
        }
        
        // If no files were provided, create a dummy job for backwards compatibility testing
        if (jobs.empty()) {
            jobs.push({"movie.mkv", "movie.mkv", room_id});
        }

        networking::Server server;
        server.start(jobs);
    }
    return 0;
}
