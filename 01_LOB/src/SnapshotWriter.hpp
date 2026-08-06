//
// Created by Simon Konieczny on 12/06/2026.
//

#ifndef LOB_SNAPSHOTWRITER_HPP
#define LOB_SNAPSHOTWRITER_HPP
#include <atomic>
#include <fstream>
#include <string>

#include "IBookObserver.hpp"
#include "../plugins/IAnalyticsPlugin.hpp"
#include "OFICalculator.hpp"
#include "OrderBook.hpp"
#include "SPSCQueue.hpp"

#endif //LOB_SNAPSHOTWRITER_HPP

/**
 * @brief Snapshot data row structure for order book state.
 */
struct SnapshotRow {
    uint64_t timestamp_ns;
    std::array<int64_t, 5> bids, asks;
    std::array<uint32_t, 5> bid_vols, ask_vols;
    int64_t ofi_1s, ofi_5s, ofi_30s;
    int64_t mid_price;
    int64_t micro_price;
};

/**
 * @brief Snapshot Writer Plugin.
 * 
 * Observes order book updates and writes periodic snapshots to a CSV file.
 * Captures top-N levels from both sides of the book along with OFI signals.
 */
class SnapshotWriter : public IAnalyticsPlugin
{
public:
    /**
     * @brief Construct a SnapshotWriter plugin.
     * 
     * @param snapshotQueue Queue for receiving snapshot data.
     * @param producerDone Flag indicating when producer thread is done.
     * @param book The OrderBook to observe.
     * @param ofi The OFICalculator for OFI signal data.
     * @param interval_ns Minimum interval between snapshots in nanoseconds.
     */
    explicit SnapshotWriter(SPSCQueue<SnapshotRow>& snapshotQueue,
        std::atomic<bool>& producerDone,
        const OrderBook& book,
        const OFICalculator& ofi,
        uint64_t interval_ns)
    : snapshotQueue_(snapshotQueue), 
      producerDone_(producerDone), 
      book_(book), 
      ofi_(ofi), 
      interval_ns_(interval_ns), 
      last_ts_(0) {}

    // IAnalyticsPlugin interface
    [[nodiscard]] std::string getName() const override { return "Snapshot Writer"; }
    void initialize() override {}
    void cleanup() override {}

    void onBookUpdate(const BookUpdate& update) override
    {
        if (!enabled_) return;
        
        if (book_.getBestBid() > 0 && book_.getBestAsk() > 0) {
            if (update.timestamp - last_ts_ >= interval_ns_) {
                SnapshotRow row{};
                row.timestamp_ns = update.timestamp;

                // Extract state instantly
                book_.getTopN(row.bids, row.asks, row.bid_vols, row.ask_vols);
                row.ofi_1s = ofi_.getOFI_1s();
                row.ofi_5s = ofi_.getOFI_5s();
                row.ofi_30s = ofi_.getOFI_30s();

                row.mid_price = (row.bids[0] + row.asks[0]) / 2;
                row.micro_price = (row.bids[0] * row.bid_vols[0] + row.asks[0] * row.ask_vols[0])
                / (row.bid_vols[0] + row.ask_vols[0]);

                snapshotQueue_.push(row);
                last_ts_ = update.timestamp;
            }
        }
    }

    /**
     * @brief Run the snapshot capture thread.
     * 
     * Reads from the snapshot queue and writes to CSV file.
     * 
     * @param filepath Path to the output CSV file.
     */
    void runSnapshotCapture(const std::string& filepath)
    {
        std::ofstream csv_file(filepath, std::ios::out | std::ios::trunc);
        if (!csv_file.is_open())
        {
            std::cerr << "Failed to open " << filepath << " for writing LOB snapshots." << "\n";
            return;
        }

        csv_file << "timestamp_ns,";
        for (int i = 0; i < 5; ++i) csv_file << "bid_" << i << ",bid_vol_" << i << ",";
        for (int i = 0; i < 5; ++i) csv_file << "ask_" << i << ",ask_vol_" << i << ",";
        csv_file << "ofi_1s,ofi_5s,ofi_30s,mid_price,micro_price\n";

        SnapshotRow row = {};

        while (true)
        {
            if (snapshotQueue_.pop(row))
            {
                writeRow(csv_file, row);
            } else
            {
                if (producerDone_.load(std::memory_order_acquire))
                {
                    if (!snapshotQueue_.pop(row)) break;
                }
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
    SPSCQueue<SnapshotRow>& snapshotQueue_;
    std::atomic<bool>& producerDone_;
    const OrderBook& book_;
    const OFICalculator& ofi_;
    uint64_t interval_ns_;
    uint64_t last_ts_;

    static inline void writeRow(std::ofstream& file, const SnapshotRow& row)
    {
        file << row.timestamp_ns << ",";

        for (size_t i = 0; i < 5; ++i) {
            file << row.bids[i] << "," << row.bid_vols[i] << ",";
        }
        for (size_t i = 0; i < 5; ++i) {
            file << row.asks[i] << "," << row.ask_vols[i] << ",";
        }

        file << row.ofi_1s << "," << row.ofi_5s << "," << row.ofi_30s << "," << row.mid_price << "," << row.micro_price << "\n";
    }
};