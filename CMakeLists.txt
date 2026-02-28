cmake_minimum_required(VERSION 3.10)
project(FluxDrop)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include directories
include_directories(include)

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

add_executable(fluxdrop src/main.cpp src/networking.cpp src/transfer.cpp src/packet.cpp)
target_include_directories(fluxdrop PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(fluxdrop PRIVATE Threads::Threads)
