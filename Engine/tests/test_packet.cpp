#include "protocol/packet.hpp"
#include <gtest/gtest.h>

TEST(PacketTest, SerializeDeserializeHeader) {
    protocol::PacketHeader header;
    header.command = static_cast<uint32_t>(protocol::CommandType::FILE_META);
    header.payload_size = 1024;
    header.session_id = 42;
    header.reserved = 0;

    auto buffer = protocol::serialize_header(header);
    protocol::PacketHeader decoded = protocol::deserialize_header(buffer);

    EXPECT_EQ(decoded.command, header.command);
    EXPECT_EQ(decoded.payload_size, header.payload_size);
    EXPECT_EQ(decoded.session_id, header.session_id);
    EXPECT_EQ(decoded.reserved, header.reserved);
}

TEST(PacketTest, SerializeDeserializeMaxValues) {
    protocol::PacketHeader header;
    header.command = 0xFFFFFFFF;
    header.payload_size = 0xFFFFFFFF;
    header.session_id = 0xFFFFFFFF;
    header.reserved = 0xFFFFFFFF;

    auto buffer = protocol::serialize_header(header);
    protocol::PacketHeader decoded = protocol::deserialize_header(buffer);

    EXPECT_EQ(decoded.command, 0xFFFFFFFF);
    EXPECT_EQ(decoded.payload_size, 0xFFFFFFFF);
    EXPECT_EQ(decoded.session_id, 0xFFFFFFFF);
    EXPECT_EQ(decoded.reserved, 0xFFFFFFFF);
}

TEST(PacketTest, SerializeDeserializeZeros) {
    protocol::PacketHeader header = {0, 0, 0, 0};
    auto buffer = protocol::serialize_header(header);
    protocol::PacketHeader decoded = protocol::deserialize_header(buffer);
    EXPECT_EQ(decoded.command, 0);
    EXPECT_EQ(decoded.payload_size, 0);
}
