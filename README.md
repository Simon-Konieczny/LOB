# High-Performance Limit Order Book (C++)
An end-to-end implementation of market microstructure mechanics, ranging from zero-allocation binary parsing of NASDAQ ITCH 5.0 market data to deterministic order matching and quantitative alpha research.

See `analytics.md` for quantitative finance research.

## Executive Summary
* **Domain Focus:** Quantitative Finance and High-Frequency Trading (HFT) Infrastructure
* **Core Features:** Deterministic, ultra-low-latency Matching Engine designed for HFT applications along with comprehensive research suite.
* **Tech Stack:** Modern C++20, Python, CMake, Docker, Google Test, Google Benchmark, and RapidCheck
* **Performance Peak:** Throughput of >26.4M ops with average latency of ~36.8ns per operation on Apple M4 Pro architecture

Snapshot of the book:
![demo_image](demo.png)

## 🚀 Performance Metrics (M4 Pro)
- Average Latency: `~36.8ns`
- p50: `0ns`
- p99: `1us`
- p99.9 Tail Latency: `1us`
- Throughput: >26.4M orders/second

See `benchmark_results` for more details


## 🛠 Low-Latency Architecture & Optimizations
* **Zero Dynamic Allocation (Object Pooling):** The system completely eliminates OS-level malloc and free calls during the live matching cycle. It relies instead on a pre-allocated OrderPool and LimitPool managed via a Free List to prevent unexpected latency spikes.
* **O(1) Order Operations:** Order tracking utilizes a std::unordered_map for lookup combined with a Custom Doubly Linked List embedded directly within the Order struct itself. This custom data layout enables $O(1)$ algorithmic complexity for order cancellations without the need to iterate through individual price levels.
* **Price Level Tracking:** The limit order book utilizes a Map of Pointers to Linked Lists structural approach, employing std::map<Price, LimitLevel*> for sorted price levels. Internal linked lists then strictly enforce deterministic Time Priority (FIFO) matching.
* **Deterministic Fixed-Point Math:** To safely eliminate floating-point non-determinism and inherent rounding errors, all internal price operations are processed as int64_t fixed-point integers scaled by $10^4$.
* **Zero-Copy Ingestion:** Employs a custom MmapReader memory-mapped file abstraction to map and parse raw binary ITCH 5.0 messages directly from disk without copying into user space.
* **Lock-Free Concurrency Concepts:** Implements Single-Producer Single-Consumer (SPSCQueue) ring buffers to gracefully decouple the critical deterministic matching thread from asynchronous background tasks.

## Market Data Integration & Quantitative Analytics
Using the described infrasturcture, this repository bridges software engineering with quantitative financial research.
* **ITCH 5.0 Protocol Decoding:** Fully supports tracking complex state reductions via strict NASDAQ ITCH 5.0 protocols. The engine correctly parses and reacts to standard messages including AddOrder, CancelOrder, OrderDelete, and OrderExecutedWithPrice.
* **Live Order Flow Imbalance (OFI):** Features a real-time OFICalculator that utilizes a synchronous Observer pattern (IBookObserver) to dynamically monitor book updates. It efficiently manages state pruning of trades across concurrent rolling 1-second, 5-second, and 30-second time windows.
* **OFI Signal Replication (Cont et al. 2014):** Contains a dedicated Python analytics pipeline (cont.py) that successfully replicates Cont's renowned academic paper on OFI as a mid-price predictor. By processing historical 2019 AAPL ITCH data, the pipeline achieved an $R^2$ value of $0.605$ on a 1-second horizon, deeply matching the original paper's findings for equities.

