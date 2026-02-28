#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace security {

// Generate a random 4-digit PIN (1000-9999)
uint16_t generate_pin();

// Hash a PIN string using libsodium crypto_generichash (BLAKE2b)
// Returns a 32-byte hash as a hex string
std::string hash_pin(const std::string& pin);

// Verify a PIN against a hash
bool verify_pin(const std::string& pin, const std::string& expected_hash);

} // namespace security
