#include "security.hpp"
#include <sodium.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace security {

uint16_t generate_pin() {
    if (sodium_init() < 0) {
        std::cerr << "libsodium initialization failed!\n";
        // Fallback to std::random_device
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dist(1000, 9999);
        return dist(gen);
    }
    // Use libsodium's secure random for PIN generation
    return 1000 + randombytes_uniform(9000); // 1000–9999
}

std::string hash_pin(const std::string& pin) {
    if (sodium_init() < 0) {
        std::cerr << "libsodium initialization failed!\n";
        return "";
    }

    unsigned char hash[crypto_generichash_BYTES]; // 32 bytes
    crypto_generichash(hash, sizeof(hash),
                       reinterpret_cast<const unsigned char*>(pin.c_str()), pin.size(),
                       nullptr, 0);

    // Convert to hex string
    std::ostringstream oss;
    for (size_t i = 0; i < sizeof(hash); ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

bool verify_pin(const std::string& pin, const std::string& expected_hash) {
    return hash_pin(pin) == expected_hash;
}

} // namespace security
