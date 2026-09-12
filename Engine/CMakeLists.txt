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

add_subdirectory(../Logger logger)

# --- Core Library ---
add_library(fluxdrop_core STATIC
    src/networking.cpp
    src/transfer.cpp
    src/packet.cpp
    src/security.cpp
    src/session.cpp
    src/core_api.cpp
)

target_include_directories(fluxdrop_core PUBLIC
    include
    ${Boost_INCLUDE_DIRS}
    ${SODIUM_INCLUDE_DIRS}
)

target_link_libraries(fluxdrop_core PUBLIC
    fluxdrop_logger
    Threads::Threads
    nlohmann_json::nlohmann_json
    ${Boost_LIBRARIES}
    ${SODIUM_LIBRARIES}
)

if (WIN32)
    target_link_libraries(fluxdrop_core PUBLIC ws2_32 mswsock bcrypt)
endif()

# --- Testing ---
include(CTest)
enable_testing()

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG release-1.12.1
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()
