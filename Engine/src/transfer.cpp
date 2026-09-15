#include "transfer.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <vector>

#include "logger.hpp"

namespace {

const char* command_name(uint32_t cmd) {
    switch (cmd) {
    case 1: return "FILE_META";
    case 2: return "FILE_CHUNK";
    case 3: return "CANCEL";
    case 4: return "PING";
    case 5: return "PONG";
    case 6: return "RESUME";
    case 7: return "AUTH";
    case 8: return "AUTH_OK";
    case 9: return "AUTH_FAIL";
    case 15: return "FILE_REJECT";
    case 16: return "SESSION_END";
    default: return "UNKNOWN";
    }
}

bool check_socket_open(boost::asio::ip::tcp::socket& socket, const char* caller) {
    if (!socket.is_open()) {
        FD_LOG_ERR("[DIAG] " << caller << " — socket is NOT open");
        return false;
    }
    int err = 0;
#if defined(_WIN32)
    int len = sizeof(err);
    if (getsockopt(socket.native_handle(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) == 0 && err != 0) {
#else
    socklen_t len = sizeof(err);
    if (getsockopt(socket.native_handle(), SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err != 0) {
#endif
        FD_LOG_ERR("[DIAG] " << caller << " — socket has pending SO_ERROR: " << err << " (" << strerror(err) << ")");
        return false;
    }
    return true;
}

} // namespace

namespace transfer {

namespace fs = std::filesystem;

namespace {

bool replace_with_completed_file(const fs::path& part_path, const fs::path& final_path) {
    std::error_code ec;

    if (fs::exists(final_path, ec)) {
        fs::remove(final_path, ec);
        if (ec) {
            FD_LOG_ERR("Failed to remove existing destination file: " << final_path << " (" << ec.message() << ")");
            return false;
        }
    }

    fs::rename(part_path, final_path, ec);
    if (ec) {
        FD_LOG_ERR("Failed to finalize received file: " << final_path << " (" << ec.message() << ")");
        return false;
    }

    return true;
}

} // namespace

bool MessageSender::send(boost::asio::ip::tcp::socket& socket, const std::string& message) {
    if (!check_socket_open(socket, "send()")) return false;
    try {
        std::string msg = message + "\n";
        FD_LOG_DEBUG("[DIAG] send() — writing " << msg.size() << " bytes");
        boost::asio::write(socket, boost::asio::buffer(msg));
        return true;
    } catch (const boost::system::system_error& e) {
        FD_LOG_ERR("[DIAG] send() FAILED — boost error: " << e.what()
                   << " | code=" << e.code().value()
                   << " (" << e.code().category().name() << ")");
        return false;
    } catch (std::exception& e) {
        FD_LOG_ERR("[DIAG] send() FAILED — exception: " << e.what());
        return false;
    }
}

bool MessageSender::send_header(boost::asio::ip::tcp::socket& socket, const protocol::PacketHeader& header) {
    if (!check_socket_open(socket, "send_header()")) return false;
    try {
        FD_LOG_DEBUG("[DIAG] send_header() — cmd=" << command_name(header.command)
                     << "(" << header.command << ")"
                     << " payload_size=" << header.payload_size
                     << " session_id=" << header.session_id);
        auto buf = protocol::serialize_header(header);
        boost::asio::write(socket, boost::asio::buffer(buf));
        return true;
    } catch (const boost::system::system_error& e) {
        FD_LOG_ERR("[DIAG] send_header(" << command_name(header.command) << ") FAILED — boost error: " << e.what()
                   << " | code=" << e.code().value()
                   << " (" << e.code().category().name() << ")");
        return false;
    } catch (std::exception& e) {
        FD_LOG_ERR("[DIAG] send_header(" << command_name(header.command) << ") FAILED — exception: " << e.what());
        return false;
    }
}

bool MessageSender::send_file_meta(boost::asio::ip::tcp::socket& socket, const protocol::FileInfo& info,
                                   uint32_t session_id) {
    if (!check_socket_open(socket, "send_file_meta()")) return false;
    try {
        nlohmann::json j = info;
        std::string payload = j.dump();

        FD_LOG_INFO("[DIAG] send_file_meta() — file=\"" << info.filename
                    << "\" size=" << info.size
                    << " mime=" << info.mime
                    << " payload_json=" << payload.size() << " bytes"
                    << " session_id=" << session_id);

        protocol::PacketHeader header{static_cast<uint32_t>(protocol::CommandType::FILE_META),
                                      static_cast<uint32_t>(payload.size()), session_id, 0};

        if (!send_header(socket, header)) {
            FD_LOG_ERR("[DIAG] send_file_meta() — header write failed, aborting meta send");
            return false;
        }
        boost::asio::write(socket, boost::asio::buffer(payload));
        FD_LOG_INFO("[DIAG] send_file_meta() — SUCCESS");
        return true;
    } catch (const boost::system::system_error& e) {
        FD_LOG_ERR("[DIAG] send_file_meta() FAILED — boost error: " << e.what()
                   << " | code=" << e.code().value()
                   << " (" << e.code().category().name() << ")");
        return false;
    } catch (std::exception& e) {
        FD_LOG_ERR("[DIAG] send_file_meta() FAILED — exception: " << e.what());
        return false;
    }
}

std::string MessageReceiver::receive(boost::asio::ip::tcp::socket& socket) {
    try {
        boost::asio::streambuf buf;
        boost::asio::read_until(socket, buf, '\n');

        std::istream is(&buf);
        std::string message;
        std::getline(is, message);
        FD_LOG_DEBUG("[DIAG] receive() — got " << message.size() << " bytes");
        return message;
    } catch (const boost::system::system_error& e) {
        if (e.code() == boost::asio::error::eof || e.code() == boost::asio::error::operation_aborted) {
            FD_LOG_INFO("[DIAG] receive() — connection closed (" << e.code().message() << ")");
            return "";
        }
        FD_LOG_ERR("[DIAG] receive() FAILED — boost error: " << e.what()
                   << " | code=" << e.code().value()
                   << " (" << e.code().category().name() << ")");
        return "";
    } catch (std::exception& e) {
        FD_LOG_ERR("[DIAG] receive() FAILED — exception: " << e.what());
        return "";
    }
}

protocol::PacketHeader MessageReceiver::receive_header(boost::asio::ip::tcp::socket& socket) {
    protocol::PacketHeader empty_header{0, 0, 0, 0};
    if (!check_socket_open(socket, "receive_header()")) return empty_header;
    try {
        std::array<uint8_t, 16> buf;
        boost::asio::read(socket, boost::asio::buffer(buf));
        auto header = protocol::deserialize_header(buf);
        FD_LOG_DEBUG("[DIAG] receive_header() — cmd=" << command_name(header.command)
                     << "(" << header.command << ")"
                     << " payload_size=" << header.payload_size
                     << " session_id=" << header.session_id);
        return header;
    } catch (const boost::system::system_error& e) {
        if (e.code() == boost::asio::error::eof || e.code() == boost::asio::error::operation_aborted) {
            FD_LOG_INFO("[DIAG] receive_header() — connection closed (" << e.code().message() << ")");
            return empty_header;
        }
        FD_LOG_ERR("[DIAG] receive_header() FAILED — boost error: " << e.what()
                   << " | code=" << e.code().value()
                   << " (" << e.code().category().name() << ")");
        return empty_header;
    } catch (std::exception& e) {
        FD_LOG_ERR("[DIAG] receive_header() FAILED — exception: " << e.what());
        return empty_header;
    }
}

protocol::FileInfo MessageReceiver::receive_file_meta(boost::asio::ip::tcp::socket& socket, uint32_t payload_size) {
    protocol::FileInfo info;
    try {
        FD_LOG_DEBUG("[DIAG] receive_file_meta() — reading " << payload_size << " bytes of metadata");
        std::vector<char> buf(payload_size);
        boost::asio::read(socket, boost::asio::buffer(buf));

        std::string payload(buf.begin(), buf.end());
        nlohmann::json j = nlohmann::json::parse(payload);
        info = j.get<protocol::FileInfo>();
        FD_LOG_INFO("[DIAG] receive_file_meta() — file=\"" << info.filename
                    << "\" size=" << info.size << " mime=" << info.mime);
    } catch (const boost::system::system_error& e) {
        FD_LOG_ERR("[DIAG] receive_file_meta() FAILED — boost error: " << e.what()
                   << " | code=" << e.code().value()
                   << " (" << e.code().category().name() << ")");
    } catch (std::exception& e) {
        FD_LOG_ERR("[DIAG] receive_file_meta() FAILED — exception: " << e.what());
    }
    return info;
}

bool MessageSender::send_file(boost::asio::ip::tcp::socket& socket, const std::string& filepath, uint32_t session_id,
                              uint64_t start_offset, TransferProgressCallback progress_cb,
                              std::atomic<bool>* cancel_flag) {
    if (!check_socket_open(socket, "send_file()")) return false;
    FD_LOG_INFO("[DIAG] send_file() — BEGIN file=\"" << filepath
                << "\" session_id=" << session_id
                << " start_offset=" << start_offset);
    try {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            FD_LOG_ERR("[DIAG] send_file() — could not open file: " << filepath);
            protocol::PacketHeader cancel_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0, session_id, 0};
            send_header(socket, cancel_header);
            return false;
        }

        file.seekg(0, std::ios::end);
        uint64_t file_size = file.tellg();
        file.seekg(start_offset);

        FD_LOG_INFO("[DIAG] send_file() — file_size=" << file_size
                    << " remaining=" << (file_size - start_offset) << " bytes");

        uint64_t total_sent = start_offset;
        auto start_time = std::chrono::steady_clock::now();
        auto last_cb_time = start_time;

        std::vector<char> buffer(64 * 1024); // 64KB per chunk
        while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
            if (cancel_flag && cancel_flag->load()) {
                FD_LOG_INFO("[DIAG] send_file() — cancelled locally at " << total_sent << "/" << file_size);
                protocol::PacketHeader cancel_header{static_cast<uint32_t>(protocol::CommandType::CANCEL), 0,
                                                     session_id, 0};
                send_header(socket, cancel_header);
                return false;
            }

            std::streamsize bytes_read = file.gcount();
            protocol::PacketHeader header{static_cast<uint32_t>(protocol::CommandType::FILE_CHUNK),
                                          static_cast<uint32_t>(bytes_read), session_id, 0};
            if (!send_header(socket, header)) {
                FD_LOG_ERR("[DIAG] send_file() — chunk header write failed at " << total_sent << "/" << file_size);
                return false;
            }
            boost::asio::write(socket, boost::asio::buffer(buffer.data(), bytes_read));
            total_sent += bytes_read;

            if (progress_cb) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed_since_cb =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cb_time).count();
                if (elapsed_since_cb >= 300 || total_sent == file_size) {
                    double elapsed = std::chrono::duration<double>(now - start_time).count();
                    uint64_t session_sent = total_sent - start_offset;
                    double speed = (elapsed > 0) ? (session_sent / elapsed / (1024.0 * 1024.0)) : 0;
                    fs::path p(filepath);
                    progress_cb(p.filename().string(), total_sent, file_size, speed);
                    last_cb_time = now;
                }
            }
        }
        FD_LOG_INFO("[DIAG] send_file() — COMPLETED " << total_sent << "/" << file_size << " bytes");
        return true;
    } catch (const boost::system::system_error& e) {
        FD_LOG_ERR("[DIAG] send_file() FAILED — boost error: " << e.what()
                   << " | code=" << e.code().value()
                   << " (" << e.code().category().name() << ")");
        return false;
    } catch (std::exception& e) {
        FD_LOG_ERR("[DIAG] send_file() FAILED — exception: " << e.what());
        return false;
    }
}

