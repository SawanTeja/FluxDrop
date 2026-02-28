#pragma once

#include <string>
#include <functional>
#include <boost/asio.hpp>
#include "protocol/packet.hpp"
#include "protocol/file_meta.hpp"
#include <atomic>

namespace transfer {

// Progress callback: filename, bytes_transferred, bytes_total, speed_mbps
using TransferProgressCallback = std::function<void(const std::string&, uint64_t, uint64_t, double)>;

enum class TransferState {
    COMPLETED,
    CANCELLED,
    FAILED
};

class MessageSender {
public:
    static void send(boost::asio::ip::tcp::socket& socket, const std::string& message);
    static void send_header(boost::asio::ip::tcp::socket& socket, const protocol::PacketHeader& header);
    static void send_file_meta(boost::asio::ip::tcp::socket& socket, const protocol::FileInfo& info);
    static bool send_file(boost::asio::ip::tcp::socket& socket, const std::string& filepath,
                          uint32_t session_id, uint64_t start_offset = 0,
                          TransferProgressCallback progress_cb = nullptr);
};

class MessageReceiver {
public:
    static std::string receive(boost::asio::ip::tcp::socket& socket);
    static protocol::PacketHeader receive_header(boost::asio::ip::tcp::socket& socket);
    static protocol::FileInfo receive_file_meta(boost::asio::ip::tcp::socket& socket, uint32_t payload_size);
    static TransferState receive_file(boost::asio::ip::tcp::socket& socket, const std::string& filepath,
                                      uint64_t expected_size, uint64_t start_offset = 0,
                                      TransferProgressCallback progress_cb = nullptr,
                                      std::atomic<bool>* cancel_flag = nullptr);
};

} // namespace transfer
