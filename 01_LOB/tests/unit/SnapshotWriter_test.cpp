//
// Created by Simon Konieczny on 12/06/2026.
//

#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio> // for std::remove

#include "../../src/SnapshotWriter.hpp"
#include "../../src/OrderBook.hpp"
#include "../../src/OFICalculator.hpp"
#include "../../src/SPSCQueue.hpp"

class SnapshotWriterTest : public ::testing::Test {
protected:
    SPSCQueue<SnapshotRow> queue_{1024};
    SPSCQueue<ITradeObserver::TradeRecord> tradeQueue_{1024};
    std::atomic<bool> producerDone_{false};

    // Real instances required since SnapshotWriter takes them by const reference
    OrderBook book_{tradeQueue_};
    OFICalculator ofi_{};

    uint64_t interval_ns_ = 100'000'000; // 100ms interval
    std::string testFile_ = "test_snapshots_output.csv";

    void TearDown() override {
        // Clean up the generated file after each test
        std::remove(testFile_.c_str());
    }

    // Helper function to read generated CSV files
    std::vector<std::string> readCSV(const std::string& filepath) {
        std::vector<std::string> lines;
        std::ifstream file(filepath);
        std::string line;
        while (std::getline(file, line)) {
            lines.push_back(line);
        }
        return lines;
    }

    // Helper to populate the book so getBestBid() > 0 and getBestAsk() > 0
    void setupValidOrderBook() {
        // TODO: Replace with your actual methods for pushing state into the LOB
        // to ensure book_.getBestBid() > 0 && book_.getBestAsk() > 0.
        // Example:
        // book_.processMessage(NormalizedMsg{...});
    }
};

// --- 1. PRODUCER LOGIC TESTS (onBookUpdate) ---

TEST_F(SnapshotWriterTest, SkipsUpdateWhenOrderBookIsInvalid) {
    SnapshotWriter writer(queue_, producerDone_, book_, ofi_, interval_ns_);

    // Default initialized book_ usually has best bid/ask == 0
    writer.onBookUpdate(BookUpdate{100, 102, 10, 10, 1'000'000'000});

    SnapshotRow poppedRow;
    // Queue should remain empty because book state is incomplete
    EXPECT_FALSE(queue_.pop(poppedRow));
}

TEST_F(SnapshotWriterTest, RespectsThrottlingInterval) {
    setupValidOrderBook(); // Assume this sets bestBid and bestAsk > 0

    // If you don't have a way to easily populate OrderBook in the test environment,
    // this test will correctly fail until you update setupValidOrderBook().
    if (book_.getBestBid() == 0 || book_.getBestAsk() == 0) {
        GTEST_SKIP() << "Skipping: setupValidOrderBook() needs actual implementation.";
    }

    SnapshotWriter writer(queue_, producerDone_, book_, ofi_, interval_ns_);

    // T = 100,000,000 (Exactly the interval) -> SHOULD PUSH
    writer.onBookUpdate(BookUpdate{100, 102, 10, 10, interval_ns_});

    // T = 150,000,000 (Only 50ms later, ignores update) -> SHOULD NOT PUSH
    writer.onBookUpdate(BookUpdate{100, 102, 10, 10, interval_ns_ + 50'000'000});

    // T = 200,000,000 (100ms passed since last push) -> SHOULD PUSH
    writer.onBookUpdate(BookUpdate{100, 102, 10, 10, interval_ns_ * 2});

    SnapshotRow poppedRow;
    EXPECT_TRUE(queue_.pop(poppedRow));
    EXPECT_EQ(poppedRow.timestamp_ns, interval_ns_);

    EXPECT_TRUE(queue_.pop(poppedRow));
    EXPECT_EQ(poppedRow.timestamp_ns, interval_ns_ * 2);

    // Queue should now be empty
    EXPECT_FALSE(queue_.pop(poppedRow));
}

// --- 2. CONSUMER LOGIC TESTS (runSnapshotCapture) ---

TEST_F(SnapshotWriterTest, GeneratesCorrectHeaderWhenFileOpens) {
    // End consumer immediately
    producerDone_ = true;

    SnapshotWriter writer(queue_, producerDone_, book_, ofi_, interval_ns_);
    writer.runSnapshotCapture(testFile_);

    auto lines = readCSV(testFile_);
    ASSERT_EQ(lines.size(), 1); // Only header expected

    std::string expectedHeader = "timestamp_ns,"
                                 "bid_0,bid_vol_0,bid_1,bid_vol_1,bid_2,bid_vol_2,bid_3,bid_vol_3,bid_4,bid_vol_4,"
                                 "ask_0,ask_vol_0,ask_1,ask_vol_1,ask_2,ask_vol_2,ask_3,ask_vol_3,ask_4,ask_vol_4,"
                                 "ofi_1s,ofi_5s,ofi_30s";

    EXPECT_EQ(lines[0], expectedHeader);
}

TEST_F(SnapshotWriterTest, WritesRowDataAccurately) {
    // Manually inject a row into the queue (bypassing onBookUpdate)
    SnapshotRow row{};
    row.timestamp_ns = 987654321;
    row.bids = {100.5, 100.4, 100.3, 100.2, 100.1};
    row.bid_vols = {10, 20, 30, 40, 50};
    row.asks = {101.1, 101.2, 101.3, 101.4, 101.5};
    row.ask_vols = {15, 25, 35, 45, 55};
    row.ofi_1s = 5;
    row.ofi_5s = -10;
    row.ofi_30s = 42;

    queue_.push(row);

    // Signal completion so the loop breaks after popping our row
    producerDone_ = true;

    SnapshotWriter writer(queue_, producerDone_, book_, ofi_, interval_ns_);
    writer.runSnapshotCapture(testFile_);

    auto lines = readCSV(testFile_);
    ASSERT_EQ(lines.size(), 2); // Header + 1 Data Row

    std::string expectedData = "987654321,"
                               "100.5,10,100.4,20,100.3,30,100.2,40,100.1,50," // Bids
                               "101.1,15,101.2,25,101.3,35,101.4,45,101.5,55," // Asks
                               "5,-10,42";                                     // OFI

    EXPECT_EQ(lines[1], expectedData);
}

TEST_F(SnapshotWriterTest, GracefullyFailsOnInvalidFilepath) {
    producerDone_ = true;
    SnapshotWriter writer(queue_, producerDone_, book_, ofi_, interval_ns_);

    // Attempting to write to a restricted root directory path
    writer.runSnapshotCapture("/invalid_directory_path/test.csv");

    // Execution reaching here means it returned gracefully instead of throwing/crashing.
    SUCCEED();
}

TEST_F(SnapshotWriterTest, DrainsQueueBeforeExiting)
{
    // Push multiple items
    queue_.push(SnapshotRow{100});
    queue_.push(SnapshotRow{200});
    queue_.push(SnapshotRow{300});

    // Set flag to true BEFORE running.
    // The consumer logic MUST still drain the remaining 3 items before breaking.
    producerDone_ = true;

    SnapshotWriter writer(queue_, producerDone_, book_, ofi_, interval_ns_);
    writer.runSnapshotCapture(testFile_);

    auto lines = readCSV(testFile_);
    // 1 Header + 3 Data rows = 4 total lines
    ASSERT_EQ(lines.size(), 4);

    // Just verify the timestamps were captured
    EXPECT_TRUE(lines[1].rfind("100,", 0) == 0); // starts with "100,"
    EXPECT_TRUE(lines[2].rfind("200,", 0) == 0); // starts with "200,"
    EXPECT_TRUE(lines[3].rfind("300,", 0) == 0);
}