#pragma once

#include <array>
#include <cstdint>

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
    AUTH_FAIL = 9,
    SESSION_REQUEST = 10, // Reserved for future multi-guest
    SESSION_ACCEPT = 11,  // Reserved for future multi-guest
    SESSION_DENY = 12,    // Reserved for future multi-guest
    FILE_OFFER = 13,      // Reserved for future batch negotiation
    FILE_ACCEPT = 14,     // Reserved for future batch negotiation
    FILE_REJECT = 15,     // Peer declines a file before transfer starts
    SESSION_END = 16      // Graceful session disconnect
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