TransferState MessageReceiver::receive_file(boost::asio::ip::tcp::socket& socket, const std::string& filepath,
                                            uint64_t expected_size, uint64_t start_offset,
                                            TransferProgressCallback progress_cb, std::atomic<bool>* cancel_flag) {
    try {
        fs::path final_path(filepath);
        fs::path part_path(filepath + ".fluxpart");

        fs::path parent = part_path.parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent);
        }

        std::ios_base::openmode mode = std::ios::binary;
        if (start_offset > 0) {
            mode |= std::ios::app;
        }

        std::ofstream file(part_path, mode);
        if (!file.is_open()) {
            FD_LOG_ERR("Could not open file for writing: " << part_path);
            return TransferState::FAILED;
        }

        uint64_t total_received = start_offset;
        auto start_time = std::chrono::steady_clock::now();
        auto last_print_time = start_time;

        while (total_received < expected_size) {
            if (cancel_flag && cancel_flag->load()) {
                FD_LOG_INFO("Transfer cancelled locally.");
                file.close();
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

                auto now = std::chrono::steady_clock::now();
                auto elapsed_since_print =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_print_time).count();

                if (elapsed_since_print >= 300 || total_received == expected_size) {
                    double elapsed_seconds = std::chrono::duration<double>(now - start_time).count();
                    uint64_t session_received = total_received - start_offset;
                    double speed_bps = (elapsed_seconds > 0) ? (session_received / elapsed_seconds) : 0;
                    double speed_mbps = speed_bps / (1024.0 * 1024.0);

                    if (progress_cb) {
                        progress_cb(filepath, total_received, expected_size, speed_mbps);
                    }
                    last_print_time = now;
                }

            } else if (header.command == static_cast<uint32_t>(protocol::CommandType::CANCEL)) {
                FD_LOG_INFO("Transfer cancelled by sender.");
                file.close();
                std::error_code ec;
                fs::remove(part_path, ec);
                return TransferState::CANCELLED;
            } else if (header.command == static_cast<uint32_t>(protocol::CommandType::PING)) {
                protocol::PacketHeader pong_header{static_cast<uint32_t>(protocol::CommandType::PONG), 0,
                                                   header.session_id, 0};
                MessageSender::send_header(socket, pong_header);
            }
        }
        if (!progress_cb) {
            FD_LOG_INFO("File transfer completed successfully.");
        }
        file.close();

        if (!replace_with_completed_file(part_path, final_path)) {
            return TransferState::FAILED;
        }

        return TransferState::COMPLETED;
    } catch (std::exception& e) {
        FD_LOG_ERR("MessageReceiver Exception (receive_file): " << e.what());
        return TransferState::FAILED;
    }
}

} // namespace transfer
