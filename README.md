# concurrent-exchange-sim
C++20 multithreaded exchange simulator with a high-throughput Limit Order Book and matching engine. Trader threads publish orders into a bounded ring buffer; the engine consumes and matches them. Uses mutexes for shared-state integrity and counting semaphores for efficient signaling + concurrency limiting.
