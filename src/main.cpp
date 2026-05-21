//
// Created by Simon Konieczny on 19/02/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <iostream>
#include <thread>
#include <iomanip>
#include <deque>
#include <random>
#include <chrono>

#include "SPSCQueue.hpp"
#include "Protocol.hpp"
#include "OrderBook.hpp"

struct MarketEvent {
    MsgType type;
    union {
        NewOrderMsg newOrder;
        CancelOrderMsg cancel;
    } data;
};

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

void engineThread(SPSCQueue<MarketEvent, 1024>& queue, OrderBook& book) {
    MarketEvent event{};
    while (true) {
        if (queue.pop(event)) {
            if (event.type == MsgType::NewOrder) {
                book.addOrder(event.data.newOrder.orderId,
                             event.data.newOrder.price,
                             event.data.newOrder.qty,
                             event.data.newOrder.traderId,
                             event.data.newOrder.side,
                             event.data.newOrder.stpPolicy);
            } else if (event.type == MsgType::CancelOrder) {
                book.cancelOrder(event.data.cancel.orderId);
            }
        }
    }
}

int main() {
    VisualObserver obs;
    OrderBook engine(&obs);
    std::mt19937 rng(std::random_device{}());
    uint64_t orderIdCounter = 1;

    for(int i = 0; i < 10; ++i) {
        engine.addOrder(orderIdCounter++, 1000 + (i*2), 10 + i, orderIdCounter, Side::Sell,STPBehavior::CancelBoth);
        engine.addOrder(orderIdCounter++, 990 - (i*2), 10 + i, orderIdCounter, Side::Buy, STPBehavior::CancelBoth);
    }

    while (true) {
        std::uniform_int_distribution<int> dist(0, 10);
        int action = dist(rng);

        if (action < 8) {
            int64_t priceShift = std::uniform_int_distribution<int>(-5, 5)(rng);
            Side s = (dist(rng) > 5) ? Side::Buy : Side::Sell;
            int64_t basePrice = (s == Side::Buy) ? 990 : 1000;
            engine.addOrder(orderIdCounter++, basePrice + priceShift, 5, orderIdCounter, s, STPBehavior::CancelBoth);
        }
        else {
            if (dist(rng) > 5)
                engine.addOrder(orderIdCounter++, 1010, 15, orderIdCounter, Side::Buy, STPBehavior::CancelBoth);
            else
                engine.addOrder(orderIdCounter++, 980, 15, orderIdCounter, Side::Sell, STPBehavior::CancelBoth);
        }

        auto snap = engine.getSnapshot(5);
        renderUI(snap, obs, orderIdCounter);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
