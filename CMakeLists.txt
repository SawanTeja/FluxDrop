cmake_minimum_required(VERSION 3.10)
project(FluxDrop)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include directories
include_directories(include)

add_executable(fluxdrop src/main.cpp)
