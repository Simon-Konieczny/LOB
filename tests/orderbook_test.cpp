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

// Test 1: Simple limit order addition
TEST_F(OrderBookTest, AddRestingOrder) {
    book.addOrder(1, 100, 10, Side::Buy);

    EXPECT_EQ(book.getBestBid(), 100);
}

// Test 2: Basic Matching logic
TEST_F(OrderBookTest, SimpleMatch) {
    book.addOrder(1, 100, 10, Side::Sell);

    book.addOrder(2, 101, 10, Side::Buy);

    // Both orders should be fully filled and removed from the book.
    EXPECT_EQ(book.getBestBid(), 0);
    EXPECT_EQ(book.getBestAsk(), 0);
}

// Test 3: Partial Fill
TEST_F(OrderBookTest, PartialFill) {
    book.addOrder(1, 100, 100, Side::Sell);

    book.addOrder(2, 100, 40, Side::Buy);

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

// Order Pool Growth
TEST_F(OrderBookTest, OrderPoolGrowthAndPointerStability)
{
    const int initial_orders = 100000;
    const int expansion_orders = 250000;

    for (int i = 1; i <= initial_orders; i++) {
        book.addOrder(i, 100, 10, Side::Sell);
    }

    for (int i = initial_orders + 1; i <= expansion_orders; i++) {
        book.addOrder(i, 105, 10, Side::Sell);
    }

    auto* firstOrder = book.getOrder(1);
    ASSERT_NE(firstOrder, nullptr);
    EXPECT_EQ(firstOrder->id, 1);
    EXPECT_EQ(firstOrder->price, 100);
    EXPECT_EQ(firstOrder->quantity, 10);
}