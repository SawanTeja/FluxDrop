#include <iostream>
#include <string>
#include <cstdint>
#include <vector>
#include <filesystem>
#include <thread>
#include <chrono>
#include <iomanip>
#include <atomic>
#include "fluxdrop_core.h"
#include "ui/main_window.hpp"

namespace fs = std::filesystem;

static std::atomic<bool> g_transfer_done{false};

// --- Callbacks for CLI ---
void on_ready(const char* ip, int port, int pin) {
    std::cout << "Listening on " << ip << ":" << port << "\n";
    std::cout << "┌──────────────────────┐\n";
    std::cout << "│  Room PIN: " << pin << "       │\n";
    std::cout << "└──────────────────────┘\n";
}

void on_status(const char* msg) {
    std::cout << msg << "\n";
}

void on_error(const char* err) {
    std::cerr << "Error: " << err << "\n";
    g_transfer_done = true;
}

void on_progress(const char* filename, uint64_t transferred, uint64_t total, double speed_mbps) {
    int percent = (total > 0) ? (int)((transferred * 100.0) / total) : 100;
       
    std::cout << "\r" << percent << "% | " 
              << std::fixed << std::setprecision(1) << speed_mbps << " MB/s"
              << "    " << std::flush;
}

void on_complete() {
    std::cout << "\nTransfer completed.\n";
    g_transfer_done = true;
}

bool on_file_request(const char* filename, uint64_t size) {
    std::cout << "\nIncoming file: " << filename << " (" << size << " bytes)\n";
    std::cout << "Accept? (y/n) ";
    std::string ans;
    std::getline(std::cin, ans);
    return (ans == "y" || ans == "Y");
}

void on_device_found(const fd_device_t* dev) {
    std::cout << "Found host: " << dev->ip << ":" << dev->port << " room " << dev->session_id << "\n";
}

int main(int argc, char* argv[]) {
    // No arguments → launch GUI
    if (argc == 1) {
        return ui::run_gui(argc, argv);
    }

    fd_init();

    // CLI connect mode
    if (argc >= 4 && std::string(argv[1]) == "connect") {
        std::string ip = argv[2];
        unsigned short port = std::stoi(argv[3]);
        
        std::cout << "Enter room PIN: ";
        std::string pin;
        std::getline(std::cin, pin);

        std::cout << "Save files to (default: current directory): ";
        std::string save_dir;
        std::getline(std::cin, save_dir);
        if (save_dir.empty()) save_dir = ".";
        
        fd_connect(ip.c_str(), port, pin.c_str(), save_dir.c_str(),
                   on_status, on_error, on_file_request, on_progress, on_complete);
                   
        while (!g_transfer_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } 
    // CLI join mode (discovery)
    else if (argc >= 3 && std::string(argv[1]) == "join") {
        uint32_t room_id = std::stoul(argv[2]);
        std::string target_ip;
        int target_port = 0;
        
        std::cout << "Scanning for room " << room_id << " broadcasts...\n";
        
        std::atomic<bool> found{false};
        fd_start_discovery(room_id, [](const fd_device_t* dev) {
            // Note: In real C we couldn't capture variables easily without a user_data void pointer.
            // For now, this callback prints it. We should ideally stop discovery and connect.
            on_device_found(dev);
        });
        
        // This is a simplified block. We really should wait for found, grab IP/Port, stop discovery, then connect.
        // Doing a quick hack for CLI demo:
        std::cout << "Discovery started. (Auto-connect not fully implemented in this demo snippet yet. Press enter to exit)\n";
        std::cin.get();
        fd_stop_discovery();
    } 
    // CLI send mode
    else {
        std::vector<std::string> string_paths;
        for (int i = 1; i < argc; ++i) {
            string_paths.push_back(argv[i]);
        }
        
        std::vector<const char*> c_paths;
        for (const auto& p : string_paths) {
            c_paths.push_back(p.c_str());
        }

        fd_start_server(c_paths.data(), c_paths.size(),
                        on_ready, on_status, on_error, on_progress, on_complete);

        while (!g_transfer_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    fd_cleanup();
    return 0;
}
