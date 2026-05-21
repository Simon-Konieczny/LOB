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
    // Setup multiple price levels with multiple orders at the best price
    book.addOrder(1, 100, 10, 1, Side::Sell, STPBehavior::CancelBoth);
    book.addOrder(2, 100, 15, 2, Side::Sell, STPBehavior::CancelBoth);
    book.addOrder(3, 101, 20, 3, Side::Sell, STPBehavior::CancelBoth);
    book.addOrder(4, 102, 50, 4, Side::Sell, STPBehavior::CancelBoth);

    // Send aggressive buy to sweep levels 100 and 101, and partially fill 102
    book.addOrder(5, 105, 55, 6, Side::Buy, STPBehavior::CancelBoth);

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
    book.addOrder(1, 100, 10, 1, Side::Sell, STPBehavior::CancelBoth);
    book.cancelOrder(1);

    // Attempting to cross the now-canceled order
    book.addOrder(2, 100, 10, 3, Side::Buy, STPBehavior::CancelBoth);

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
        book.addOrder(i, 10000 + i, 10, 1, Side::Sell, STPBehavior::CancelBoth);
    }

    EXPECT_EQ(book.getBestAsk(), 10001); // Lowest ask

    // Sweep the first 1500 levels to ensure the linked lists and limits are intact
    book.addOrder(9999, 20000, 15000, 2, Side::Buy, STPBehavior::CancelBoth);

    EXPECT_EQ(obs.trades.size(), 1500);
    EXPECT_EQ(book.getBestAsk(), 11501);
}

TEST_F(OrderBookTest, StpMidQueueExecution) {
    // Build a queue at Price Level 100
    book.addOrder(1, 100, 10, 101, Side::Buy, STPBehavior::CancelOldest); // Firm A
    book.addOrder(2, 100, 10, 102, Side::Buy, STPBehavior::CancelOldest); // Firm B (Target)
    book.addOrder(3, 100, 10, 103, Side::Buy, STPBehavior::CancelOldest); // Firm C

    // Firm B sends an aggressive sweeping order
    book.addOrder(4, 100, 30, 102, Side::Sell, STPBehavior::CancelOldest);

    // Verify the execution sequence
    ASSERT_EQ(obs.trades.size(), 2);

    // First trade should be Firm B hitting Firm A
    EXPECT_EQ(obs.trades[0].makerId, 1);
    EXPECT_EQ(obs.trades[0].takerId, 4);
    EXPECT_EQ(obs.trades[0].qty, 10);

    // Second trade should be Firm B hitting Firm C (Order 2 was canceled via STP)
    EXPECT_EQ(obs.trades[1].makerId, 3);
    EXPECT_EQ(obs.trades[1].takerId, 4);
    EXPECT_EQ(obs.trades[1].qty, 10);

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
    book.addOrder(1, 100, 10, 101, Side::Buy, STPBehavior::CancelOldest); // Firm A at $100
    book.addOrder(2, 100, 10, 102, Side::Buy, STPBehavior::CancelOldest); // Firm B at $100
    book.addOrder(3, 99,  10, 103, Side::Buy, STPBehavior::CancelOldest); // Firm C at $99

    // Firm A modifies their price down to $99
    // Rule: Price change -> Loses queue priority at the new level.
    book.modifyOrder(1, 99, 10);

    // Send a massive sell order that sweeps the $100 and $99 levels
    book.addOrder(4, 99, 30, 104, Side::Sell, STPBehavior::CancelOldest);

    ASSERT_EQ(obs.trades.size(), 3);

    // Firm B was alone at $100
    EXPECT_EQ(obs.trades[0].makerId, 2);

    // Firm C was already at $99, so they should get filled BEFORE Firm A
    EXPECT_EQ(obs.trades[1].makerId, 3);

    // Firm A arrives last because they lost priority during the modification
    EXPECT_EQ(obs.trades[2].makerId, 1);
}

TEST_F(OrderBookTest, ModifyIncreaseQuantityLosesTimePriority) {
    book.addOrder(1, 100, 10, 101, Side::Sell, STPBehavior::CancelOldest); // Firm A
    book.addOrder(2, 100, 10, 102, Side::Sell, STPBehavior::CancelOldest); // Firm B

    // Firm A tries to upsize their order to 20 lots
    // Rule: Quantity Increase -> Loses queue priority to prevent top-of-book hogging.
    book.modifyOrder(1, 100, 20);

    // Send a buyer for 15 lots
    book.addOrder(3, 100, 15, 103, Side::Buy, STPBehavior::CancelOldest);

    ASSERT_EQ(obs.trades.size(), 2);

    // Firm B should trade first (10 lots) because Firm A was sent to the back
    EXPECT_EQ(obs.trades[0].makerId, 2);
    EXPECT_EQ(obs.trades[0].qty, 10);

    // Firm A gets the remaining 5 lots
    EXPECT_EQ(obs.trades[1].makerId, 1);
    EXPECT_EQ(obs.trades[1].qty, 5);
}

