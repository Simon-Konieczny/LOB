//
// Created by Simon Konieczny on 24/05/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include <vector>
#include "../../src/OrderBook.hpp"

struct TradeRecord {
    uint64_t makerId;
    uint64_t takerId;
    uint32_t qty;
    int64_t price;
};

// Observer to record the exact sequence of trades for validation
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