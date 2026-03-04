cmake_minimum_required(VERSION 3.10)
project(FluxDropGUI)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find GTK4 and dependencies for the static core
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(SODIUM REQUIRED libsodium)
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)

# Define the GUI executable
add_executable(fluxdrop_gui WIN32
    src/main.cpp
    src/ui/main_window.cpp
    src/ui/device_list.cpp
    src/ui/file_sender.cpp
    src/ui/transfer_dialog.cpp
)

# Include core headers and local UI headers
target_include_directories(fluxdrop_gui PRIVATE
    include
    ../include
    ${GTK4_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${SODIUM_INCLUDE_DIRS}
)

# Link against the pre-built core library and its transitive dependencies
target_link_libraries(fluxdrop_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../build/libfluxdrop_core.a
    ${GTK4_LIBRARIES}
    Threads::Threads
    nlohmann_json::nlohmann_json
    ${Boost_LIBRARIES}
    ${SODIUM_LIBRARIES}
)

if (WIN32)
    target_link_libraries(fluxdrop_gui PRIVATE ws2_32 mswsock bcrypt)
endif()

target_compile_options(fluxdrop_gui PRIVATE ${GTK4_CFLAGS_OTHER})

# Copy assets to build directory so the executable can find them
add_custom_command(TARGET fluxdrop_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:fluxdrop_gui>/assets
)
