#include "OrderBook.hpp"

void OrderBook::addOrder(uint64_t id, int64_t price, uint32_t quantity, uint32_t traderId, Side side, STPBehavior stpPolicy) {
    auto* newOrder = pool.acquire(id, price, quantity, traderId, side, stpPolicy);
    orderMap[id] = newOrder;

    match(newOrder);

    if (newOrder->quantity > 0)
    {
        if (side == Side::Buy) {
            // Binary search for descending order O(log N)
            auto it = std::lower_bound(bids.begin(), bids.end(), price,
                [](const LimitLevel* level, int64_t p) {return level->price > p;});

            if (it == bids.end() || (*it)->price != price) {
                LimitLevel* newLevel = limitPool.acquireLevel(price);
                it = bids.insert(it, newLevel);
            }
            (*it)->appendOrder(newOrder);
        } else {
            // Binary search for ascending order
            auto it = std::lower_bound(asks.begin(), asks.end(), price,
                [](const LimitLevel* level, int64_t p) {return level->price < p;});

            if (it == asks.end() || (*it)->price != price) {
                LimitLevel* newLevel = limitPool.acquireLevel(price);
                it = asks.insert(it, newLevel);
            }
            (*it)->appendOrder(newOrder);
        }
    } else {
        orderMap.erase(id);
        pool.release(newOrder);
    }
}

void OrderBook::match(Order* taker) {
    if (taker->side == Side::Buy) {
        while (taker->quantity > 0 && !asks.empty()) {
            LimitLevel* level = asks.front();
            if (taker->price < level->price) break;

            executeMatch(taker, level);

            if (level->head == nullptr) {
                asks.erase(asks.begin());
                limitPool.releaseLevel(level);
            }
        }
    } else {
        while (taker->quantity > 0 && !bids.empty()) {
            LimitLevel* level = bids.front();
            if (taker->price > level->price) break;

            executeMatch(taker, level);

            if (level->head == nullptr) {
                bids.erase(bids.begin());
                limitPool.releaseLevel(level);
            }
        }
    }
}

void OrderBook::executeMatch(Order* taker, LimitLevel* level) {
    Order* maker = level->head;
    while (taker->quantity > 0 && maker != nullptr) {
        Order* nextMaker = maker->next;

        // Self-Trade Prevention
        if (taker->traderId == maker->traderId)
        {
            uint32_t overlapQty = std::min(taker->quantity, maker->quantity);

            if (taker->stpPolicy == STPBehavior::CancelNewest)
            {
                // incoming order rejected
                taker->quantity = 0;
                break;
            }
            else if (taker->stpPolicy == STPBehavior::CancelOldest)
            {
                // resting order ripped from book
                level->totalVolume -= maker->quantity;
                level->removeOrder(maker);
                orderMap.erase(maker->id);
                pool.release(maker);

                maker = nextMaker;
                continue;
            }
            else if (taker->stpPolicy == STPBehavior::CancelBoth)
            {
                // both orders reduced by overlapping amount
                taker->quantity -= overlapQty;
                maker->quantity -= overlapQty;
                level->totalVolume -= overlapQty;

                if (maker->quantity == 0)
                {
                    level->removeOrder(maker);
                    orderMap.erase(maker->id);
                    pool.release(maker);
                }

                maker = nextMaker;
                if (taker->quantity == 0) break;
                continue;
            }
        }
        uint32_t fillQty = std::min(taker->quantity, maker->quantity);

        lastTradePrice = maker->price;

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

        maker = nextMaker;
    }
}

void OrderBook::cancelOrder(uint64_t id) {
    const auto orderIt = orderMap.find(id);
    if (orderIt == orderMap.end()) return; // order not found

    Order* order = orderIt->second;
    if (order->side == Side::Buy)
    {
        auto it = std::lower_bound(bids.begin(), bids.end(), order->price,
            [](const LimitLevel* l, int64_t p) {return l->price > p;});
        if (it != bids.end() && (*it)->price == order->price)
        {
            LimitLevel* level = *it;
            level->removeOrder(order);
            if (level->head == nullptr)
            {
                bids.erase(it);
                limitPool.releaseLevel(level);
            }
        }
    } else {
        auto it = std::lower_bound(asks.begin(), asks.end(), order->price,
            [](const LimitLevel* l, int64_t p) {return l->price <p;});
        if (it != asks.end() && (*it)->price == order->price)
        {
            LimitLevel* level = *it;
            level->removeOrder(order);
            if (level->head == nullptr)
            {
                asks.erase(it);
                limitPool.releaseLevel(level);
            }
        }
    }

    orderMap.erase(id);
    pool.release(order);
}

Order* OrderBook::getOrder(uint64_t id)
{
    const auto it = orderMap.find(id);
    if (it == orderMap.end()) return nullptr;
    return it->second;
}

BookSnapshot OrderBook::getSnapshot(int depth) {
    BookSnapshot snapshot;
    snapshot.lastTradePrice = lastTradePrice;

    int count = 0;
    for (LimitLevel* level : bids) {
        if (count++ >= depth) break;
        snapshot.bids.push_back({level->price, level->totalVolume});
    }

    count = 0;
    for (LimitLevel* level : asks) {
        if (count++ >= depth) break;
        snapshot.asks.push_back({level->price, level->totalVolume});
    }

    return snapshot;
}