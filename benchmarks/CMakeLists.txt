# ============================================================================
# CES Benchmarks
# ============================================================================

add_executable(ces_bench_order_book
    bench_order_book.cpp
)

target_link_libraries(ces_bench_order_book PRIVATE
    ces_core
    benchmark::benchmark
    benchmark::benchmark_main
)

add_executable(ces_bench_engine
    bench_engine_latency.cpp
)

target_link_libraries(ces_bench_engine PRIVATE
    ces_core
    benchmark::benchmark
    benchmark::benchmark_main
)
