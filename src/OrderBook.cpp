#include "OrderBook.hpp"

void OrderBook::addOrder(uint64_t id, int64_t price, uint32_t quantity, Side side) {
    auto* newOrder = pool.acquire(id, price, quantity, side);
    orderMap[id] = newOrder;

    match(newOrder);

    if (newOrder->quantity > 0) {
        if (side == Side::Buy) {
            if (!bids.contains(price)) {
                bids[price] = new LimitLevel(price, 0, nullptr, nullptr);
            }
            bids[price]->appendOrder(newOrder);
        } else {
            if (!asks.contains(price)) {
                asks[price] = new LimitLevel(price, 0, nullptr, nullptr);
            }
            asks[price]->appendOrder(newOrder);
        }
    } else {
        orderMap.erase(id);
        pool.release(newOrder);
    }
}

void OrderBook::match(Order* taker) {
    if (taker->side == Side::Buy) {
        while (taker->quantity > 0 && !asks.empty()) {
            const auto it = asks.begin();
            if (taker->price < it->first) break;

            executeMatch(taker, it->second);
            if (it->second->head == nullptr) {
                delete it->second;
                asks.erase(it);
            }
        }
    } else {
        while (taker->quantity > 0 && !bids.empty()) {
            auto it = bids.begin();
            if (taker->price > it->first) break;

            executeMatch(taker, it->second);
            if (it->second->head == nullptr) {
                delete it->second;
                bids.erase(it);
            }
        }
    }
}

void OrderBook::executeMatch(Order* taker, LimitLevel* level) {
    while (taker->quantity > 0 && level->head) {
        Order* maker = level->head;
        uint32_t fillQty = std::min(taker->quantity, maker->quantity);

        if (observer)
        {
            observer->onTrade(maker->id, taker->id, fillQty, maker->price);
        }

        taker->quantity -= fillQty;
        maker->quantity -= fillQty;
        level->totalVolume -= fillQty;

        if (maker->quantity == 0) {
            level->removeOrder(maker);
            orderMap.erase(maker->id);
            pool.release(maker);
        }
    }
}

void OrderBook::cancelOrder(uint64_t id) {
    const auto it = orderMap.find(id);
    if (it == orderMap.end()) return;

    Order* order = it->second;
    if (order->side == Side::Buy)
    {
        LimitLevel* level = bids[order->price];
        level->removeOrder(order);

        if (!level->head)
        {
            bids.erase(order->price);
            delete level;
        }
    } else if (order->side == Side::Sell)
    {
        LimitLevel* level = asks[order->price];
        level->removeOrder(order);

        if (!level->head)
        {
            bids.erase(order->price);
            delete level;
        }
    }

    orderMap.erase(id);
    pool.release(order);
}

BookSnapshot OrderBook::getSnapshot(int depth) {
    BookSnapshot snapshot;

    int count = 0;
    for (auto const& [price, level] : bids) {
        if (count++ >= depth) break;
        snapshot.bids.push_back({price, level->totalVolume});
    }

    count = 0;
    for (auto const& [price, level] : asks) {
        if (count++ >= depth) break;
        snapshot.asks.push_back({price, level->totalVolume});
    }

    return snapshot;
}