## Engineering Standards
* **Comprehensive Unit Testing:** Employs Google Test (gtest) to meticulously cover edge-case business logic, including complex modifications where a trader increasing their order quantity or changing their limit price properly loses time priority in the queue. Test cases carefully enforce separation of concerns by manually injecting dummy data structures into queues to test asynchronous writing without spinning up complex order book dependency states.
* **Property-Based Fuzzing:** RapidCheck is deeply integrated to continuously generate randomized order flow payloads, rigorously verifying critical system invariants such as validating that "No Order Matches Itself".
* **Continuous Integration:** Strict build health is automatically enforced on pushes via .github/workflows/ci.yml.
* **Code Coverage Verification:** Uses bash shell scripts (run_coverage.sh) coupled with gcovr to output detailed HTML reports and enforce high testing coverage standards directly over the core matching engine logic.

## 🔌 Plugin Architecture

The LOB system features a **modular plugin architecture** that allows you to extend functionality without modifying the core matching engine. This design enables rapid development of new analytics, data exporters, and processing modules.

### Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                    OrderBook (Core)                          │
│              (Deterministic Matching Engine)               │
└─────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────┐
│                   PluginManager                              │
│              (Lifecycle & Registration)                      │
└─────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
┌─────────────────┐   ┌─────────────────┐   ┌─────────────────┐
│  OFICalculator   │   │ SnapshotWriter   │   │  Your Plugin    │
│  (Analytics)     │   │  (Data Export)   │   │  (Custom)       │
└─────────────────┘   └─────────────────┘   └─────────────────┘
```

### Key Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `IAnalyticsPlugin` | `01_LOB/plugins/IAnalyticsPlugin.hpp` | Base interface for all plugins |
| `PluginManager` | `01_LOB/plugins/PluginManager.hpp` | Manages plugin lifecycle and registration |

### Creating a New Plugin

To create a new plugin, inherit from `IAnalyticsPlugin` and implement the required methods:

```cpp
// plugins/analytics/YourPlugin.hpp
#pragma once
#include "IAnalyticsPlugin.hpp"
#include <string>

class YourPlugin : public IAnalyticsPlugin {
public:
    // Required interface methods
    std::string getName() const override { 
        return "Your Plugin Name"; 
    }
    
    void initialize() override { 
        // Setup initialization logic
    }
    
    void cleanup() override { 
        // Cleanup resources
    }
    
    // Book update callback (from IBookObserver)
    void onBookUpdate(const BookUpdate& update) override {
        // Your logic here - called on every book change
    }
    
    // Additional custom methods
    void yourCustomMethod() { 
        // Your functionality
    }
};
```

### Registering Plugins

In your main application:

```cpp
#include "plugins/PluginManager.hpp"
#include "plugins/analytics/YourPlugin.hpp"

int main() {
    OrderBook book(tradeQueue);
    PluginManager plugins(book, true); // verbose = true
    
    // Add built-in plugins
    OFICalculator& ofi = plugins.addPlugin<OFICalculator>();
    SnapshotWriter& writer = plugins.addPlugin<SnapshotWriter>(queue, flag, book, ofi, interval);
    
    // Add your custom plugin
    YourPlugin& custom = plugins.addPlugin<YourPlugin>(arg1, arg2);
    
    // Plugins are automatically registered as OrderBook observers
    // No need to manually call book.addObserver()
    
    return 0;
}
```

### Plugin Lifecycle

1. **Construction**: Plugin is created via `addPlugin<T>()`
2. **Registration**: Automatically registered as an OrderBook observer
3. **Initialization**: `initialize()` is called immediately after construction
4. **Runtime**: `onBookUpdate()` is called for each book change
5. **Cleanup**: `cleanup()` is called during PluginManager destruction

### Built-in Plugins

| Plugin | Purpose | Configuration |
|--------|---------|---------------|
| `OFICalculator` | Real-time OFI signal calculation | None required |
| `SnapshotWriter` | Periodic book snapshots to CSV | Output path, interval |

### Runtime Plugin Management

You can enable/disable plugins at runtime:

```cpp
// Disable a plugin
plugins.setPluginEnabled("OFI Calculator", false);

// Enable a plugin
plugins.setPluginEnabled("OFI Calculator", true);

