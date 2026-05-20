#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include <vector>
#include "../src/OrderBook.hpp"

// Observer to record the exact sequence of trades for validation
struct TradeRecord {
    uint64_t makerId;
    uint64_t takerId;
    uint32_t qty;
    int64_t price;
};

class SequenceObserver : public ITradeObserver {
public:
    std::vector<TradeRecord> trades;
    void onTrade(uint64_t makerId, uint64_t takerId, uint32_t qty, int64_t price) override {
        trades.push_back({makerId, takerId, qty, price});
    }
};

class OrderBookTest : public ::testing::Test {
protected:
    SequenceObserver obs;
    OrderBook book;

    OrderBookTest() : book(&obs) {}
};

TEST_F(OrderBookTest, PriceTimePriorityAndAggressiveSweep) {
    // 1. Setup multiple price levels with multiple orders at the best price
    book.addOrder(1, 100, 10, Side::Sell);
    book.addOrder(2, 100, 15, Side::Sell);
    book.addOrder(3, 101, 20, Side::Sell);
    book.addOrder(4, 102, 50, Side::Sell);

    // 2. Send aggressive buy to sweep levels 100 and 101, and partially fill 102
    book.addOrder(5, 105, 55, Side::Buy);

    ASSERT_EQ(obs.trades.size(), 4);

    // Verify Time Priority at Level 100
    EXPECT_EQ(obs.trades[0].makerId, 1);
    EXPECT_EQ(obs.trades[0].qty, 10);
    EXPECT_EQ(obs.trades[1].makerId, 2);
    EXPECT_EQ(obs.trades[1].qty, 15);

    // Verify Price Priority
    EXPECT_EQ(obs.trades[2].makerId, 3);
    EXPECT_EQ(obs.trades[2].qty, 20);
    EXPECT_EQ(obs.trades[3].makerId, 4);
    EXPECT_EQ(obs.trades[3].qty, 10); // Partial fill at 102

    // Verify remaining state
    EXPECT_EQ(book.getBestAsk(), 102);
    EXPECT_EQ(book.getOrder(4)->quantity, 40);
}

TEST_F(OrderBookTest, CancelThenMatchIdempotency) {
    book.addOrder(1, 100, 10, Side::Sell);
    book.cancelOrder(1);

    // Attempting to cross the now-canceled order
    book.addOrder(2, 100, 10, Side::Buy);

    // Should not trade, buy order should rest on the book
    EXPECT_EQ(obs.trades.size(), 0);
    EXPECT_EQ(book.getBestBid(), 100);
    EXPECT_EQ(book.getBestAsk(), 0);

    // Idempotent cancellation (canceling an order that doesn't exist/already canceled)
    EXPECT_NO_THROW(book.cancelOrder(1));
    EXPECT_NO_THROW(book.cancelOrder(999));
}

TEST_F(OrderBookTest, LimitPoolExhaustionRecovery) {
    // LimitPool is initialized with 1000 capacity.
    // Insert 2000 orders at completely unique price levels to force pool growth.
    for (int i = 1; i <= 2000; ++i) {
        book.addOrder(i, 10000 + i, 10, Side::Sell);
    }

    EXPECT_EQ(book.getBestAsk(), 10001); // Lowest ask

    // Sweep the first 1500 levels to ensure the linked lists and limits are intact
    book.addOrder(9999, 20000, 15000, Side::Buy);

    EXPECT_EQ(obs.trades.size(), 1500);
    EXPECT_EQ(book.getBestAsk(), 11501);
}

// ==========================================
// PROPERTY-BASED TESTS (RAPIDCHECK)
// ==========================================

struct OrderAction {
    bool isCancel;
    uint64_t id;
    int64_t price;
    uint32_t qty;
    Side side;
};

// Generate random order actions for the property tests
namespace rc {
    template <>
    struct Arbitrary<OrderAction> {
        static Gen<OrderAction> arbitrary() {
            return gen::build<OrderAction>(
                gen::set(&OrderAction::isCancel, gen::weightedElement<bool>({{1, true}, {4, false}})), // 20% chance to cancel
                gen::set(&OrderAction::id, gen::inRange<uint64_t>(1, 1000)),
                gen::set(&OrderAction::price, gen::inRange<int64_t>(50, 150)),
                gen::set(&OrderAction::qty, gen::inRange<uint32_t>(1, 100)),
                gen::set(&OrderAction::side, gen::element(Side::Buy, Side::Sell))
            );
        }
    };
}

RC_GTEST_PROP(OrderBookProperties, NoOrderMatchesItself, (const std::vector<OrderAction>& actions)) {
    SequenceObserver propertyObs;
    OrderBook testBook(&propertyObs);

    for (const auto& action : actions) {
        if (action.isCancel) {
            testBook.cancelOrder(action.id);
        } else {
            // Only add if it doesn't already exist to avoid duplicate ID edge cases
            if (testBook.getOrder(action.id) == nullptr) {
                testBook.addOrder(action.id, action.price, action.qty, action.side);
            }
        }
    }

    // PROPERTY: An order ID should never appear as both maker and taker in the same trade
    for (const auto& trade : propertyObs.trades) {
        RC_ASSERT(trade.makerId != trade.takerId);
    }
}

RC_GTEST_PROP(OrderBookProperties, BookNeverCrosses, (const std::vector<OrderAction>& actions)) {
    SequenceObserver propertyObs;
    OrderBook testBook(&propertyObs);

    for (const auto& action : actions) {
        if (action.isCancel) {
            testBook.cancelOrder(action.id);
        } else {
            if (testBook.getOrder(action.id) == nullptr) {
                testBook.addOrder(action.id, action.price, action.qty, action.side);
            }
        }

        int64_t bestBid = testBook.getBestBid();
        int64_t bestAsk = testBook.getBestAsk();

        // PROPERTY: The matching engine must resolve all crosses
        // Best Bid must strictly be less than Best Ask (unless one side of the book is empty)
        if (bestBid > 0 && bestAsk > 0) {
            RC_ASSERT(bestBid < bestAsk);
        }
    }
}