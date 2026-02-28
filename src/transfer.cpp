#include "transfer.hpp"
#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>

namespace transfer {

void MessageSender::send(boost::asio::ip::tcp::socket& socket, const std::string& message) {
    try {
        std::string msg = message + "\n";
        boost::asio::write(socket, boost::asio::buffer(msg));
    } catch (std::exception& e) {
        std::cerr << "MessageSender Exception: " << e.what() << "\n";
    }
}

void MessageSender::send_header(boost::asio::ip::tcp::socket& socket, const protocol::PacketHeader& header) {
    try {
        auto buf = protocol::serialize_header(header);
        boost::asio::write(socket, boost::asio::buffer(buf));
    } catch (std::exception& e) {
        std::cerr << "MessageSender Exception: " << e.what() << "\n";
    }
}

void MessageSender::send_file_meta(boost::asio::ip::tcp::socket& socket, const protocol::FileInfo& info) {
    try {
        nlohmann::json j = info;
        std::string payload = j.dump();

        protocol::PacketHeader header{
            static_cast<uint32_t>(protocol::CommandType::FILE_META),
            static_cast<uint32_t>(payload.size()),
            0, 0
        };
        
        send_header(socket, header);
        boost::asio::write(socket, boost::asio::buffer(payload));
    } catch (std::exception& e) {
        std::cerr << "MessageSender Exception (meta): " << e.what() << "\n";
    }
}

std::string MessageReceiver::receive(boost::asio::ip::tcp::socket& socket) {
    try {
        boost::asio::streambuf buf;
        boost::asio::read_until(socket, buf, '\n');
        
        std::istream is(&buf);
        std::string message;
        std::getline(is, message);
        return message;
    } catch (std::exception& e) {
        std::cerr << "MessageReceiver Exception: " << e.what() << "\n";
        return "";
    }
}

protocol::PacketHeader MessageReceiver::receive_header(boost::asio::ip::tcp::socket& socket) {
    protocol::PacketHeader empty_header{0, 0, 0, 0};
    try {
        std::array<uint8_t, 16> buf;
        boost::asio::read(socket, boost::asio::buffer(buf));
        return protocol::deserialize_header(buf);
    } catch (std::exception& e) {
        std::cerr << "MessageReceiver Exception: " << e.what() << "\n";
        return empty_header;
    }
}

protocol::FileInfo MessageReceiver::receive_file_meta(boost::asio::ip::tcp::socket& socket, uint32_t payload_size) {
    protocol::FileInfo info;
    try {
        std::vector<char> buf(payload_size);
        boost::asio::read(socket, boost::asio::buffer(buf));
        
        std::string payload(buf.begin(), buf.end());
        nlohmann::json j = nlohmann::json::parse(payload);
        info = j.get<protocol::FileInfo>();
    } catch (std::exception& e) {
        std::cerr << "MessageReceiver Exception (meta): " << e.what() << "\n";
    }
    return info;
}

bool MessageSender::send_file(boost::asio::ip::tcp::socket& socket, const std::string& filepath, uint32_t session_id, uint64_t start_offset, TransferProgressCallback progress_cb) {
    try {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Could not open file for reading: " << filepath << "\n";
            return false;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        uint64_t file_size = file.tellg();
        file.seekg(start_offset);

        uint64_t total_sent = start_offset;
        auto start_time = std::chrono::steady_clock::now();
        auto last_cb_time = start_time;

        std::vector<char> buffer(64 * 1024); // 64KB per chunk
        while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
            std::streamsize bytes_read = file.gcount();
            protocol::PacketHeader header{
                static_cast<uint32_t>(protocol::CommandType::FILE_CHUNK),
                static_cast<uint32_t>(bytes_read),
                session_id, 0
            };
            send_header(socket, header);
            boost::asio::write(socket, boost::asio::buffer(buffer.data(), bytes_read));
            total_sent += bytes_read;

            if (progress_cb) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed_since_cb = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cb_time).count();
                if (elapsed_since_cb >= 300 || total_sent == file_size) {
                    double elapsed = std::chrono::duration<double>(now - start_time).count();
                    uint64_t session_sent = total_sent - start_offset;
                    double speed = (elapsed > 0) ? (session_sent / elapsed / (1024.0 * 1024.0)) : 0;
                    std::filesystem::path p(filepath);
                    progress_cb(p.filename().string(), total_sent, file_size, speed);
                    last_cb_time = now;
                }
            }
        }
        return true;
    } catch (std::exception& e) {
        std::cerr << "MessageSender Exception (send_file): " << e.what() << "\n";
        return false;
    }
}

