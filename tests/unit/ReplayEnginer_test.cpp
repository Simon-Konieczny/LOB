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
    SPSCQueue<BookUpdate> bookUpdateQueue_{1024};
    SPSCQueue<ITradeObserver::TradeRecord> tradeQueue_{1024};
    std::atomic<bool> producerDone_{false};

    ReplayEngine engine{msgQueue_, producerDone_, bookUpdateQueue_, tradeQueue_};
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