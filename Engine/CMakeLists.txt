cmake_minimum_required(VERSION 3.10)
project(FluxDropEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include directories
include_directories(include)

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(nlohmann_json REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

# --- Core Library ---
add_library(fluxdrop_core STATIC
    src/networking.cpp
    src/transfer.cpp
    src/packet.cpp
    src/security.cpp
    src/core_api.cpp
)

target_include_directories(fluxdrop_core PUBLIC
    include
    ${Boost_INCLUDE_DIRS}
    ${SODIUM_INCLUDE_DIRS}
)

target_link_libraries(fluxdrop_core PUBLIC
    Threads::Threads
    nlohmann_json::nlohmann_json
    ${Boost_LIBRARIES}
    ${SODIUM_LIBRARIES}
)

if (WIN32)
    target_link_libraries(fluxdrop_core PUBLIC ws2_32 mswsock bcrypt)
endif()
