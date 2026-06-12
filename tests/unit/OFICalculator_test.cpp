//
// Created by Simon Konieczny on 31/05/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include <thread>
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
    ASSERT_EQ(calc.getRecentTrades().size(), 5);
    EXPECT_EQ(calc.getRecentTrades().front().price, 106); // Latest trade
    EXPECT_EQ(calc.getRecentTrades().back().price, 102);  // Oldest retained trade
}

TEST_F(OFICalculatorTest, EvaluatesPriceLevelChangesAndZeroOFI) {
    // Init state
    bookUpdateQueue_.push(BookUpdate{100, 105, 10, 10, 1000});

    // 1. Bid price improves (> prev_bid_price)
    // Demand increases. dW = update.bidVol = 5. dV = 0. OFI = +5.
    bookUpdateQueue_.push(BookUpdate{101, 105, 5, 10, 2000});

    // 2. Ask price rises (> prev_ask_price)
    // Supply drops. dW = 0. dV = -prev_ask_vol = -10. OFI = 0 - (-10) = +10.
    bookUpdateQueue_.push(BookUpdate{101, 106, 5, 8, 3000});

    // 3. Bid price drops (< prev_bid_price)
    // Demand drops. dW = -prev_bid_vol = -5. dV = 0. OFI = -5 + 0 = -5.
    bookUpdateQueue_.push(BookUpdate{99, 106, 12, 8, 4000});

    // 4. Zero OFI (No change in state)
    // dW = 0. dV = 0. OFI = 0. (Fails the current_ofi != 0 check, skips pushing)
    bookUpdateQueue_.push(BookUpdate{99, 106, 12, 8, 5000});

    producerDone_ = true;
    calc.runOfiCalculator();

    // Total 1s OFI should be: 5 + 10 - 5 = 10
    EXPECT_EQ(calc.getOFI_1s(), 10);
}

TEST_F(OFICalculatorTest, PrunesThirtySecondWindow) {
    // Init at T=1s
    bookUpdateQueue_.push(BookUpdate{100, 102, 10, 10, 1'000'000'000});

    // T=2s: Bid volume +10 -> OFI = +10
    bookUpdateQueue_.push(BookUpdate{100, 102, 20, 10, 2'000'000'000});

    // T=33s: Ask volume -5 -> OFI = +5
    // This jump forces the 30s while-loop to execute and prune the T=2s event.
    bookUpdateQueue_.push(BookUpdate{100, 102, 20, 5, 33'000'000'000});

    producerDone_ = true;
    calc.runOfiCalculator();

    // At T=33s, the T=2s event is 31 seconds old and pruned across all horizons.
    // Only the T=33s event (+5) remains in the rolling sums.
    EXPECT_EQ(calc.getOFI_1s(), 5);
    EXPECT_EQ(calc.getOFI_5s(), 5);
    EXPECT_EQ(calc.getOFI_30s(), 5);
}

TEST_F(OFICalculatorTest, ConsumersSpinWaitWhenQueuesAreEmpty) {
    // Start both consumers in background threads
    std::thread ofiThread([this]() { calc.runOfiCalculator(); });
    std::thread tradeThread([this]() { calc.runTradeCapture(); });

    // Sleep briefly to ensure both threads exhaust their empty queues
    // and fall into the `else` branch, hitting the assembly spin-locks.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // Signal completion
    producerDone_ = true;

    ofiThread.join();
    tradeThread.join();

    // If execution reaches here without deadlocking, the empty-queue
    // spin-locks and the subsequent producerDone break-conditions executed correctly.
    SUCCEED();
}