#!/bin/bash
# run_bench.sh - Run all benchmarks and generate reports

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Concurrent Exchange Simulator Benchmarks ===${NC}\n"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Build directory not found. Building project...${NC}"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake -DCMAKE_BUILD_TYPE=Release -DCES_NATIVE_OPT=ON ..
    cmake --build . --config Release -j$(nproc)
fi

cd "$BUILD_DIR"

# Run order book benchmarks
echo -e "\n${GREEN}Running Order Book Benchmarks...${NC}\n"
if [ -f "./benchmarks/ces_bench_order_book" ]; then
    ./benchmarks/ces_bench_order_book --benchmark_format=console --benchmark_counters_tabular=true
elif [ -f "./benchmarks/Release/ces_bench_order_book.exe" ]; then
    ./benchmarks/Release/ces_bench_order_book.exe --benchmark_format=console --benchmark_counters_tabular=true
else
    echo -e "${RED}Order book benchmark not found!${NC}"
fi

# Run engine latency benchmarks
echo -e "\n${GREEN}Running Engine Latency Benchmarks...${NC}\n"
if [ -f "./benchmarks/ces_bench_engine" ]; then
    ./benchmarks/ces_bench_engine --benchmark_format=console --benchmark_counters_tabular=true
elif [ -f "./benchmarks/Release/ces_bench_engine.exe" ]; then
    ./benchmarks/Release/ces_bench_engine.exe --benchmark_format=console --benchmark_counters_tabular=true
else
    echo -e "${RED}Engine benchmark not found!${NC}"
fi

# Generate JSON report if requested
if [ "$1" == "--json" ]; then
    echo -e "\n${GREEN}Generating JSON reports...${NC}\n"
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    
    ./benchmarks/ces_bench_order_book --benchmark_format=json > "bench_orderbook_${TIMESTAMP}.json" 2>/dev/null || true
    ./benchmarks/ces_bench_engine --benchmark_format=json > "bench_engine_${TIMESTAMP}.json" 2>/dev/null || true
    
    echo -e "Reports saved to build directory"
fi

echo -e "\n${GREEN}=== Benchmarks Complete ===${NC}\n"
