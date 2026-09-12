#include "security.hpp"
#include <gtest/gtest.h>
#include <string>

TEST(SecurityTest, GeneratePinBounds) {
    for (int i = 0; i < 100; ++i) {
        uint16_t pin = security::generate_pin();
        EXPECT_GE(pin, 1000);
        EXPECT_LE(pin, 9999);
    }
}

TEST(SecurityTest, HashConsistency) {
    std::string pin = "1234";
    std::string hash1 = security::hash_pin(pin);
    std::string hash2 = security::hash_pin(pin);
    EXPECT_EQ(hash1, hash2);
}

TEST(SecurityTest, HashDifference) {
    EXPECT_NE(security::hash_pin("1234"), security::hash_pin("1235"));
}

TEST(SecurityTest, VerifyPin) {
    std::string pin = "9999";
    std::string expected_hash = security::hash_pin(pin);
    EXPECT_TRUE(security::verify_pin(pin, expected_hash));
    EXPECT_FALSE(security::verify_pin("1111", expected_hash));
    EXPECT_FALSE(security::verify_pin("", expected_hash));
}
