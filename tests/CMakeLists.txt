# ============================================================================
# CES Unit Tests
# ============================================================================

add_executable(ces_tests
    test_matching.cpp
    test_order_book.cpp
    test_ring_buffer.cpp
)

target_link_libraries(ces_tests PRIVATE
    ces_core
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(ces_tests)
