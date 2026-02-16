cmake_minimum_required(VERSION 3.20)
project(concurrent-exchange-sim
    VERSION 1.0.0
    DESCRIPTION "Quant-grade multithreaded exchange simulator"
    LANGUAGES CXX
)

# ============================================================================
# C++20 Standard
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Build Options
# ============================================================================
option(CES_BUILD_TESTS "Build unit tests" ON)
option(CES_BUILD_BENCHMARKS "Build benchmarks" ON)
option(CES_NATIVE_OPT "Enable -march=native optimization" OFF)

# ============================================================================
# Compiler Flags
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    endif()
    
    if(CES_NATIVE_OPT)
        add_compile_options(-march=native)
    endif()
    
    # Threading support
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
elseif(MSVC)
    add_compile_options(/W4 /WX /permissive-)
    # Disable warning C4324: structure was padded due to alignment specifier
    # This is expected for cache-aligned structures
    add_compile_options(/wd4324)
    
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /DNDEBUG)
    endif()
endif()

# ============================================================================
# Include Directories
# ============================================================================
include_directories(${CMAKE_SOURCE_DIR}/include)

# ============================================================================
# Main Library
# ============================================================================
add_library(ces_core STATIC
    src/engine/matching_engine.cpp
    src/engine/trader.cpp
    src/engine/accounts.cpp
    src/lob/order_book.cpp
    src/logging/async_logger.cpp
    src/metrics/latency.cpp
)

target_include_directories(ces_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_libraries(ces_core PUBLIC Threads::Threads)
endif()

# ============================================================================
# Main Executable
# ============================================================================
add_executable(ces_sim src/main.cpp)
target_link_libraries(ces_sim PRIVATE ces_core)

# ============================================================================
# CSV Replay Tool
# ============================================================================
add_executable(ces_replay tools/replay_from_csv.cpp)
target_link_libraries(ces_replay PRIVATE ces_core)

# ============================================================================
# FetchContent for Dependencies
# ============================================================================
include(FetchContent)

# ============================================================================
# GoogleTest
# ============================================================================
if(CES_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Google Benchmark
# ============================================================================
if(CES_BUILD_BENCHMARKS)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    
    add_subdirectory(benchmarks)
endif()

# ============================================================================
# Installation
# ============================================================================
install(TARGETS ces_sim ces_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ces DESTINATION include)