// Get a plugin by name
IAnalyticsPlugin* plugin = plugins.getPlugin("OFI Calculator");

// Get a plugin by type
OFICalculator* ofi = plugins.getPlugin<OFICalculator>();

// List all plugins
auto names = plugins.getPluginNames();
```

### Plugin Best Practices

1. **Keep `onBookUpdate()` fast**: This is called on every book change
2. **Use `isEnabled()` flag**: Check if plugin is enabled before expensive operations
3. **Minimize allocations**: Use object pooling for frequent allocations
4. **Thread safety**: Plugin callbacks are called from the main matching thread
5. **Error handling**: Don't throw exceptions from callbacks

### Directory Structure

```
01_LOB/
├── plugins/
│   ├── IAnalyticsPlugin.hpp      # Base plugin interface
│   ├── PluginManager.hpp        # Plugin management system
│   └── CMakeLists.txt            # Plugin build configuration
├── src/
│   ├── OFICalculator.hpp/cpp    # Now inherits from IAnalyticsPlugin
│   ├── SnapshotWriter.hpp/cpp   # Now inherits from IAnalyticsPlugin
│   └── ...
└── main.cpp                     # Uses PluginManager
```

## 🛠 Low-Latency Architecture & Optimizations
* **Zero Dynamic Allocation (Object Pooling):** The system completely eliminates OS-level malloc and free calls during the live matching cycle. It relies instead on a pre-allocated OrderPool and LimitPool managed via a Free List to prevent unexpected latency spikes.
* **O(1) Order Operations:** Order tracking utilizes a std::unordered_map for lookup combined with a Custom Doubly Linked List embedded directly within the Order struct itself. This custom data layout enables $O(1)$ algorithmic complexity for order cancellations without the need to iterate through individual price levels.
* **Price Level Tracking:** The limit order book utilizes a Map of Pointers to Linked Lists structural approach, employing std::map<Price, LimitLevel*> for sorted price levels. Internal linked lists then strictly enforce deterministic Time Priority (FIFO) matching.
* **Deterministic Fixed-Point Math:** To safely eliminate floating-point non-determinism and inherent rounding errors, all internal price operations are processed as int64_t fixed-point integers scaled by $10^4$.
* **Zero-Copy Ingestion:** Employs a custom MmapReader memory-mapped file abstraction to map and parse raw binary ITCH 5.0 messages directly from disk without copying into user space.
* **Lock-Free Concurrency Concepts:** Implements Single-Producer Single-Consumer (SPSCQueue) ring buffers to gracefully decouple the critical deterministic matching thread from asynchronous background tasks.

## Market Data Integration & Quantitative Analytics
Using the described infrasturcture, this repository bridges software engineering with quantitative financial research.
* **ITCH 5.0 Protocol Decoding:** Fully supports tracking complex state reductions via strict NASDAQ ITCH 5.0 protocols. The engine correctly parses and reacts to standard messages including AddOrder, CancelOrder, OrderDelete, and OrderExecutedWithPrice.
* **Live Order Flow Imbalance (OFI):** Features a real-time OFICalculator that utilizes a synchronous Observer pattern (IBookObserver) to dynamically monitor book updates. It efficiently manages state pruning of trades across concurrent rolling 1-second, 5-second, and 30-second time windows.
* **OFI Signal Replication (Cont et al. 2014):** Contains a dedicated Python analytics pipeline (cont.py) that successfully replicates Cont's renowned academic paper on OFI as a mid-price predictor. By processing historical 2019 AAPL ITCH data, the pipeline achieved an $R^2$ value of $0.605$ on a 1-second horizon, deeply matching the original paper's findings for equities.

## Engineering Standards
* **Comprehensive Unit Testing:** Employs Google Test (gtest) to meticulously cover edge-case business logic, including complex modifications where a trader increasing their order quantity or changing their limit price properly loses time priority in the queue. Test cases carefully enforce separation of concerns by manually injecting dummy data structures into queues to test asynchronous writing without spinning up complex order book dependency states.
* **Property-Based Fuzzing:** RapidCheck is deeply integrated to continuously generate randomized order flow payloads, rigorously verifying critical system invariants such as validating that "No Order Matches Itself".
* **Continuous Integration:** Strict build health is automatically enforced on pushes via .github/workflows/ci.yml.
* **Code Coverage Verification:** Uses bash shell scripts (run_coverage.sh) coupled with gcovr to output detailed HTML reports and enforce high testing coverage standards directly over the core matching engine logic.

## 🔧 Getting Started

### Prerequisites
- C++20 compatible compiler (Clang 14+, GCC 11+, MSVC 2022)
- CMake 3.28+
- Ninja build system (recommended)

### Build Instructions

```bash
# Clone the repository
cd LOB/01_LOB

