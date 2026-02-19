#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include "../src/OrderBook.hpp"

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook book;
};

// Test 1: Simple limit order addition (resting in book)
TEST_F(OrderBookTest, AddRestingOrder) {
    book.addOrder(1, 100, 10, Side::Buy);

    EXPECT_EQ(book.getBestBid(), 100);
}

// Test 2: Basic Matching logic (The "Crossing" Test)
TEST_F(OrderBookTest, SimpleMatch) {
    // 1. Add a resting Sell order (Maker) at $100
    book.addOrder(1, 100, 10, Side::Sell);

    // 2. Add an incoming Buy order (Taker) at $101
    // This should match instantly because 101 >= 100
    book.addOrder(2, 101, 10, Side::Buy);

    // 3. Verification:
    // Both orders should be fully filled and removed from the book.
    // Therefore, Best Bid and Best Ask should be 0 (or your "empty" value)
    EXPECT_EQ(book.getBestBid(), 0);
    EXPECT_EQ(book.getBestAsk(), 0);
}

// Test 3: Partial Fill
TEST_F(OrderBookTest, PartialFill) {
    // Sell 100 units at $100
    book.addOrder(1, 100, 100, Side::Sell);

    // Buy only 40 units at $100
    book.addOrder(2, 100, 40, Side::Buy);

    // After match, there should still be 60 units left on the Sell side
    // We can add a helper method to OrderBook to get volume at a price to check this
    // For now, we check that Best Ask is still $100
    EXPECT_EQ(book.getBestAsk(), 100);
}

class MockObserver : public ITradeObserver {
public:
    uint32_t tradeCount = 0;
    void onTrade(uint64_t, uint64_t, uint32_t, int64_t) override {
        tradeCount++;
    }
};

TEST_F(OrderBookTest, ObserverTest) {
    MockObserver myObserver;
    OrderBook testBook(&myObserver); // Inject the mock

    testBook.addOrder(1, 100, 10, Side::Sell);
    testBook.addOrder(2, 100, 10, Side::Buy);

    EXPECT_EQ(myObserver.tradeCount, 1);
}