TransferState MessageReceiver::receive_file(boost::asio::ip::tcp::socket& socket, const std::string& filepath, uint64_t expected_size, uint64_t start_offset, TransferProgressCallback progress_cb, std::atomic<bool>* cancel_flag) {
    try {
        std::string part_file = filepath + ".fluxpart";
        
        // Ensure parent directories exist for nested file paths
        std::filesystem::path parent = std::filesystem::path(part_file).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        
        // Open in append mode if we have a start_offset
        std::ios_base::openmode mode = std::ios::binary;
        if (start_offset > 0) {
            mode |= std::ios::app;
        }
        
        std::ofstream file(part_file, mode);
        if (!file.is_open()) {
            std::cerr << "Could not open file for writing: " << part_file << "\n";
            return TransferState::FAILED;
        }

        uint64_t total_received = start_offset;
        auto start_time = std::chrono::steady_clock::now();
        auto last_print_time = start_time;

        while (total_received < expected_size) {
            if (cancel_flag && cancel_flag->load()) {
                std::cout << "\nTransfer cancelled locally.\n";
                file.close();
                // Send CANCEL to the sender
                protocol::PacketHeader cancel_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0, 0, 0};
                MessageSender::send_header(socket, cancel_header);
                return TransferState::CANCELLED;
            }

            protocol::PacketHeader header = receive_header(socket);
            
            if (header.command == static_cast<uint32_t>(protocol::CommandType::FILE_CHUNK)) {
                std::vector<char> buffer(header.payload_size);
                boost::asio::read(socket, boost::asio::buffer(buffer));
                file.write(buffer.data(), buffer.size());
                total_received += header.payload_size;

                // --- Progress Tracking ---
                auto now = std::chrono::steady_clock::now();
                auto elapsed_since_print = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print_time).count();
                
                if (elapsed_since_print >= 300 || total_received == expected_size) {
                    double elapsed_seconds = std::chrono::duration<double>(now - start_time).count();
                    uint64_t session_received = total_received - start_offset;
                    double speed_bps = (elapsed_seconds > 0) ? (session_received / elapsed_seconds) : 0;
                    double speed_mbps = speed_bps / (1024.0 * 1024.0);
                    
                    if (progress_cb) {
                        progress_cb(filepath, total_received, expected_size, speed_mbps);
                    } else {
                        // CLI mode: print to stdout
                        int percent = (expected_size > 0) ? static_cast<int>((total_received * 100.0) / expected_size) : 100;
                        uint64_t remaining_bytes = expected_size - total_received;
                        double eta_seconds = (speed_bps > 0) ? (remaining_bytes / speed_bps) : 0;
                        int eta_min = static_cast<int>(eta_seconds) / 60;
                        int eta_sec = static_cast<int>(eta_seconds) % 60;

                        std::cout << "\r" << percent << "% | " 
                                  << std::fixed << std::setprecision(1) << speed_mbps << " MB/s | "
                                  << "ETA " << std::setfill('0') << std::setw(2) << eta_min << ":"
                                  << std::setfill('0') << std::setw(2) << eta_sec << "    " << std::flush;
                    }
                    last_print_time = now;
                }

            } else if (header.command == static_cast<uint32_t>(protocol::CommandType::CANCEL)) {
                std::cout << "\nTransfer cancelled by sender.\n";
                file.close();
                std::remove(part_file.c_str());
                return TransferState::CANCELLED;
            } else if (header.command == static_cast<uint32_t>(protocol::CommandType::PING)) {
                protocol::PacketHeader pong_header{static_cast<uint32_t>(protocol::CommandType::PONG), 0, header.session_id, 0};
                MessageSender::send_header(socket, pong_header);
            }
        }
        if (!progress_cb) {
            std::cout << "\r                                                                 " << std::flush;
            std::cout << "\nFile transfer completed successfully.\n";
        }
        file.close();
        
        // Rename .fluxpart to final filename
        if (std::rename(part_file.c_str(), filepath.c_str()) != 0) {
             std::cerr << "Failed to rename temp file to: " << filepath << "\n";
             return TransferState::FAILED;
        }

        return TransferState::COMPLETED;
    } catch (std::exception& e) {
        std::cerr << "\nMessageReceiver Exception (receive_file): " << e.what() << "\n";
        return TransferState::FAILED;
    }
}

} // namespace transfer
