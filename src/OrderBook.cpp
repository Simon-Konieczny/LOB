#include "OrderBook.hpp"
#include "OFICalculator.hpp"

#include <chrono>

void OrderBook::addOrder(uint64_t id, int64_t price, uint32_t quantity, uint32_t traderId, Side side, uint64_t timestamp, STPBehavior stpPolicy) {
    auto* newOrder = pool.acquire(id, price, quantity, traderId, side, stpPolicy);
    orderMap[id] = newOrder;

    match(newOrder);

    internalAddOrder(newOrder, id, price, timestamp);
}

void OrderBook::replayOrder(uint64_t id, int64_t price, uint32_t quantity, uint32_t traderId, Side side, uint64_t timestamp, STPBehavior stpPolicy)
{
    // consider quantity = 0 here to avoid needles acquire/release cycles
    auto* newOrder = pool.acquire(id, price, quantity, traderId, side, stpPolicy);
    orderMap[id] = newOrder;

    internalAddOrder(newOrder, id, price, timestamp);
}

void OrderBook::internalAddOrder(Order* newOrder, uint64_t id, int64_t price, uint64_t timestamp)
{
    if (newOrder->quantity > 0)
    {
        if (newOrder->side == Side::Buy) {
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
        fireBookUpdate(timestamp);
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
        if (taker->traderId == maker->traderId && taker->stpPolicy != STPBehavior::None)
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

void OrderBook::cancelOrder(uint64_t id, uint64_t timestamp) {
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
    fireBookUpdate(timestamp);
}

void OrderBook::modifyOrder(uint64_t id, int64_t newPrice, uint32_t newQuantity, uint64_t timestamp)
{
    const auto orderIt = orderMap.find(id);
    if (orderIt == orderMap.end()) return;

    Order* order = orderIt->second;

    if (order->price == newPrice && order->quantity == newQuantity) return;

    if (order->price != newPrice || order->quantity < newQuantity)
    {
        // price change: lose time prio
        uint32_t cachedTraderId = order->traderId;
        Side cachedSide = order->side;
        STPBehavior cachedStp = order->stpPolicy;

        cancelOrder(id, timestamp);

        addOrder(id, newPrice, newQuantity, cachedTraderId, cachedSide, timestamp, cachedStp);

    }
    else
    {
        // quantity decrease only
        uint32_t delta = order->quantity - newQuantity;
        order->quantity = newQuantity;

        if (order->side == Side::Buy)
        {
            auto it = std::lower_bound(bids.begin(), bids.end(), order->price,
                [](const LimitLevel* l, int64_t p) {return l->price > p;});

            if (it != bids.end() && (*it)->price == order->price)
            {
                (*it)->totalVolume -= delta;
            }
        } else
        {
            auto it = std::lower_bound(asks.begin(), asks.end(), order->price,
                [](const LimitLevel* l, int64_t p) {return l->price < p;});

            if (it != asks.end() && (*it)->price == order->price)
            {
                (*it)->totalVolume -= delta;
            }
        }
    }
    fireBookUpdate(timestamp);
}

void OrderBook::reduceOrder(uint64_t id, uint32_t delta, uint64_t timestamp)
{
    // for ITCH 5.0 OrderCancel
    const auto orderIt = orderMap.find(id);
    if (orderIt == orderMap.end()) return;

    Order* order = orderIt->second;

    const uint32_t newQuantity = order->quantity - delta;

    if (newQuantity == 0)
    {
        cancelOrder(id, timestamp);
        return;
    }

    if (order->side == Side::Buy)
    {
        auto it = std::lower_bound(bids.begin(), bids.end(), order->price,
            [](const LimitLevel* l, int64_t p) {return l->price > p;});

        if (it != bids.end() && (*it)->price == order->price)
        {
            (*it)->totalVolume -= delta;
        }
    } else
    {
        auto it = std::lower_bound(asks.begin(), asks.end(), order->price,
            [](const LimitLevel* l, int64_t p) {return l->price < p;});

        if (it != asks.end() && (*it)->price == order->price)
        {
            (*it)->totalVolume -= delta;
        }
    }

    order->quantity = newQuantity;

    fireBookUpdate(timestamp);
}

void OrderBook::replaceOrder(uint64_t oldId, uint64_t newId, int64_t newPrice, uint32_t newQuantity, uint64_t timestamp)
{
    Order* oldOrder = getOrder(oldId);
    if (!oldOrder) return;

    Side side = oldOrder->side;

    cancelOrder(oldId, timestamp);

    replayOrder(newId, newPrice, newQuantity, 0, side, timestamp, STPBehavior::None);
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

// book update for OFI calculation
void OrderBook::fireBookUpdate(const uint64_t timestamp) const
{
    // if (ofiCalculator)
    // {
    //     ofiCalculator->onBookUpdate(BookUpdate(
    //         getBestBid(),
    //         getBestAsk(),
    //         getBestBidVolume(),
    //         getBestAskVolume(),
    //         timestamp
    //     ));
    // }
    orderUpdateQueue.push(BookUpdate(
        getBestBid(),
        getBestAsk(),
        getBestBidVolume(),
        getBestAskVolume(),
        timestamp
        ));
}
