#pragma once

#include <cstdint>
#include <array>

namespace protocol {

enum class CommandType : uint32_t {
    FILE_META = 1,
    FILE_CHUNK = 2,
    CANCEL = 3,
    PING = 4,
    PONG = 5,
    RESUME = 6,
    AUTH = 7,
    AUTH_OK = 8,
    AUTH_FAIL = 9
};

struct PacketHeader {
    uint32_t command;
    uint32_t payload_size;
    uint32_t session_id;
    uint32_t reserved;
};

std::array<uint8_t, 16> serialize_header(const PacketHeader& header);
PacketHeader deserialize_header(const std::array<uint8_t, 16>& buffer);

} // namespace protocol