# Configure and build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run the engine
./build/lob_main
```

### Running Tests

```bash
# Build and run tests
cmake --build build --target all
ctest --test-dir build --output-on-failure
```

### Running Benchmarks

```bash
./build/benchmarks/latency_bench
```

### Code Coverage

```bash
./run_coverage.sh
```

### Plugin Development

To create a new plugin:

1. Create a header file in `01_LOB/plugins/analytics/` or `01_LOB/plugins/exporters/`
2. Inherit from `IAnalyticsPlugin`
3. Implement the required interface methods
4. Add any custom functionality
5. Register the plugin in your main application using `PluginManager::addPlugin<T>()`

Example plugin template:

```cpp
// plugins/analytics/MyAnalytics.hpp
#pragma once
#include "IAnalyticsPlugin.hpp"

class MyAnalytics : public IAnalyticsPlugin {
public:
    std::string getName() const override { return "My Analytics"; }
    
    void onBookUpdate(const BookUpdate& update) override {
        // Your analytics logic here
        if (!isEnabled()) return;
        
        // Process the book update
        // Calculate your signals
        // Store results
    }
    
    // Custom methods specific to your plugin
    double getMySignal() const { return mySignal_; }
    
private:
    double mySignal_ = 0.0;
};
```

For more details, see the [Plugin Architecture](#-plugin-architecture) section above.

## 📚 Project Structure

```
LOB/
├── 01_LOB/                          # Core C++ implementation
│   ├── src/                        # Source files
│   │   ├── OrderBook.hpp/cpp        # Matching engine
│   │   ├── OFICalculator.hpp/cpp    # OFI calculation
│   │   ├── SnapshotWriter.hpp      # CSV exporter
│   │   ├── ReplayEngine.hpp        # Data replay
│   │   ├── ITCHParser.hpp          # ITCH 5.0 parser
│   │   └── ...
│   ├── plugins/                    # Plugin system
│   │   ├── IAnalyticsPlugin.hpp    # Plugin interface
│   │   ├── PluginManager.hpp      # Plugin manager
│   │   └── CMakeLists.txt
│   ├── benchmarks/                # Performance benchmarks
│   ├── tests/                     # Unit & property tests
│   ├── CMakeLists.txt
│   └── Dockerfile
├── 02_analysis/                    # Python analytics
│   ├── cont.py                    # OFI replication
│   └── ofi_regression_plot.png    # Results visualization
├── 03_data/                        # Market data
│   └── 12302019.NASDAQ_ITCH50     # Sample ITCH data
├── README.md                       # This file
├── analytics.md                   # Research results
└── benchmark_results.md            # Performance metrics
```

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/your-feature`)
3. Add your plugin or feature
4. Update documentation
5. Submit a Pull Request

### Adding New Plugins

When adding a new plugin:
- Place header files in the appropriate `plugins/` subdirectory
- Inherit from `IAnalyticsPlugin`
- Add comprehensive unit tests
- Update this README with plugin documentation
- Ensure the plugin respects the performance constraints

## 📄 License

This project is provided for educational and research purposes.

---

*Built with C++20 for maximum performance and reliability.*