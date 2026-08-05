//
// Created by Mistral Vibe
//
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio> // for std::remove

#include "../../src/TradeExporter.hpp"
#include "../../src/OrderBook.hpp"
#include "../../src/SPSCQueue.hpp"

class TradeExporterTest : public ::testing::Test {
protected:
    SPSCQueue<ITradeObserver::TradeRecord> tradeQueue_{1024};
    std::atomic<bool> producerDone_{false};
    std::string testFile_ = "test_trades_output.csv";

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
};

TEST_F(TradeExporterTest, GeneratesCorrectHeaderWhenFileOpens) {
    // End consumer immediately
    producerDone_ = true;

    TradeExporter exporter(tradeQueue_, producerDone_);
    exporter.runTradeExport(testFile_);

    auto lines = readCSV(testFile_);
    ASSERT_EQ(lines.size(), 1); // Only header expected

    std::string expectedHeader = "maker_id,taker_id,quantity,price,timestamp_ns";
    EXPECT_EQ(lines[0], expectedHeader);
}

TEST_F(TradeExporterTest, WritesTradeDataAccurately) {
    // Push a trade into the queue
    ITradeObserver::TradeRecord trade{1001, 2001, 100, 15050};
    tradeQueue_.push(trade);

    // Signal completion so the loop breaks after popping our trade
    producerDone_ = true;

    TradeExporter exporter(tradeQueue_, producerDone_);
    exporter.runTradeExport(testFile_);

    auto lines = readCSV(testFile_);
    ASSERT_EQ(lines.size(), 2); // Header + 1 Data Row

    // Check that the data row contains the expected values
    // Note: timestamp will vary, so we just check the structure and trade values
    std::string dataLine = lines[1];
    
    // Should have format: maker_id,taker_id,quantity,price,timestamp_ns
    size_t firstComma = dataLine.find(',');
    size_t secondComma = dataLine.find(',', firstComma + 1);
    size_t thirdComma = dataLine.find(',', secondComma + 1);
    size_t fourthComma = dataLine.find(',', thirdComma + 1);
    
    ASSERT_NE(firstComma, std::string::npos);
    ASSERT_NE(secondComma, std::string::npos);
    ASSERT_NE(thirdComma, std::string::npos);
    ASSERT_NE(fourthComma, std::string::npos);
    
    // Extract maker_id
    std::string makerIdStr = dataLine.substr(0, firstComma);
    EXPECT_EQ(makerIdStr, "1001");
    
    // Extract taker_id
    std::string takerIdStr = dataLine.substr(firstComma + 1, secondComma - firstComma - 1);
    EXPECT_EQ(takerIdStr, "2001");
    
    // Extract quantity
    std::string qtyStr = dataLine.substr(secondComma + 1, thirdComma - secondComma - 1);
    EXPECT_EQ(qtyStr, "100");
    
    // Extract price
    std::string priceStr = dataLine.substr(thirdComma + 1, fourthComma - thirdComma - 1);
    EXPECT_EQ(priceStr, "15050");
}

TEST_F(TradeExporterTest, DrainsQueueBeforeExiting) {
    // Push multiple trades
    tradeQueue_.push(ITradeObserver::TradeRecord{1, 1001, 10, 10000});
    tradeQueue_.push(ITradeObserver::TradeRecord{2, 2001, 20, 20000});
    tradeQueue_.push(ITradeObserver::TradeRecord{3, 3001, 30, 30000});

    // Signal completion
    producerDone_ = true;

    TradeExporter exporter(tradeQueue_, producerDone_);
    exporter.runTradeExport(testFile_);

    auto lines = readCSV(testFile_);
    // Header + 3 data rows
    ASSERT_EQ(lines.size(), 4);
}

TEST_F(TradeExporterTest, GracefullyFailsOnInvalidFilepath) {
    producerDone_ = true;
    TradeExporter exporter(tradeQueue_, producerDone_);

    // Attempting to write to a restricted directory path
    exporter.runTradeExport("/invalid_directory_path/trades.csv");

    // Execution reaching here means it returned gracefully instead of throwing/crashing.
    SUCCEED();
}
