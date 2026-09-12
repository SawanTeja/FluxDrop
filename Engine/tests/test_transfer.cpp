#include "protocol/file_meta.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

TEST(TransferTest, FileInfoJsonSerialization) {
    protocol::FileInfo info{"test_file.txt", 1048576, "text/plain"};
    nlohmann::json j = info;

    EXPECT_EQ(j["filename"], "test_file.txt");
    EXPECT_EQ(j["size"], 1048576);
    EXPECT_EQ(j["mime"], "text/plain");

    protocol::FileInfo decoded = j.get<protocol::FileInfo>();
    EXPECT_EQ(decoded.filename, "test_file.txt");
    EXPECT_EQ(decoded.size, 1048576);
    EXPECT_EQ(decoded.mime, "text/plain");
}

TEST(TransferTest, FileInfoEdgeCases) {
    protocol::FileInfo info{"", 0, ""};
    nlohmann::json j = info;

    protocol::FileInfo decoded = j.get<protocol::FileInfo>();
    EXPECT_EQ(decoded.filename, "");
    EXPECT_EQ(decoded.size, 0);
    EXPECT_EQ(decoded.mime, "");
}

TEST(TransferTest, FileInfoSpecialCharacters) {
    protocol::FileInfo info{"../../etc/passwd \n \t 🚀", 0xFFFFFFFFFFFFFFFF, "application/octet-stream"};
    nlohmann::json j = info;

    protocol::FileInfo decoded = j.get<protocol::FileInfo>();
    EXPECT_EQ(decoded.filename, "../../etc/passwd \n \t 🚀");
    EXPECT_EQ(decoded.size, 0xFFFFFFFFFFFFFFFF);
    EXPECT_EQ(decoded.mime, "application/octet-stream");
}
