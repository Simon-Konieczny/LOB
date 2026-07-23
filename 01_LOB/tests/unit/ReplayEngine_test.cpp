//
// Created by Simon Konieczny on 31/05/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include "../../src/ReplayEngine.hpp"

class ReplayEngineTest : public ::testing::Test {
protected:
    SPSCQueue<NormalizedMsg> msgQueue_{1024};
    SPSCQueue<ITradeObserver::TradeRecord> tradeQueue_{1024};
    std::atomic<bool> producerDone_{false};
    OrderBook orderBook{tradeQueue_};

    ReplayEngine engine{msgQueue_, producerDone_, tradeQueue_, orderBook};
};

TEST_F(ReplayEngineTest, ProcessesAddAndCancelMessages) {
    NormalizedMsg addMsg{};
    addMsg.action = MsgAction::Add;
    addMsg.orderId = 1;
    addMsg.price = 100;
    addMsg.quantity = 50;
    addMsg.side = Side::Buy;
    addMsg.timestamp = 1000;
    msgQueue_.push(addMsg);

    NormalizedMsg cancelMsg{};
    cancelMsg.action = MsgAction::Cancel;
    cancelMsg.orderId = 1;
    cancelMsg.timestamp = 2000;
    msgQueue_.push(cancelMsg);

    producerDone_ = true;

    // Speed multiplier 0.0 disables time synchronization delays
    engine.runReplay(0.0);

    // The order was added then immediately canceled
    EXPECT_EQ(engine.getOrderBook().getBestBid(), 0);
}

TEST_F(ReplayEngineTest, ProcessesReplaceMessages) {
    NormalizedMsg addMsg{};
    addMsg.action = MsgAction::Add;
    addMsg.orderId = 1;
    addMsg.price = 100;
    addMsg.quantity = 50;
    addMsg.side = Side::Sell;
    addMsg.timestamp = 1000;
    msgQueue_.push(addMsg);

    NormalizedMsg replaceMsg{};
    replaceMsg.action = MsgAction::Replace;
    replaceMsg.orderId = 1;
    replaceMsg.newOrderId = 2;
    replaceMsg.price = 99; // Replacing with a more aggressive price
    replaceMsg.quantity = 50;
    replaceMsg.timestamp = 2000;
    msgQueue_.push(replaceMsg);

    producerDone_ = true;
    engine.runReplay(0.0);

    // Old order should be gone, new order should dictate best ask
    EXPECT_EQ(engine.getOrderBook().getOrder(1), nullptr);
    EXPECT_NE(engine.getOrderBook().getOrder(2), nullptr);
    EXPECT_EQ(engine.getOrderBook().getBestAsk(), 99);
}

TEST_F(ReplayEngineTest, ProcessesReduceMessages) {
    NormalizedMsg addMsg{};
    addMsg.action = MsgAction::Add;
    addMsg.orderId = 1;
    addMsg.price = 100;
    addMsg.quantity = 50;
    addMsg.side = Side::Buy;
    addMsg.timestamp = 1000;
    msgQueue_.push(addMsg);

    NormalizedMsg reduceMsg{};
    reduceMsg.action = MsgAction::Reduce;
    reduceMsg.orderId = 1;
    reduceMsg.quantity = 50; // Fully reduce the order to clear it
    reduceMsg.timestamp = 2000;
    msgQueue_.push(reduceMsg);

    producerDone_ = true;

    // Run at max speed
    engine.runReplay(0.0);

    // Order was fully reduced, so the best bid should drop back to 0
    EXPECT_EQ(engine.getOrderBook().getBestBid(), 0);
}

TEST_F(ReplayEngineTest, RespectsSpeedMultiplierAndTriggersPreciseSleep) {
    // 1. Add order with a 15ms timestamp to trigger the OS sleep_for (> 1000us)
    NormalizedMsg addMsg{};
    addMsg.action = MsgAction::Add;
    addMsg.orderId = 2;
    addMsg.price = 105;
    addMsg.quantity = 100;
    addMsg.side = Side::Sell;
    addMsg.timestamp = 15000000; // 15,000,000 ns = 15 ms
    msgQueue_.push(addMsg);

    // 2. Reduce order with a 15.5ms timestamp.
    // The 500us delta guarantees it drops into the sub-millisecond
    // asm volatile("pause"/"yield") spin-wait loop instead of yielding to the OS.
    NormalizedMsg reduceMsg{};
    reduceMsg.action = MsgAction::Reduce;
    reduceMsg.orderId = 2;
    reduceMsg.quantity = 25;
    reduceMsg.timestamp = 15500000; // 15.5 ms
    msgQueue_.push(reduceMsg);

    producerDone_ = true;

    // Run at 1.0x speed to activate the preciseSleepUntil branch
    auto start = std::chrono::high_resolution_clock::now();
    engine.runReplay(1.0);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Verify it actually paused execution for at least the 15.5ms requested
    EXPECT_GE(elapsedUs, 15000);

    // Verify the book state after the delay
    EXPECT_EQ(engine.getOrderBook().getBestAsk(), 105);
}