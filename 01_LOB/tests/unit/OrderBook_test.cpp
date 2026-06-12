//
// Created by Simon Konieczny on 24/05/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include <vector>
#include "../../src/OrderBook.hpp"

class OrderBookTest : public ::testing::Test {
protected:
    SPSCQueue<BookUpdate> bookUpdateQueue_{65536};
    SPSCQueue<ITradeObserver::TradeRecord> tradeQueue_{65536};
    OrderBook book;

    OrderBookTest() : book(bookUpdateQueue_, tradeQueue_) {}

    // Helper to extract all trades from the lock-free queue for easy assertions
    std::vector<ITradeObserver::TradeRecord> drainTrades() {
        std::vector<ITradeObserver::TradeRecord> trades;
        ITradeObserver::TradeRecord trade;
        // Pop until the queue is empty
        while (tradeQueue_.pop(trade)) {
            trades.push_back(trade);
        }
        return trades;
    }
};

TEST_F(OrderBookTest, PriceTimePriorityAndAggressiveSweep) {
    // Setup multiple price levels with multiple orders at the best price
    // Signature: addOrder(id, price, qty, traderId, side, timestamp, stpPolicy)
    book.addOrder(1, 100, 10, 1, Side::Sell, 0, STPBehavior::None);
    book.addOrder(2, 100, 15, 2, Side::Sell, 0, STPBehavior::None);
    book.addOrder(3, 101, 20, 3, Side::Sell, 0, STPBehavior::None);
    book.addOrder(4, 102, 50, 4, Side::Sell, 0, STPBehavior::None);

    // Send aggressive buy to sweep levels 100 and 101, and partially fill 102
    book.addOrder(5, 105, 55, 6, Side::Buy, 0, STPBehavior::None);

    auto trades = drainTrades();
    ASSERT_EQ(trades.size(), 4);

    // Verify Time Priority at Level 100
    EXPECT_EQ(trades[0].mId, 1);
    EXPECT_EQ(trades[0].qty, 10);
    EXPECT_EQ(trades[1].mId, 2);
    EXPECT_EQ(trades[1].qty, 15);

    // Verify Price Priority
    EXPECT_EQ(trades[2].mId, 3);
    EXPECT_EQ(trades[2].qty, 20);
    EXPECT_EQ(trades[3].mId, 4);
    EXPECT_EQ(trades[3].qty, 10); // Partial fill at 102

    // Verify remaining state
    EXPECT_EQ(book.getBestAsk(), 102);
    EXPECT_EQ(book.getOrder(4)->quantity, 40);
}

TEST_F(OrderBookTest, CancelThenMatchIdempotency) {
    book.addOrder(1, 100, 10, 1, Side::Sell, 0, STPBehavior::None);
    book.cancelOrder(1, 1); // Canceled at timestamp 1

    // Attempting to cross the now-canceled order
    book.addOrder(2, 100, 10, 3, Side::Buy, 2, STPBehavior::None);

    auto trades = drainTrades();

    // Should not trade, buy order should rest on the book
    EXPECT_EQ(trades.size(), 0);
    EXPECT_EQ(book.getBestBid(), 100);
    EXPECT_EQ(book.getBestAsk(), 0);

    // Idempotent cancellation (canceling an order that doesn't exist/already canceled)
    EXPECT_NO_THROW(book.cancelOrder(1, 3));
    EXPECT_NO_THROW(book.cancelOrder(999, 4));
}

TEST_F(OrderBookTest, LimitPoolExhaustionRecovery) {
    // Insert 2000 orders at completely unique price levels to force pool growth.
    for (int i = 1; i <= 2000; ++i) {
        book.addOrder(i, 10000 + i, 10, 1, Side::Sell, 0, STPBehavior::None);
    }

    EXPECT_EQ(book.getBestAsk(), 10001); // Lowest ask

    // Sweep the first 1500 levels
    book.addOrder(9999, 20000, 15000, 2, Side::Buy, 0, STPBehavior::None);

    auto trades = drainTrades();

    EXPECT_EQ(trades.size(), 1500);
    EXPECT_EQ(book.getBestAsk(), 11501);
}

TEST_F(OrderBookTest, StpMidQueueExecution) {
    // Build a queue at Price Level 100
    book.addOrder(1, 100, 10, 101, Side::Buy, 0, STPBehavior::CancelOldest); // Firm A
    book.addOrder(2, 100, 10, 102, Side::Buy, 0, STPBehavior::CancelOldest); // Firm B (Target)
    book.addOrder(3, 100, 10, 103, Side::Buy, 0, STPBehavior::CancelOldest); // Firm C

    // Firm B sends an aggressive sweeping order
    book.addOrder(4, 100, 30, 102, Side::Sell, 0, STPBehavior::CancelOldest);

    auto trades = drainTrades();

    // Verify the execution sequence
    ASSERT_EQ(trades.size(), 2);

    // First trade should be Firm B hitting Firm A
    EXPECT_EQ(trades[0].mId, 1);
    EXPECT_EQ(trades[0].tId, 4);
    EXPECT_EQ(trades[0].qty, 10);

    // Second trade should be Firm B hitting Firm C (Order 2 was canceled via STP)
    EXPECT_EQ(trades[1].mId, 3);
    EXPECT_EQ(trades[1].tId, 4);
    EXPECT_EQ(trades[1].qty, 10);

    // Verify book state (Order 4 should be resting with 10 lots remaining)
    EXPECT_EQ(book.getBestAsk(), 100);
    EXPECT_EQ(book.getOrder(4)->quantity, 10);

    // The bid side should be completely empty
    EXPECT_EQ(book.getBestBid(), 0);
}

