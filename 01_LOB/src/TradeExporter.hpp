//
// Created by Mistral Vibe
//

#ifndef LOB_TRADEEXPORTER_HPP
#define LOB_TRADEEXPORTER_HPP

#include <atomic>
#include <fstream>
#include <string>

#include "IBookObserver.hpp"
#include "../plugins/IAnalyticsPlugin.hpp"
#include "OrderBook.hpp"
#include "SPSCQueue.hpp"

#endif //LOB_TRADEEXPORTER_HPP

/**
 * @brief Trade record for CSV export.
 * Matches ITradeObserver::TradeRecord structure.
 */
struct TradeRecord {
    uint64_t mId;      // Maker order ID
    uint64_t tId;      // Taker order ID
    uint32_t qty;      // Quantity
    int64_t price;     // Price
    Side side;         // Side
    uint64_t timestamp_ns; // Timestamp in nanoseconds
    ITradeObserver::TradeType type;    // Trade type (Manual/Replay)
};

/**
 * @brief Trade Exporter Plugin.
 * 
 * Consumes trade records from a queue and exports them to a CSV file.
 * Runs in a separate thread to avoid blocking the main matching thread.
 * 
 * This plugin provides high-throughput trade export with minimal impact
 * on the core matching engine performance.
 */
class TradeExporter : public IAnalyticsPlugin
{
public:
    /**
     * @brief Construct a TradeExporter plugin.
     * 
     * @param tradeQueue Queue containing trade records from the OrderBook.
     * @param producerDone Flag indicating when the producer thread is done.
     */
    explicit TradeExporter(
        SPSCQueue<ITradeObserver::TradeRecord>& tradeQueue,
        std::atomic<bool>& producerDone)
    : tradeQueue_(tradeQueue), 
      producerDone_(producerDone) {}

    // IAnalyticsPlugin interface
    std::string getName() const override { return "Trade Exporter"; }
    void initialize() override {}
    void cleanup() override {}

    /**
     * @brief Book update callback (required by IBookObserver).
     * This plugin doesn't need book updates, but the interface requires it.
     */
    void onBookUpdate(const BookUpdate& update) override {
        // TradeExporter only consumes from the trade queue
        // Book updates are not needed for this plugin
    }

    /**
     * @brief Run the trade export thread.
     * 
     * Reads trade records from the queue and writes them to a CSV file.
     * This method blocks until the producer is done and all trades are processed.
     * 
     * @param filepath Path to the output CSV file.
     */
    void runTradeExport(const std::string& filepath)
    {
        std::ofstream csv_file(filepath, std::ios::out | std::ios::trunc);
        if (!csv_file.is_open())
        {
            std::cerr << "Failed to open " << filepath << " for writing trades." << "\n";
            return;
        }

        // Write CSV header
        csv_file << "maker_id,taker_id,quantity,price,side,timestamp_ns,type\n";

        ITradeObserver::TradeRecord trade;
        uint64_t timestamp_ns = 0;

        while (true)
        {
            if (tradeQueue_.pop(trade))
            {
                writeTrade(csv_file, trade);
            } else
            {
                if (producerDone_.load(std::memory_order_acquire))
                {
                    // Try one last time to catch any remaining trades
                    if (!tradeQueue_.pop(trade)) break;
                    
                    auto now = std::chrono::high_resolution_clock::now();
                    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        now.time_since_epoch()).count();
                    writeTrade(csv_file, trade);
                    break;
                }

                // Spin-wait for efficiency (same pattern as SnapshotWriter)
                #if defined(__aarch64__) || defined(__arm__)
                                __asm__ volatile("yield" ::: "memory");
                #else
                                __asm__ volatile("pause" ::: "memory");
                #endif
            }
        }
        csv_file.close();
    }

private:
    SPSCQueue<ITradeObserver::TradeRecord>& tradeQueue_;
    std::atomic<bool>& producerDone_;

    /**
     * @brief Write a single trade record to the CSV file.
     * 
     * @param file The output file stream.
     * @param trade The trade record to write.
     */
    static inline void writeTrade(
        std::ofstream& file, 
        const ITradeObserver::TradeRecord& trade)
    {
        char sideChar = (trade.side == Side::Buy) ? 'B' : 'S';

        file << trade.mId << ","
             << trade.tId << ","
             << trade.qty << ","
             << trade.price << ","
             << sideChar << ","
             << trade.timestamp_ns << ","
             << static_cast<int>(trade.type) << "\n";
    }
};
