#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace protocol {

struct FileInfo {
    std::string filename;
    uint64_t size;
    std::string mime;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FileInfo, filename, size, mime)

} // namespace protocol
