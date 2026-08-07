//
// Created by Simon Konieczny on 22/05/2026.
//

#ifndef LOB_REPLAYENGINE_HPP
#define LOB_REPLAYENGINE_HPP
#include <atomic>
#include <chrono>
#include <thread>

#include "ITCHParser.hpp"
#include "OrderBook.hpp"
#include "SPSCQueue.hpp"

#endif


class ReplayEngine
{
public:
    ReplayEngine(SPSCQueue<NormalizedMsg>& queue,
        std::atomic<bool>& producerDone,
        SPSCQueue<ITradeObserver::TradeRecord>& tradeQueue,
        OrderBook& orderBook)
        : queue_(queue), tradeQueue_(tradeQueue), producerDone_(producerDone), book_(orderBook) {}

    void runReplay(double speedMultiplier = 0.0)
    {
        std::cout << "Starting replay. Speed: " << speedMultiplier << std::endl;

        const auto processStartTime = std::chrono::high_resolution_clock::now();
        size_t msgCount = 0;
        NormalizedMsg msg;

        while (true)
        {
            if (queue_.pop(msg))
            {
                if (speedMultiplier > 0.0 && msg.timestamp > 0) {
                    auto targetWaitNs = static_cast<uint64_t>(msg.timestamp / speedMultiplier);
                    auto wakeUpTime = processStartTime + std::chrono::nanoseconds(targetWaitNs);
                    preciseSleepUntil(wakeUpTime);
                }

                processMessage(msg);
                msgCount++;
            } else
            {
                if (producerDone_.load(std::memory_order_acquire)) {
                    // Try one last time to ensure we didn't miss a tail item between checks
                    if (!queue_.pop(msg)) break;
                }

                // Spin-wait for the next message from the producer
                #if defined(__aarch64__) || defined(__arm__)
                                    __asm__ volatile("yield" ::: "memory");
                #else
                                    __asm__ volatile("pause" ::: "memory");
                #endif
            }
        }

        std::cout << msgCount << " msgs" << std::endl;
    }

    [[nodiscard]] OrderBook& getOrderBook() const { return book_;}

private:
    SPSCQueue<NormalizedMsg>& queue_;
    SPSCQueue<ITradeObserver::TradeRecord>& tradeQueue_;
    std::atomic<bool>& producerDone_;
    OrderBook& book_;

    __attribute__((always_inline)) inline void processMessage(const NormalizedMsg& msg) {
        switch (msg.action)
        {
        case MsgAction::Add:
            book_.replayOrder(msg.orderId, msg.price, msg.quantity, 0, msg.side, msg.timestamp, STPBehavior::None);
            break;
        case MsgAction::Reduce:
            book_.reduceOrder(msg.orderId, msg.quantity, msg.timestamp);
            break;
        case MsgAction::Cancel:
            book_.cancelOrder(msg.orderId, msg.timestamp);
            break;
        case MsgAction::Replace:
            book_.replaceOrder(msg.orderId, msg.newOrderId, msg.price, msg.quantity, msg.timestamp);
            break;
        case MsgAction::Trade:
            book_.executeReplayMatch(msg.quantity, msg.price, msg.side, msg.timestamp);
        case MsgAction::Execute:
            book_.executeAndReduceOrder(msg.orderId, msg.quantity, msg.timestamp);
        }
    }

    static inline void preciseSleepUntil(std::chrono::high_resolution_clock::time_point wakeUpTime) {
        auto now = std::chrono::high_resolution_clock::now();
        if (now >= wakeUpTime) return;

        auto diffUs = std::chrono::duration_cast<std::chrono::microseconds>(wakeUpTime - now).count();
        if (diffUs > 1000) {
            std::this_thread::sleep_for(std::chrono::microseconds(diffUs - 500));
        }

        while (std::chrono::high_resolution_clock::now() < wakeUpTime) {
            #if defined(__aarch64__) || defined(__arm__)
                            __asm__ volatile("yield" ::: "memory");
            #else
                            __asm__ volatile("pause" ::: "memory");
            #endif
        }
    }
};