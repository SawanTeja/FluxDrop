add_executable(engine_tests
    test_security.cpp
    test_packet.cpp
    test_transfer.cpp
)

target_include_directories(engine_tests PRIVATE ../include)

target_link_libraries(engine_tests PRIVATE
    fluxdrop_core
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(engine_tests)
