//
// Created by Simon Konieczny on 31/05/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include "../../src/OFICalculator.hpp"

class OFICalculatorTest : public ::testing::Test {
protected:
    SPSCQueue<BookUpdate> bookUpdateQueue_{1024};
    SPSCQueue<ITradeObserver::TradeRecord> tradeQueue_{1024};
    std::atomic<bool> producerDone_{false};

    OFICalculator calc{bookUpdateQueue_, tradeQueue_, producerDone_};
};

TEST_F(OFICalculatorTest, CalculatesOrderFlowImbalance) {
    // 1. Initial State (T=1000)
    bookUpdateQueue_.push(BookUpdate{100, 102, 10, 10, 1000});

    // 2. Bid volume increases by 5 (Demand increases -> positive OFI)
    bookUpdateQueue_.push(BookUpdate{100, 102, 15, 10, 2000});

    // 3. Ask price drops (Supply becomes more aggressive -> negative OFI)
    bookUpdateQueue_.push(BookUpdate{100, 101, 15, 5,  3000});

    producerDone_ = true;
    calc.runOfiCalculator();

    // Event 2: dW = +5, dV = 0 => OFI = +5
    // Event 3: dW = 0,  dV = 5 => OFI = -5
    // Total rolling OFI = 0
    EXPECT_EQ(calc.getOFI_1s(), 0);
}

TEST_F(OFICalculatorTest, PrunesOldEventsOutsideTimeWindow) {
    bookUpdateQueue_.push(BookUpdate{100, 102, 10, 10, 1'000'000'000}); // Init

    // T=2s: Bid volume +10 -> OFI = +10
    bookUpdateQueue_.push(BookUpdate{100, 102, 20, 10, 2'000'000'000});

    // T=4s: Ask volume -5 -> OFI = +5
    bookUpdateQueue_.push(BookUpdate{100, 102, 20, 5,  4'000'000'000});

    producerDone_ = true;
    calc.runOfiCalculator();

    // At T=4s, the 1-second window only sees the T=4s event (+5).
    // The T=2s event (+10) is older than 1 second (1'000'000'000 ns) and is pruned.
    EXPECT_EQ(calc.getOFI_1s(), 5);

    // The 5-second window retains both (+15)
    EXPECT_EQ(calc.getOFI_5s(), 15);
}

TEST_F(OFICalculatorTest, CapturesRecentTrades) {
    for (int i = 1; i <= 6; ++i) {
        tradeQueue_.push(ITradeObserver::TradeRecord{1, 2, 100, 100 + i});
    }

    producerDone_ = true;
    calc.runTradeCapture();

    // Should only hold the latest 5 trades
    ASSERT_EQ(calc.recentTrades.size(), 5);
    EXPECT_EQ(calc.recentTrades.front().price, 106); // Latest trade
    EXPECT_EQ(calc.recentTrades.back().price, 102);  // Oldest retained trade
}