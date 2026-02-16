# Concurrent Exchange Simulator

A **quant-grade multithreaded exchange simulator** built in modern **C++20** with a focus on low-latency performance, cache efficiency, and proper concurrency primitives.

## Features

- **High-throughput producer-consumer order ingestion** using semaphore-based bounded ring buffer
- **Cache-aware limit order book** with O(1) order operations
- **Price-time priority matching engine** with full trade execution
- **Zero-allocation hot path** after initialization using object pools
- **Async logging** that never blocks the matching thread
- **Comprehensive latency metrics** with P99 reporting

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Concurrent Exchange Simulator                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                               │
│  │ Trader 1 │  │ Trader 2 │  │ Trader N │   (Producer Threads)          │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘                               │
│       │             │             │                                      │
│       ▼             ▼             ▼                                      │
│  ┌──────────────────────────────────────────────┐                       │
│  │          SPSC Semaphore Queue                │  ◄── Bounded Ring     │
│  │  ┌─────────────────────────────────────────┐ │      Buffer with      │
│  │  │ free_slots: counting_semaphore<Cap>     │ │      Semaphore        │
│  │  │ filled_slots: counting_semaphore<Cap>   │ │      Coordination     │
│  │  │ [OrderEvent][OrderEvent][...]           │ │                       │
│  │  └─────────────────────────────────────────┘ │                       │
│  └─────────────────────┬────────────────────────┘                       │
│                        │                                                 │
│                        ▼                                                 │
│  ┌──────────────────────────────────────────────┐                       │
│  │            Matching Engine                    │  (Consumer Thread)   │
│  │  ┌────────────────────────────────────────┐  │                       │
│  │  │  Order Book (mutex protected)          │  │                       │
│  │  │  ┌─────────────────┬─────────────────┐ │  │                       │
│  │  │  │ Bids (desc)     │ Asks (asc)      │ │  │                       │
│  │  │  │ ┌─────────────┐ │ ┌─────────────┐ │ │  │                       │
│  │  │  │ │ PriceLevel  │ │ │ PriceLevel  │ │ │  │                       │
│  │  │  │ │ @10010 [→→] │ │ │ @10020 [→→] │ │ │  │                       │
│  │  │  │ │ @10000 [→→] │ │ │ @10030 [→→] │ │ │  │                       │
│  │  │  │ └─────────────┘ │ └─────────────┘ │ │  │                       │
│  │  │  └─────────────────┴─────────────────┘ │  │                       │
│  │  └────────────────────────────────────────┘  │                       │
│  │  ┌────────────────────────────────────────┐  │                       │
│  │  │  Accounts (striped mutex)              │  │                       │
│  │  └────────────────────────────────────────┘  │                       │
│  │  ┌────────────────────────────────────────┐  │                       │
│  │  │  Stats (atomics + latency histogram)   │  │                       │
│  │  └────────────────────────────────────────┘  │                       │
│  └──────────────────────────────────────────────┘                       │
│                        │                                                 │
│                        ▼                                                 │
│  ┌──────────────────────────────────────────────┐                       │
│  │  Async Logger (lock-free ring → file)        │  (Background Thread) │
│  └──────────────────────────────────────────────┘                       │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

## Key Design Decisions

### Concurrency Primitives

| Primitive | Usage | Purpose |
|-----------|-------|---------|
| `std::mutex` | OrderBook, Accounts | **Data Protection** - Guards shared state during mutations |
| `std::counting_semaphore` | SPSC Queue | **Signaling & Coordination** - No busy-wait loops for producer/consumer |

**Why both?**
- **Mutex** provides mutual exclusion for complex data structure modifications
- **Semaphore** provides efficient blocking without spinning, perfect for bounded queues

### Memory Management

- **ObjectPool<Order>**: Fixed-capacity pool with O(1) allocate/free via freelist indices
- **Preallocated vectors**: All containers `reserve()` at construction
- **No heap allocation in hot path**: After initialization, matching uses only preallocated memory

### Cache Efficiency

- **Flat price levels**: `std::vector<PriceLevel>` instead of `std::map` for sequential access
- **Intrusive linked lists**: Orders linked via indices, not pointers
- **Cache-line alignment**: Critical structures aligned to 64 bytes to avoid false sharing
- **Dense order lookup**: `std::unordered_map` with low load factor and reserved capacity

## Building

### Prerequisites

- C++20 compatible compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.20+
- Git (for FetchContent dependencies)

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/yourusername/concurrent-exchange-sim.git
cd concurrent-exchange-sim

# Create build directory
mkdir build && cd build

# Configure (Release mode with native optimizations)
cmake -DCMAKE_BUILD_TYPE=Release -DCES_NATIVE_OPT=ON ..

# Build
cmake --build . --config Release -j$(nproc)
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CES_BUILD_TESTS` | ON | Build unit tests |
| `CES_BUILD_BENCHMARKS` | ON | Build benchmarks |
| `CES_NATIVE_OPT` | OFF | Enable `-march=native` optimization |

## Running

### Simulation

```bash
./ces_sim --orders 100000 --pin

