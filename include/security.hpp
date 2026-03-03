#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace security {

uint16_t generate_pin();

std::string hash_pin(const std::string& pin);

bool verify_pin(const std::string& pin, const std::string& expected_hash);

} // namespace security
