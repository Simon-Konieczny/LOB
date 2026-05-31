//
// Created by Simon Konieczny on 24/05/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <vector>
#include "../../src/OrderBook.hpp"

// Observer to record the exact sequence of trades for validation
class SequenceObserver : public ITradeObserver {
public:
    std::vector<TradeRecord> trades;
    void onTrade(const TradeRecord& tradeRecord) override {
        trades.push_back(tradeRecord);
    }
};