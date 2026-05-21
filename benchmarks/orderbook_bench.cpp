#include <chrono>
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <string>
#include "../src/OrderBook.hpp"

class NullObserver : public ITradeObserver {
    void onTrade(uint64_t, uint64_t, uint32_t, int64_t) override {}
};

std::vector<double> g_latencies;

static void BM_OrderBookAdd(benchmark::State& state)
{
    NullObserver obs;
    OrderBook book(&obs);

    // Warm up the cache and memory pool
    for (int i = 0; i < 1000; ++i)
    {
        book.addOrder(i, 100, 1, 1, Side::Sell, STPBehavior::CancelBoth);
    }

    uint64_t orderId = 200000;
    uint32_t traderId = 100;

    // pre-allocate to prevent OS-level latency spikes during measurement
    g_latencies.clear();
    g_latencies.reserve(state.max_iterations);

    for (auto _ : state)
    {
        auto start = std::chrono::high_resolution_clock::now();

        // prevent compiler from optimizing orderId + memory ops
        benchmark::DoNotOptimize(orderId);

        book.addOrder(orderId++, 100, 1, traderId++, Side::Buy, STPBehavior::CancelBoth);

        benchmark::ClobberMemory();

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

        g_latencies.push_back(static_cast<double>(elapsed));
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_OrderBookAdd)->Iterations(10000000);

void PrintAsciiHistogram(std::vector<double>& latencies)
{
    if (latencies.empty()) return;

    std::sort(latencies.begin(), latencies.end());

    double p99_9 = latencies[latencies.size() * 0.999];

    std::cout << "\n========================================================\n";
    std::cout << "   M4 Pro Latency Distribution (ASCII Histogram)\n";
    std::cout << "========================================================\n";

    const int num_bins = 10;
    double bin_size = p99_9 / num_bins;
    if (bin_size == 0) bin_size = 1;

    std::vector<int> bins(num_bins +1, 0);
    for (double lat : latencies)
    {
        if (lat <= p99_9)
        {
            int bin_idx = std::min(static_cast<int>(lat / bin_size), num_bins);
            bins[bin_idx]++;
        }
    }

    int max_freq = *std::max_element(bins.begin(), bins.end());
    const int max_bar_length = 40;

    for (int i = 0; i <= num_bins; ++i)
    {
        double bin_start = i * bin_size;
        double bin_end = (i + 1) * bin_size;

        int bar_length = (bins[i] * max_bar_length) / (max_freq > 0 ? max_freq : 1);
        std::string bar(bar_length, '#');

        double percentage = (static_cast<double>(bins[i]) / latencies.size()) * 100.0;

        std::cout << "[" << std::setw(4) << static_cast<int>(bin_start)
                  << " - " << std::setw(4) << static_cast<int>(bin_end) << " ns] | "
                  << std::setw(max_bar_length) << std::left << bar
                  << " (" << std::fixed << std::setprecision(1) << percentage << "%)\n";
    }

    std::cout << "\n--------------------------------------------------------\n";
    std::cout << "Percentiles:\n";
    std::cout << "p50  : " << latencies[latencies.size() * 0.50] << " ns\n";
    std::cout << "p90  : " << latencies[latencies.size() * 0.90] << " ns\n";
    std::cout << "p99  : " << latencies[latencies.size() * 0.99] << " ns\n";
    std::cout << "p99.9: " << p99_9 << " ns\n";
    std::cout << "========================================================\n";

}

int main(int argc, char** argv) {
    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();

    PrintAsciiHistogram(g_latencies);

    return 0;
}