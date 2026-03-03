cmake_minimum_required(VERSION 3.18)
project(fluxdrop_android)

set(CMAKE_CXX_STANDARD 20)

# Core library source directory
set(CORE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src")
set(CORE_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../include")

# --- Third-party dependencies ---

# Boost (header-only Asio)
set(BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost")
include_directories(${BOOST_ROOT})
add_definitions(-DBOOST_ASIO_NO_DEPRECATED)

# nlohmann_json (header-only)
set(JSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann")
include_directories(${JSON_DIR})

# libsodium (pre-built for Android)
set(SODIUM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium")
include_directories(${SODIUM_DIR}/include)
add_library(sodium STATIC IMPORTED)
set_target_properties(sodium PROPERTIES
    IMPORTED_LOCATION ${SODIUM_DIR}/lib/${ANDROID_ABI}/libsodium.a
)

# --- Core library ---
add_library(fluxdrop_core STATIC
    ${CORE_SRC_DIR}/networking.cpp
    ${CORE_SRC_DIR}/transfer.cpp
    ${CORE_SRC_DIR}/packet.cpp
    ${CORE_SRC_DIR}/security.cpp
    ${CORE_SRC_DIR}/core_api.cpp
)

target_include_directories(fluxdrop_core PUBLIC ${CORE_INC_DIR})

# --- JNI bridge ---
add_library(fluxdrop_jni SHARED jni_bridge.cpp)

target_link_libraries(fluxdrop_jni
    fluxdrop_core
    sodium
    log
    android
)
