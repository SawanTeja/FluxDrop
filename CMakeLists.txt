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
pkg_check_modules(GTK4 REQUIRED gtk4)

add_executable(fluxdrop
    src/main.cpp
    src/networking.cpp
    src/transfer.cpp
    src/packet.cpp
    src/security.cpp
    src/ui/main_window.cpp
    src/ui/device_list.cpp
    src/ui/file_sender.cpp
    src/ui/transfer_dialog.cpp
)

target_include_directories(fluxdrop PRIVATE
    ${Boost_INCLUDE_DIRS}
    ${SODIUM_INCLUDE_DIRS}
    ${GTK4_INCLUDE_DIRS}
)

target_link_libraries(fluxdrop PRIVATE
    Threads::Threads
    ${SODIUM_LIBRARIES}
    ${GTK4_LIBRARIES}
)

target_compile_options(fluxdrop PRIVATE ${GTK4_CFLAGS_OTHER})
