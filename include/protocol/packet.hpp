#pragma once

#include <cstdint>
#include <array>

namespace protocol {

enum class CommandType : uint32_t {
    FILE_META = 1,
    FILE_CHUNK = 2,
    CANCEL = 3,
    PING = 4,
    PONG = 5
};

// Fixed 16-byte header
struct PacketHeader {
    uint32_t command;      // 4 bytes
    uint32_t payload_size; // 4 bytes
    uint32_t session_id;   // 4 bytes
    uint32_t reserved;     // 4 bytes
};

std::array<uint8_t, 16> serialize_header(const PacketHeader& header);
PacketHeader deserialize_header(const std::array<uint8_t, 16>& buffer);

} // namespace protocol