TEST_F(OrderBookTest, ModifyDecreaseQuantityRetainsTimePriority) {
    book.addOrder(1, 100, 50, 101, Side::Buy, STPBehavior::CancelOldest); // Firm A
    book.addOrder(2, 100, 10, 102, Side::Buy, STPBehavior::CancelOldest); // Firm B

    // Firm A scales down their risk to 10 lots
    // Rule: Quantity Decrease -> Retains queue priority exactly where it is.
    book.modifyOrder(1, 100, 10);

    // Send a seller for 15 lots
    book.addOrder(3, 100, 15, 103, Side::Sell, STPBehavior::CancelOldest);

    ASSERT_EQ(obs.trades.size(), 2);

    // Firm A should trade first because decreasing size does NOT lose priority
    EXPECT_EQ(obs.trades[0].makerId, 1);
    EXPECT_EQ(obs.trades[0].qty, 10); // Firm A's new total size

    // Firm B gets the remaining 5 lots
    EXPECT_EQ(obs.trades[1].makerId, 2);
    EXPECT_EQ(obs.trades[1].qty, 5);
}

TEST_F(OrderBookTest, ModifyDecreaseUpdatesSnapshotVolume) {
    // Validating the internal state tracking for market data feeds
    book.addOrder(1, 100, 100, 101, Side::Sell, STPBehavior::CancelOldest);
    book.addOrder(2, 100, 50,  102, Side::Sell, STPBehavior::CancelOldest);

    // Initial check: Level 100 should have 150 volume
    BookSnapshot snap1 = book.getSnapshot(1);
    ASSERT_EQ(snap1.asks.size(), 1);
    EXPECT_EQ(snap1.asks[0].volume, 150);

    // Decrease Firm A's order by 90 lots
    book.modifyOrder(1, 100, 10);

    // Level 100 should now accurately reflect 60 volume (10 + 50)
    BookSnapshot snap2 = book.getSnapshot(1);
    ASSERT_EQ(snap2.asks.size(), 1);
    EXPECT_EQ(snap2.asks[0].volume, 60);
}

TEST_F(OrderBookTest, ModifyIdempotentAndInvalid) {
    // Modifying an order that doesn't exist should safely return without crashing
    EXPECT_NO_THROW(book.modifyOrder(999, 100, 10));

    book.addOrder(1, 100, 10, 101, Side::Buy, STPBehavior::CancelOldest);
    book.addOrder(2, 100, 10, 102, Side::Buy, STPBehavior::CancelOldest);

    // Submitting a modification that changes nothing (same price, same qty)
    book.modifyOrder(1, 100, 10);

    // Send seller for 10 lots
    book.addOrder(3, 100, 10, 103, Side::Sell, STPBehavior::CancelOldest);

    // Firm A should still get the fill, proving their queue position wasn't reset
    // by a redundant modify command.
    ASSERT_EQ(obs.trades.size(), 1);
    EXPECT_EQ(obs.trades[0].makerId, 1);
}

// ==========================================
// PROPERTY-BASED TESTS (RAPIDCHECK)
// ==========================================

struct OrderAction {
    bool isCancel;
    uint64_t id;
    int64_t price;
    uint32_t qty;
    uint32_t traderId;
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
                gen::set(&OrderAction::traderId, gen::inRange<uint32_t>(1, 10000)),
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
                testBook.addOrder(action.id, action.price, action.qty, action.traderId, action.side, STPBehavior::CancelBoth);
            }
        }
    }

    // An order ID should never appear as both maker and taker in the same trade
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
                testBook.addOrder(action.id, action.price, action.qty, action.traderId, action.side, STPBehavior::CancelBoth);
            }
        }

        int64_t bestBid = testBook.getBestBid();
        int64_t bestAsk = testBook.getBestAsk();

        // The matching engine must resolve all crosses
        // Best Bid must strictly be less than Best Ask (unless one side of the book is empty)
        if (bestBid > 0 && bestAsk > 0) {
            RC_ASSERT(bestBid < bestAsk);
        }
    }
}