// ==========================================
// ORDER MODIFICATION TESTS (TIME PRIORITY)
// ==========================================

TEST_F(OrderBookTest, ModifyPriceLosesTimePriority) {
    book.addOrder(1, 100, 10, 101, Side::Buy, 0, STPBehavior::CancelOldest); // Firm A at $100
    book.addOrder(2, 100, 10, 102, Side::Buy, 0, STPBehavior::CancelOldest); // Firm B at $100
    book.addOrder(3, 99,  10, 103, Side::Buy, 0, STPBehavior::CancelOldest); // Firm C at $99

    // Firm A modifies their price down to $99
    // Signature: modifyOrder(id, newPrice, newQty, timestamp)
    book.modifyOrder(1, 99, 10, 0);

    // Send a massive sell order that sweeps the $100 and $99 levels
    book.addOrder(4, 99, 30, 104, Side::Sell, 0, STPBehavior::CancelOldest);

    auto trades = drainTrades();

    ASSERT_EQ(trades.size(), 3);

    // Firm B was alone at $100
    EXPECT_EQ(trades[0].mId, 2);

    // Firm C was already at $99, so they should get filled BEFORE Firm A
    EXPECT_EQ(trades[1].mId, 3);

    // Firm A arrives last because they lost priority during the modification
    EXPECT_EQ(trades[2].mId, 1);
}

TEST_F(OrderBookTest, ModifyIncreaseQuantityLosesTimePriority) {
    book.addOrder(1, 100, 10, 101, Side::Sell, 0, STPBehavior::CancelOldest); // Firm A
    book.addOrder(2, 100, 10, 102, Side::Sell, 0, STPBehavior::CancelOldest); // Firm B

    // Firm A tries to upsize their order to 20 lots
    book.modifyOrder(1, 100, 20, 0);

    // Send a buyer for 15 lots
    book.addOrder(3, 100, 15, 103, Side::Buy, 0, STPBehavior::CancelOldest);

    auto trades = drainTrades();

    ASSERT_EQ(trades.size(), 2);

    // Firm B should trade first (10 lots) because Firm A was sent to the back
    EXPECT_EQ(trades[0].mId, 2);
    EXPECT_EQ(trades[0].qty, 10);

    // Firm A gets the remaining 5 lots
    EXPECT_EQ(trades[1].mId, 1);
    EXPECT_EQ(trades[1].qty, 5);
}

TEST_F(OrderBookTest, ModifyDecreaseQuantityRetainsTimePriority) {
    book.addOrder(1, 100, 50, 101, Side::Buy, 0, STPBehavior::CancelOldest); // Firm A
    book.addOrder(2, 100, 10, 102, Side::Buy, 0, STPBehavior::CancelOldest); // Firm B

    // Firm A scales down their risk to 10 lots
    book.modifyOrder(1, 100, 10, 0);

    // Send a seller for 15 lots
    book.addOrder(3, 100, 15, 103, Side::Sell, 0, STPBehavior::CancelOldest);

    auto trades = drainTrades();

    ASSERT_EQ(trades.size(), 2);

    // Firm A should trade first because decreasing size does NOT lose priority
    EXPECT_EQ(trades[0].mId, 1);
    EXPECT_EQ(trades[0].qty, 10); // Firm A's new total size

    // Firm B gets the remaining 5 lots
    EXPECT_EQ(trades[1].mId, 2);
    EXPECT_EQ(trades[1].qty, 5);
}

TEST_F(OrderBookTest, ModifyDecreaseUpdatesSnapshotVolume) {
    book.addOrder(1, 100, 100, 101, Side::Sell, 0, STPBehavior::CancelOldest);
    book.addOrder(2, 100, 50,  102, Side::Sell, 0, STPBehavior::CancelOldest);

    // Initial check: Level 100 should have 150 volume
    BookSnapshot snap1 = book.getSnapshot(1);
    ASSERT_EQ(snap1.asks.size(), 1);
    EXPECT_EQ(snap1.asks[0].volume, 150);

    // Decrease Firm A's order by 90 lots
    book.modifyOrder(1, 100, 10, 0);

    // Level 100 should now accurately reflect 60 volume (10 + 50)
    BookSnapshot snap2 = book.getSnapshot(1);
    ASSERT_EQ(snap2.asks.size(), 1);
    EXPECT_EQ(snap2.asks[0].volume, 60);
}

TEST_F(OrderBookTest, ModifyIdempotentAndInvalid) {
    // Modifying an order that doesn't exist should safely return without crashing
    EXPECT_NO_THROW(book.modifyOrder(999, 100, 10, 0));

    book.addOrder(1, 100, 10, 101, Side::Buy, 0, STPBehavior::CancelOldest);
    book.addOrder(2, 100, 10, 102, Side::Buy, 0, STPBehavior::CancelOldest);

    // Submitting a modification that changes nothing (same price, same qty)
    book.modifyOrder(1, 100, 10, 0);

    // Send seller for 10 lots
    book.addOrder(3, 100, 10, 103, Side::Sell, 0, STPBehavior::CancelOldest);

    auto trades = drainTrades();

    // Firm A should still get the fill, proving their queue position wasn't reset
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].mId, 1);
}