//
// Created by Simon Konieczny on 24/05/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include <fstream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>

#include "../../src/ITCHParser.hpp"
#include "../../src/ITCH50.hpp"

// Mock Consumer to collect parsed messages
struct MockMessageConsumer {
    std::vector<NormalizedMsg> messages;
    void onMessage(const NormalizedMsg& msg) {
        messages.push_back(msg);
    }
};

// Test Fixture to handle temporary file creation and cleanup
class ITCHParserTest : public ::testing::Test {
protected:
    std::string testFilepath = "test_itch_data.bin";
    std::ofstream outStream;
    MockMessageConsumer consumer;

    void SetUp() override {
        outStream.open(testFilepath, std::ios::binary);
    }

    void TearDown() override {
        outStream.close();
        std::remove(testFilepath.c_str());
    }

    // Helper to write a message payload with its 2-byte length prefix
    template <typename T>
    void writeMessage(const T& msg) {
        uint16_t length = sizeof(T);
        uint16_t networkLength = ITCHParser<MockMessageConsumer>::swap16(length);
        outStream.write(reinterpret_cast<const char*>(&networkLength), sizeof(networkLength));
        outStream.write(reinterpret_cast<const char*>(&msg), sizeof(T));
    }

    // Helper to format 48-bit timestamps
    void setTimestamp(uint8_t* tsArray, uint64_t tsValue) {
        tsArray[0] = (tsValue >> 40) & 0xFF;
        tsArray[1] = (tsValue >> 32) & 0xFF;
        tsArray[2] = (tsValue >> 24) & 0xFF;
        tsArray[3] = (tsValue >> 16) & 0xFF;
        tsArray[4] = (tsValue >> 8) & 0xFF;
        tsArray[5] = tsValue & 0xFF;
    }

    // Helper to format tickers (right-padded with spaces)
    void setTicker(char* dest, const std::string& ticker) {
        std::memset(dest, ' ', 8);
        std::memcpy(dest, ticker.data(), std::min<size_t>(ticker.size(), 8));
    }
};

// --- STATIC METHOD TESTS ---

TEST(ITCHParserStaticTest, EndianSwapping) {
    // Note: Assuming a little-endian test environment (x86/ARM standard)
    EXPECT_EQ(ITCHParser<MockMessageConsumer>::swap16(0x1234), 0x3412);
    EXPECT_EQ(ITCHParser<MockMessageConsumer>::swap32(0x12345678), 0x78563412);
    EXPECT_EQ(ITCHParser<MockMessageConsumer>::swap64(0x1122334455667788), 0x8877665544332211);
}

TEST(ITCHParserStaticTest, Parse48BitTimestamp) {
    uint8_t ts[6] = {0x00, 0x00, 0x01, 0x23, 0x45, 0x67};
    uint64_t expected = 0x01234567;
    EXPECT_EQ(ITCHParser<MockMessageConsumer>::parse48BitTimestamp(ts), expected);
}

// --- PARSER LOGIC TESTS ---

TEST_F(ITCHParserTest, AddOrder_FiltersTargetTicker) {
    // 1. Add order for AAPL
    ITCH5_AddOrder msgAAPL{};
    msgAAPL.msgType = 'A';
    setTicker(msgAAPL.stock, "AAPL");
    msgAAPL.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(100);
    msgAAPL.side = 'B';
    msgAAPL.shares = ITCHParser<MockMessageConsumer>::swap32(500);
    msgAAPL.price = ITCHParser<MockMessageConsumer>::swap32(1500000); // 150.0000
    setTimestamp(msgAAPL.timestamp, 123456789);
    writeMessage(msgAAPL);

    // 2. Add order for MSFT (should be ignored)
    ITCH5_AddOrder msgMSFT{};
    msgMSFT.msgType = 'A';
    setTicker(msgMSFT.stock, "MSFT");
    msgMSFT.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(101);
    msgMSFT.shares = ITCHParser<MockMessageConsumer>::swap32(300);
    writeMessage(msgMSFT);

    outStream.close(); // Flush to disk

    ITCHParser<MockMessageConsumer> parser(consumer);
    parser.parse(testFilepath, "AAPL    ");

    ASSERT_EQ(consumer.messages.size(), 1);

    const auto& normMsg = consumer.messages[0];
    EXPECT_EQ(normMsg.action, MsgAction::Add);
    EXPECT_EQ(normMsg.side, Side::Buy);
    EXPECT_EQ(normMsg.orderId, 100);
    EXPECT_EQ(normMsg.quantity, 500);
    EXPECT_EQ(normMsg.price, 1500000);
    EXPECT_EQ(normMsg.timestamp, 123456789);
}