# All options:
#   --orders N      Total orders to generate (default: 10000)
#   --traders T     Number of trader threads (default: 1, SPSC queue)
#   --seed S        Random seed (default: 12345)
#   --pin           Enable thread pinning
#   --log FILE      Log file path
```

> **Note**: The current implementation uses a Single-Producer Single-Consumer (SPSC) queue,
> so only 1 trader thread is supported. Future versions may support MPSC for multiple producers.

### CSV Replay

```bash
./ces_replay data/sample_orders.csv
```

### Unit Tests

```bash
ctest --output-on-failure
# or
./tests/ces_tests
```

### Benchmarks

```bash
./benchmarks/ces_bench_order_book
./benchmarks/ces_bench_engine
```

## Performance

### Example Output (AMD Ryzen 9, Release Build, -march=native)

```
=== Performance Results ===
Total time:         1.234 seconds
Orders processed:   1000000
Throughput:         810,372 orders/second

=== Engine Statistics ===
  Trades:       487,234
  Volume:       24,361,700

=== Latency Statistics ===
  Samples:  1000000
  Mean:     1.23 µs
  Median:   0.98 µs
  P90:      1.87 µs
  P95:      2.34 µs
  P99:      4.12 µs     ◄── Key metric for low-latency systems
  P99.9:    8.76 µs
  Min:      0.42 µs
  Max:      123.45 µs
```

### Benchmark Results

```
BM_AddOrder              45.2 ns     (22 million orders/sec)
BM_CancelOrder           38.7 ns
BM_MatchHotPath         156.3 ns
BM_BestBidAsk            12.1 ns
```

## Code Structure

```
concurrent-exchange-sim/
├── CMakeLists.txt              # Main build configuration
├── include/ces/
│   ├── common/
│   │   ├── types.hpp           # Strong types: Price, Qty, OrderId
│   │   ├── time.hpp            # High-resolution timing
│   │   ├── concepts.hpp        # C++20 concepts
│   │   └── macros.hpp          # Performance hints, cache alignment
│   ├── concurrency/
│   │   ├── ring_buffer.hpp     # Basic ring buffer
│   │   ├── spsc_semaphore_queue.hpp  # Semaphore-based SPSC queue
│   │   └── pinning.hpp         # Thread affinity utilities
│   ├── memory/
│   │   ├── object_pool.hpp     # Fixed-capacity object pool
│   │   └── arena.hpp           # Bump allocator
│   ├── lob/
│   │   ├── order.hpp           # Order struct and events
│   │   ├── price_level.hpp     # Price level with FIFO queue
│   │   └── order_book.hpp      # Cache-aware limit order book
│   ├── engine/
│   │   ├── matching_engine.hpp # Main consumer loop
│   │   ├── trader.hpp          # Synthetic order generator
│   │   ├── accounts.hpp        # Thread-safe account management
│   │   └── risk.hpp            # Pre-trade risk checks
│   ├── logging/
│   │   └── async_logger.hpp    # Non-blocking async logger
│   └── metrics/
│       ├── latency.hpp         # Latency histogram
│       └── stats.hpp           # Engine statistics
├── src/                        # Implementation files
├── tests/                      # GoogleTest unit tests
├── benchmarks/                 # Google Benchmark files
├── tools/                      # CSV replay tool
├── data/                       # Sample order data
└── scripts/                    # Build and benchmark scripts
```

## Thread Safety Model

### Single-Writer Optimization

The default mode has one matching engine thread as the **only writer** to the order book. The mutex still exists because:

1. **Future extensibility**: Easy to add concurrent market data snapshots
2. **Correctness guarantees**: Protects against accidental concurrent access
3. **Negligible overhead**: Uncontended mutex is ~25ns on modern hardware

### Account Striping

The `Accounts` class uses **striped mutexes** to reduce contention:

```cpp
// 16 stripes by default
mutex_index = trader_id % 16;
```

This allows concurrent updates to different accounts without global locking.

## Future Improvements

- [ ] **Robin Hood Hashing**: Replace `std::unordered_map` with custom dense hash map
- [ ] **Gap Buffer**: Optimize price level insertion with gap buffer
- [ ] **Lock-Free Snapshots**: RCU-style market data snapshots
- [ ] **Disruptor Pattern**: Multi-producer ring buffer with sequence barriers
- [ ] **FPGA Offload**: Hardware-accelerated matching for sub-microsecond latency
- [ ] **WebSocket Integration**: Real-time market data streaming
- [ ] **FIX Protocol**: Standard financial messaging support

## License

MIT License - see [LICENSE](LICENSE) for details.

## Contributing

Contributions welcome! Please read our coding guidelines:

1. Follow the existing code style
2. Add tests for new functionality
3. Ensure benchmarks don't regress
4. Document any new public APIs
