//
// Created by Simon Konieczny on 19/02/2026.
//
#include <thread>

#include "ITCHParser.hpp"
#include "NormalizedMsg.hpp"
#include "ReplayEngine.hpp"
#include "SPSCQueue.hpp"
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <iostream>
#include <iomanip>
#include <deque>
#include <random>
#include <chrono>

#include "OrderBook.hpp"

class VisualObserver : public ITradeObserver {
public:
    struct TradeRecord {
        uint64_t mId; uint64_t tId; uint32_t qty; int64_t price;
    };
    std::deque<TradeRecord> recentTrades;

    void onTrade(uint64_t makerId, uint64_t takerId, uint32_t qty, int64_t price) override {
        recentTrades.push_front({makerId, takerId, qty, price});
        if (recentTrades.size() > 5) recentTrades.pop_back();
    }
};

void renderUI(const BookSnapshot& snap, VisualObserver& obs, uint64_t totalOrders) {
    std::cout << "\033[2J\033[1;1H";

    std::cout << "========================================================\n";
    std::cout << "   LOB MATCHING ENGINE DEMO | Orders Processed: " << totalOrders << "\n";
    std::cout << "   Last Traded Fill Price  : " << snap.lastTradePrice << "\n";
    std::cout << "========================================================\n";

    std::cout << std::setw(15) << "PRICE" << " | " << std::setw(15) << "QUANTITY" << "\n";
    std::cout << "--------------------------------------------------------\n";

    // Print Asks (Red)
    for (auto it = snap.asks.rbegin(); it != snap.asks.rend(); ++it) {
        std::cout << "\033[31m" << std::setw(15) << it->price
                  << " | " << std::setw(15) << it->volume << "\033[0m\n";
    }

    std::cout << "---------- SPREAD: " << (snap.asks.empty() || snap.bids.empty() ? 0 : snap.asks[0].price - snap.bids[0].price) << " ----------\n";

    // Print Bids (Green)
    for (const auto& bid : snap.bids) {
        std::cout << "\033[32m" << std::setw(15) << bid.price
                  << " | " << std::setw(15) << bid.volume << "\033[0m\n";
    }

    std::cout << "\nRECENT TRADES:\n";
    for (const auto& t : obs.recentTrades) {
        std::cout << " [+] Match: ID " << t.tId << " hit ID " << t.mId
                  << " | Qty: " << t.qty << " @ " << t.price << "\n";
    }
}

struct DirectConsumer
{
    OrderBook& book;
    inline void onMessage(const NormalizedMsg& msg)
    {
        switch (msg.action)
        {
        case MsgAction::Add:
            book.replayOrder(msg.orderId, msg.price, msg.quantity, 0, msg.side, STPBehavior::None);
            break;
        case MsgAction::Reduce:
            book.reduceOrder(msg.orderId, msg.quantity);
            break;
        case MsgAction::Cancel:
            book.cancelOrder(msg.orderId);
            break;
        case MsgAction::Replace:
            book.replaceOrder(msg.orderId, msg.newOrderId, msg.price, msg.quantity);
            break;
        }
    }
};

class QueueProducerAdapter {
public:
    QueueProducerAdapter(SPSCQueue<NormalizedMsg>& queue) : queue_(queue) {}

    inline void onMessage(const NormalizedMsg& msg) {
        while (!queue_.push(msg)) {
            #if defined(__aarch64__) || defined(__arm__)
                        __asm__ volatile("yield" ::: "memory");
            #else
                        __asm__ volatile("pause" ::: "memory");
            #endif
        }
    }
private:
    SPSCQueue<NormalizedMsg>& queue_;
};

int main() {
    VisualObserver obs;
    SPSCQueue<NormalizedMsg> queue(65536);
    std::atomic<bool> producerDone(false);

    QueueProducerAdapter adapter(queue);
    ITCHParser<QueueProducerAdapter> parser(adapter);

    std::string dataFile = "./data/12302019.NASDAQ_ITCH50";
    std::string targetTicker = "AAPL    ";
    double speedMultiplier = 3000.0;

    std::thread parserThread([&]()
    {
        std::cout << "Starting parser thread...\n";
        parser.parse(dataFile, targetTicker);
        producerDone.store(true, std::memory_order_release);
        std::cout << "parserThread thread done.\n";
    });

    ReplayEngine engine(queue, producerDone);
    engine.runReplay(speedMultiplier);

    parserThread.join();

    auto snap = engine.getOrderBook().getSnapshot(10);
    renderUI(snap, obs, 0);

    return 0;
}