TEST_F(ITCHParserTest, OrderExecuted_ReducesTrackedOrder) {
    // 1. Add tracking order
    ITCH5_AddOrder addMsg{};
    addMsg.msgType = 'A';
    setTicker(addMsg.stock, "AAPL");
    addMsg.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(200);
    writeMessage(addMsg);

    // 2. Execute tracking order
    ITCH5_OrderExecuted execMsg{};
    execMsg.msgType = 'E';
    execMsg.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(200);
    execMsg.executedShares = ITCHParser<MockMessageConsumer>::swap32(100);
    setTimestamp(execMsg.timestamp, 987654321);
    writeMessage(execMsg);

    outStream.close();

    ITCHParser<MockMessageConsumer> parser(consumer);
    parser.parse(testFilepath, "AAPL    ");

    ASSERT_EQ(consumer.messages.size(), 2);

    // Check execution message
    const auto& execNorm = consumer.messages[1];
    EXPECT_EQ(execNorm.action, MsgAction::Reduce);
    EXPECT_EQ(execNorm.orderId, 200);
    EXPECT_EQ(execNorm.quantity, 100);
    EXPECT_EQ(execNorm.timestamp, 987654321);
}

TEST_F(ITCHParserTest, OrderDelete_CancelsAndStopsTracking) {
    ITCH5_AddOrder addMsg{};
    addMsg.msgType = 'A';
    setTicker(addMsg.stock, "TSLA");
    addMsg.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(300);
    writeMessage(addMsg);

    ITCH5_OrderDelete delMsg{};
    delMsg.msgType = 'D';
    delMsg.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(300);
    writeMessage(delMsg);

    // This execution should be ignored because the order was deleted
    ITCH5_OrderExecuted execMsg{};
    execMsg.msgType = 'E';
    execMsg.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(300);
    writeMessage(execMsg);

    outStream.close();

    ITCHParser<MockMessageConsumer> parser(consumer);
    parser.parse(testFilepath, "TSLA    ");

    ASSERT_EQ(consumer.messages.size(), 2);

    EXPECT_EQ(consumer.messages[0].action, MsgAction::Add);

    const auto& delNorm = consumer.messages[1];
    EXPECT_EQ(delNorm.action, MsgAction::Cancel);
    EXPECT_EQ(delNorm.orderId, 300);
}

TEST_F(ITCHParserTest, OrderReplace_TracksNewId) {
    ITCH5_AddOrder addMsg{};
    addMsg.msgType = 'A';
    setTicker(addMsg.stock, "NVDA");
    addMsg.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(400);
    writeMessage(addMsg);

    ITCH5_OrderReplace repMsg{};
    repMsg.msgType = 'U';
    repMsg.originalOrderRefNum = ITCHParser<MockMessageConsumer>::swap64(400);
    repMsg.newOrderRefNum = ITCHParser<MockMessageConsumer>::swap64(401);
    repMsg.shares = ITCHParser<MockMessageConsumer>::swap32(150);
    repMsg.price = ITCHParser<MockMessageConsumer>::swap32(2000000);
    writeMessage(repMsg);

    // Execute against the NEW ID (should be tracked)
    ITCH5_OrderExecuted execMsg{};
    execMsg.msgType = 'E';
    execMsg.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(401);
    writeMessage(execMsg);

    outStream.close();

    ITCHParser<MockMessageConsumer> parser(consumer);
    parser.parse(testFilepath, "NVDA    ");

    ASSERT_EQ(consumer.messages.size(), 3);

    EXPECT_EQ(consumer.messages[0].action, MsgAction::Add);

    const auto& repNorm = consumer.messages[1];
    EXPECT_EQ(repNorm.action, MsgAction::Replace);
    EXPECT_EQ(repNorm.orderId, 400);
    EXPECT_EQ(repNorm.newOrderId, 401);
    EXPECT_EQ(repNorm.quantity, 150);
    EXPECT_EQ(repNorm.price, 2000000);

    const auto& execNorm = consumer.messages[2];
    EXPECT_EQ(execNorm.action, MsgAction::Reduce);
    EXPECT_EQ(execNorm.orderId, 401);
}

TEST_F(ITCHParserTest, IgnoresUntrackedOrders) {
    // Send Cancel, Delete, and Executed for an ID that was never added
    ITCH5_CancelOrder cnlMsg{};
    cnlMsg.msgType = 'X';
    cnlMsg.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(999);
    writeMessage(cnlMsg);

    ITCH5_OrderDelete delMsg{};
    delMsg.msgType = 'D';
    delMsg.orderRefNum = ITCHParser<MockMessageConsumer>::swap64(999);
    writeMessage(delMsg);

    ITCH5_OrderReplace repMsg{};
    repMsg.msgType = 'U';
    repMsg.originalOrderRefNum = ITCHParser<MockMessageConsumer>::swap64(999);
    repMsg.newOrderRefNum = ITCHParser<MockMessageConsumer>::swap64(1000);
    writeMessage(repMsg);

    outStream.close();

    ITCHParser<MockMessageConsumer> parser(consumer);
    parser.parse(testFilepath, "AAPL    ");

    // All should be ignored because '999' is not in activeTargetOrders
    EXPECT_EQ(consumer.messages.size(), 0);
}