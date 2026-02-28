#include <iostream>
#include <string>
#include <cstdint>
#include <queue>
#include <filesystem>
#include "networking.hpp"
#include "ui/main_window.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    // No arguments → launch GUI
    if (argc == 1) {
        return ui::run_gui(argc, argv);
    }

    // CLI mode (existing behavior)
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
            fs::path path(arg);
            
            if (fs::is_directory(path)) {
                // Recursively walk the directory
                std::string base_dir = path.filename().string(); // Top-level dir name
                for (const auto& entry : fs::recursive_directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        // Preserve relative path under the base directory name
                        fs::path relative = base_dir / fs::relative(entry.path(), path);
                        jobs.push({entry.path().string(), relative.string(), room_id});
                    }
                }
                std::cout << "Queued directory: " << path.string() << " (" << jobs.size() << " files)\n";
            } else if (fs::is_regular_file(path)) {
                std::string filename = path.filename().string();
                jobs.push({arg, filename, room_id});
            } else {
                std::cerr << "Skipping invalid path: " << arg << "\n";
            }
        }
        
        // If no files were provided, create a dummy job for backwards compatibility testing
        if (jobs.empty()) {
            jobs.push({"movie.mkv", "movie.mkv", room_id});
        }

        std::cout << "Total files to transfer: " << jobs.size() << "\n";
        networking::Server server;
        server.start(jobs);
    }
    return 0;
}
