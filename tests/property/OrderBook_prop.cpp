//
// Created by Simon Konieczny on 24/05/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include "../../src/OrderBook.hpp"

#include "../common/OrderBookHelpers.cpp"

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
    SPSCQueue<BookUpdate> bookUpdateQueue(65536);
    SPSCQueue<ITradeObserver::TradeRecord> tradeQueue(65536);
    OrderBook testBook(bookUpdateQueue, tradeQueue);

    std::vector<ITradeObserver::TradeRecord> allTrades;

    for (const auto& action : actions) {
        if (action.isCancel) {
            testBook.cancelOrder(action.id, 0);
        } else {
            // Only add if it doesn't already exist to avoid duplicate ID edge cases
            if (testBook.getOrder(action.id) == nullptr) {
                testBook.addOrder(action.id, action.price, action.qty, action.traderId, action.side, 0, STPBehavior::None);
            }
        }

        // Drain the lock-free queue into local vector so it doesn't hit capacity
        // during long rapidcheck sequence generations
        ITradeObserver::TradeRecord trade;
        while (tradeQueue.pop(trade)) {
            allTrades.push_back(trade);
        }
    }

    // An order ID should never appear as both maker and taker in the same trade
    for (const auto& trade : allTrades) {
        RC_ASSERT(trade.mId != trade.tId);
    }
}

RC_GTEST_PROP(OrderBookProperties, BookNeverCrosses, (const std::vector<OrderAction>& actions)) {
    SPSCQueue<BookUpdate> bookUpdateQueue(65536);
    SPSCQueue<ITradeObserver::TradeRecord> tradeQueue(65536);
    OrderBook testBook(bookUpdateQueue, tradeQueue);

    for (const auto& action : actions) {
        if (action.isCancel) {
            testBook.cancelOrder(action.id, 0);
        } else {
            if (testBook.getOrder(action.id) == nullptr) {
                testBook.addOrder(action.id, action.price, action.qty, action.traderId, action.side, 0, STPBehavior::None);
            }
        }

        int64_t bestBid = testBook.getBestBid();
        int64_t bestAsk = testBook.getBestAsk();

        // The matching engine must resolve all crosses
        // Best Bid must strictly be less than Best Ask (unless one side of the book is empty)
        if (bestBid > 0 && bestAsk > 0) {
            RC_ASSERT(bestBid < bestAsk);
        }

        // Empty the queues so they don't block
        BookUpdate update;
        while (bookUpdateQueue.pop(update)) {}

        ITradeObserver::TradeRecord trade;
        while (tradeQueue.pop(trade)) {}
    }
}