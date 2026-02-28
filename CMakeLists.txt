cmake_minimum_required(VERSION 3.10)
project(FluxDrop)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include directories
include_directories(include)

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

add_executable(fluxdrop src/main.cpp src/networking.cpp src/transfer.cpp src/packet.cpp src/security.cpp)
target_include_directories(fluxdrop PRIVATE ${Boost_INCLUDE_DIRS} ${SODIUM_INCLUDE_DIRS})
target_link_libraries(fluxdrop PRIVATE Threads::Threads ${SODIUM_LIBRARIES})
