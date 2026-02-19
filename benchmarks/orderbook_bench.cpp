#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <iostream>
#include <vector>
#include <chrono>
#include <algorithm>
#include <numeric>
#include "../src/OrderBook.hpp"

class NullObserver : public ITradeObserver {
    void onTrade(uint64_t, uint64_t, uint32_t, int64_t) override {}
};

void run_latency_test(int iterations = 100000) {
    NullObserver obs;
    OrderBook book(&obs);
    std::vector<double> latencies;
    latencies.reserve(iterations);

    // Warm-up the CPU and Cache
    for (int i = 0; i < 1000; ++i) {
        book.addOrder(i, 100, 1, Side::Sell);
    }

    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();

        book.addOrder(200000 + i, 100, 1, Side::Buy);

        auto end = std::chrono::high_resolution_clock::now();
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        latencies.push_back(static_cast<double>(nanoseconds));
    }

    std::sort(latencies.begin(), latencies.end());

    double sum = std::accumulate(latencies.begin(), latencies.end(), 0.0);

    std::cout << "--- M4 Pro Performance Report ---" << std::endl;
    std::cout << "Average: " << sum / iterations << " ns" << std::endl;
    std::cout << "p50    : " << latencies[iterations * 0.50] << " ns" << std::endl;
    std::cout << "p99    : " << latencies[iterations * 0.99] << " ns" << std::endl;
    std::cout << "p99.9  : " << latencies[iterations * 0.999] << " ns" << std::endl;
}

int main() {
    run_latency_test();
    return 0;
}