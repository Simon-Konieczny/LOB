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
    OFICalculator calc{};
};

TEST_F(OFICalculatorTest, CalculatesOrderFlowImbalance) {
    // 1. Initial State (T=1000)
    calc.onBookUpdate(BookUpdate{100, 102, 10, 10, 1000});

    // 2. Bid volume increases by 5 (Demand increases -> positive OFI)
    calc.onBookUpdate(BookUpdate{100, 102, 15, 10, 2000});

    // 3. Ask price drops (Supply becomes more aggressive -> negative OFI)
    calc.onBookUpdate(BookUpdate{100, 101, 15, 5,  3000});

    // Event 2: dW = +5, dV = 0 => OFI = +5
    // Event 3: dW = 0,  dV = 5 => OFI = -5
    // Total rolling OFI = 0
    EXPECT_EQ(calc.getOFI_1s(), 0);
}

TEST_F(OFICalculatorTest, PrunesOldEventsOutsideTimeWindow) {
    calc.onBookUpdate(BookUpdate{100, 102, 10, 10, 1'000'000'000}); // Init

    // T=2s: Bid volume +10 -> OFI = +10
    calc.onBookUpdate(BookUpdate{100, 102, 20, 10, 2'000'000'000});

    // T=4s: Ask volume -5 -> OFI = +5
    calc.onBookUpdate(BookUpdate{100, 102, 20, 5,  4'000'000'000});

    // At T=4s, the 1-second window only sees the T=4s event (+5).
    // The T=2s event (+10) is older than 1 second (1'000'000'000 ns) and is pruned.
    EXPECT_EQ(calc.getOFI_1s(), 5);

    // The 5-second window retains both (+15)
    EXPECT_EQ(calc.getOFI_5s(), 15);
}

TEST_F(OFICalculatorTest, EvaluatesPriceLevelChangesAndZeroOFI) {
    // Init state
    calc.onBookUpdate(BookUpdate{100, 105, 10, 10, 1000});

    // 1. Bid price improves (> prev_bid_price)
    // Demand increases. dW = update.bidVol = 5. dV = 0. OFI = +5.
    calc.onBookUpdate(BookUpdate{101, 105, 5, 10, 2000});

    // 2. Ask price rises (> prev_ask_price)
    // Supply drops. dW = 0. dV = -prev_ask_vol = -10. OFI = 0 - (-10) = +10.
    calc.onBookUpdate(BookUpdate{101, 106, 5, 8, 3000});

    // 3. Bid price drops (< prev_bid_price)
    // Demand drops. dW = -prev_bid_vol = -5. dV = 0. OFI = -5 + 0 = -5.
    calc.onBookUpdate(BookUpdate{99, 106, 12, 8, 4000});

    // 4. Zero OFI (No change in state)
    // dW = 0. dV = 0. OFI = 0. (Fails the current_ofi != 0 check, skips pushing)
    calc.onBookUpdate(BookUpdate{99, 106, 12, 8, 5000});

    // Total 1s OFI should be: 5 + 10 - 5 = 10
    EXPECT_EQ(calc.getOFI_1s(), 10);
}

TEST_F(OFICalculatorTest, PrunesThirtySecondWindow) {
    // Init at T=1s
    calc.onBookUpdate(BookUpdate{100, 102, 10, 10, 1'000'000'000});

    // T=2s: Bid volume +10 -> OFI = +10
    calc.onBookUpdate(BookUpdate{100, 102, 20, 10, 2'000'000'000});

    // T=33s: Ask volume -5 -> OFI = +5
    // This jump forces the 30s while-loop to execute and prune the T=2s event.
    calc.onBookUpdate(BookUpdate{100, 102, 20, 5, 33'000'000'000});

    // At T=33s, the T=2s event is 31 seconds old and pruned across all horizons.
    // Only the T=33s event (+5) remains in the rolling sums.
    EXPECT_EQ(calc.getOFI_1s(), 5);
    EXPECT_EQ(calc.getOFI_5s(), 5);
    EXPECT_EQ(calc.getOFI_30s(), 5);
}