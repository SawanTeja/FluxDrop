cmake_minimum_required(VERSION 3.16)
project(FluxDropGUI)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Find dependencies for the static core
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)

# Define the GUI executable
add_executable(fluxdrop_gui
    src/main.cpp
    src/ui/main_window.cpp
    src/ui/device_list.cpp
    src/ui/file_sender.cpp
    src/ui/transfer_dialog.cpp
    include/ui/main_window.hpp
    include/ui/device_list.hpp
    include/ui/file_sender.hpp
    include/ui/transfer_dialog.hpp
    src/app.rc
)

# Build without console on Windows
set_property(TARGET fluxdrop_gui PROPERTY WIN32_EXECUTABLE ON)

# Include core headers and local UI headers
target_include_directories(fluxdrop_gui PRIVATE
    include
    ../Engine/include
    ${Boost_INCLUDE_DIRS}
    ${SODIUM_INCLUDE_DIRS}
)

# Link against the pre-built core library and its transitive dependencies
target_link_libraries(fluxdrop_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/build/libfluxdrop_core.a
    Qt6::Widgets
    Threads::Threads
    nlohmann_json::nlohmann_json
    ${Boost_LIBRARIES}
    ${SODIUM_LIBRARIES}
    ws2_32
    mswsock
    bcrypt
)

# Copy assets to build directory so the executable can find them
add_custom_command(TARGET fluxdrop_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:fluxdrop_gui>/assets
)
