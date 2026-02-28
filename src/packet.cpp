#include "protocol/packet.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <stdexcept>

namespace protocol {

std::array<uint8_t, 16> serialize_header(const PacketHeader& header) {
    std::array<uint8_t, 16> buffer;
    uint32_t cmd = htonl(header.command);
    uint32_t payload = htonl(header.payload_size);
    uint32_t session = htonl(header.session_id);
    uint32_t res = htonl(header.reserved);

    std::memcpy(buffer.data(), &cmd, 4);
    std::memcpy(buffer.data() + 4, &payload, 4);
    std::memcpy(buffer.data() + 8, &session, 4);
    std::memcpy(buffer.data() + 12, &res, 4);

    return buffer;
}

PacketHeader deserialize_header(const std::array<uint8_t, 16>& buffer) {
    PacketHeader header;
    uint32_t cmd, payload, session, res;
    
    std::memcpy(&cmd, buffer.data(), 4);
    std::memcpy(&payload, buffer.data() + 4, 4);
    std::memcpy(&session, buffer.data() + 8, 4);
    std::memcpy(&res, buffer.data() + 12, 4);

    header.command = ntohl(cmd);
    header.payload_size = ntohl(payload);
    header.session_id = ntohl(session);
    header.reserved = ntohl(res);

    return header;
}

} // namespace protocol
