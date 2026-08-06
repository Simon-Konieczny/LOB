#pragma once
#include <algorithm>
#include <unordered_map>
#include <vector>

#include "IBookObserver.hpp"
#include "SPSCQueue.hpp"

class OFICalculator;

enum class Side { Buy, Sell };

class ITradeObserver {
public:
    enum class TradeType : uint32_t {
        Manual,   // Trade from manual order entry (addOrder)
        Replay,   // Trade from ITCH replay data
        Unknown
    };

    struct TradeRecord {
        uint64_t mId;      // Maker order ID
        uint64_t tId;      // Taker order ID  
        uint32_t qty;      // Quantity
        int64_t price;     // Price
        Side side;         // Side
        uint64_t timestamp_ns; // Timestamp in nanoseconds
        TradeType type;    // Trade type (Manual/Replay)
    };

    virtual ~ITradeObserver() = default;
    virtual void onTrade(const TradeRecord& tradeRecord) = 0;
};

struct LevelInfo {
    int64_t price;
    uint32_t volume;
};

struct BookSnapshot {
    std::vector<LevelInfo> bids;
    std::vector<LevelInfo> asks;
    int64_t lastTradePrice = 0;
};

enum class STPBehavior : uint8_t {
    None,         // Skip STP
    CancelNewest, // Cancel the incoming order
    CancelOldest, // Cancel the resting order
    CancelBoth    // Cancel resting, reduce incoming by resting size
};

struct Order {
    uint64_t id;
    int64_t price;
    uint32_t quantity;
    uint32_t traderId;
    Side side;
    STPBehavior stpPolicy;

    Order* prev = nullptr;
    Order* next = nullptr;

    Order(uint64_t id, int64_t price, uint32_t quantity, uint32_t traderId, Side side, STPBehavior stpPolicy)
        : id(id), price(price), quantity(quantity), traderId(traderId), side(side), stpPolicy(stpPolicy) {}

    ~Order() = default;
};

struct alignas(64) LimitLevel {
    int64_t price;
    uint32_t totalVolume = 0;

    Order* head = nullptr;
    Order* tail = nullptr;

    void appendOrder(Order* order) {
        if (!head) {
            head = tail = order;
        } else {
            tail->next = order;
            order->prev = tail;
            tail = order;
        }
        totalVolume += order->quantity;
    }

    void removeOrder(Order* order) {
        if (order->prev) order->prev->next = order->next;
        if (order->next) order->next->prev = order->prev;
        if (order == head) head = order->next;
        if (order == tail) tail = order->prev;

        totalVolume -= order->quantity;
        order->next = order->prev = nullptr;
    }

    [[nodiscard]] uint32_t getTotalVolume() const { return totalVolume; }

    LimitLevel(int64_t price, uint32_t total_volume, Order* head, Order* tail)
        : price(price),
          totalVolume(total_volume),
          head(head),
          tail(tail) {}

    ~LimitLevel() = default;
};

class LimitPool
{
public:
    explicit LimitPool(size_t initial_capacity) : chunkSize(initial_capacity)
    {
        grow();
    }

    LimitPool(const LimitPool&) = delete;
    LimitPool& operator=(const LimitPool&) = delete;

    LimitLevel* acquireLevel(int64_t price)
    {
        if (freeList.empty())
        {
            grow();
        }

        LimitLevel* level = freeList.back();
        freeList.pop_back();

        level->price = price;
        level->totalVolume = 0;
        level->head = nullptr;
        level->tail = nullptr;

        return level;
    }

    void releaseLevel(LimitLevel* level)
    {
        freeList.push_back(level);
    }

private:
    void grow()
    {
        std::vector<LimitLevel> newChunk;
        newChunk.reserve(chunkSize);
        for (size_t i = 0; i < chunkSize; ++i)
        {
            newChunk.emplace_back(0, 0, nullptr, nullptr);
        }

        chunks.push_back(std::move(newChunk));

        auto& allocatedChunk = chunks.back();

        for (size_t i = 0; i < chunkSize; ++i)
        {
            freeList.push_back(&allocatedChunk[i]);
        }

        // exponential backoff
        chunkSize *= 2;
    }

    size_t chunkSize;
    std::vector<std::vector<LimitLevel>> chunks;
    std::vector<LimitLevel*> freeList;
};

class OrderPool {
public:
    explicit OrderPool(size_t initial_capacity) : chunkSize(initial_capacity) {
        grow();
    }

    OrderPool(const OrderPool&) = delete;
    OrderPool& operator=(const OrderPool&) = delete;

    Order* acquire(uint64_t id, int64_t price, uint32_t qty, uint32_t traderId, Side side, STPBehavior stpPolicy) {
        if (freeList.empty()) {
            grow(); // Automatically and safely expand memory
        }

        Order* o = freeList.back();
        freeList.pop_back();

        o->id = id;
        o->price = price;
        o->quantity = qty;
        o->traderId = traderId;
        o->side = side;
        o->stpPolicy = stpPolicy;
        o->next = o->prev = nullptr;
        return o;
    }

    void release(Order* o) {
        freeList.push_back(o);
    }

private:
    void grow() {
        std::vector<Order> newChunk;
        newChunk.reserve(chunkSize);
        for (size_t i = 0; i < chunkSize; ++i) {
            newChunk.emplace_back(0, 0, 0, 0, Side::Buy, STPBehavior::CancelBoth);
        }

        chunks.push_back(std::move(newChunk));

        auto& allocatedChunk = chunks.back();
        for (size_t i = 0; i < chunkSize; ++i) {
            freeList.push_back(&allocatedChunk[i]);
        }

        chunkSize *= 2;
    }

    size_t chunkSize;
    std::vector<std::vector<Order>> chunks; // Multi-chunk storage to guarantee pointer stability
    std::vector<Order*> freeList;
};

class OrderBook
{
public:
    explicit OrderBook(SPSCQueue<ITradeObserver::TradeRecord>& tradeQueue) :
    pool(100000), limitPool(1000), lastTradePrice(0), tradeQueue_(tradeQueue) {}

    void addOrder(uint64_t id, int64_t price, uint32_t quantity, uint32_t traderId, Side side, uint64_t timestamp, STPBehavior stpPolicy);

    void replayOrder(uint64_t id, int64_t price, uint32_t quantity, uint32_t traderId, Side side, uint64_t timestamp, STPBehavior stpPolicy);

    void cancelOrder(uint64_t id, uint64_t timestamp);

    void modifyOrder(uint64_t id, int64_t newPrice, uint32_t newQuantity, uint64_t timestamp);

    void reduceOrder(uint64_t id, uint32_t newQuantity, uint64_t timestamp);

    void replaceOrder(uint64_t oldId, uint64_t newId, int64_t newPrice, uint32_t newQuantity, uint64_t timestamp);

    void executeReplayMatch(uint32_t quantity, int64_t price, Side side, uint64_t timestamp) const;

    void executeAndReduceOrder(uint64_t id, uint32_t delta, uint64_t timestamp);

    Order* getOrder(uint64_t id);

    int64_t getBestBid() const
    {
        if (bids_.empty()) return 0;
        return bids_.front()->price; // O(1) Highest Bid
    }

    int64_t getBestAsk() const
    {
        if (asks_.empty()) return 0;
        return asks_.front()->price; // O(1) Lowest Ask
    }

    uint32_t getBestBidVolume() const
    {
        if (bids_.empty()) return 0;
        return bids_.front()->totalVolume;
    }

    uint32_t getBestAskVolume() const
    {
        if (asks_.empty()) return 0;
        return asks_.front()->totalVolume;
    }

    template<size_t N>
    inline void getTopN(std::array<int64_t, N>& out_bids,
                    std::array<int64_t, N>& out_asks,
                    std::array<uint32_t, N>& out_bid_vols,
                    std::array<uint32_t, N>& out_ask_vols) const
    {
        out_bids.fill(0.0);
        out_asks.fill(0.0);
        out_bid_vols.fill(0);
        out_ask_vols.fill(0);

        const size_t bid_limit = std::min(N, bids_.size());
        for (size_t i = 0; i < bid_limit; ++i) {
            out_bids[i] = bids_[i]->price;
            out_bid_vols[i] = bids_[i]->getTotalVolume();
        }

        const size_t ask_limit = std::min(N, asks_.size());
        for (size_t i = 0; i < ask_limit; ++i) {
            out_asks[i]     = static_cast<double>(asks_[i]->price);
            out_ask_vols[i] = asks_[i]->getTotalVolume();
        }
    }

    BookSnapshot getSnapshot(int depth);

    int64_t getLastTradePrice() const {return lastTradePrice;}

    void addObserver(IBookObserver* obs)
    {
        observers_.push_back(obs);
    }

private:
    std::vector<IBookObserver*> observers_;
    SPSCQueue<ITradeObserver::TradeRecord>& tradeQueue_;
    OrderPool pool;
    LimitPool limitPool;
    std::vector<LimitLevel*> bids_;
    std::vector<LimitLevel*> asks_;

    std::unordered_map<uint64_t, Order*> orderMap;

    int64_t lastTradePrice;

    void match(Order* incomingOrder);
    void executeMatch(Order* incomingOrder, LimitLevel* level);
    void internalAddOrder(Order* newOrder, uint64_t id, int64_t price, uint64_t timestamp);
    void internalReduceOrder(Order* order, uint64_t newQuantity, uint32_t delta, uint64_t timestamp);
    void fireTradeUpdate(uint64_t makerId, uint64_t takerId, uint32_t quantity, int64_t price, Side side, uint64_t timestamp, ITradeObserver::TradeType type) const;

    void notifyBookUpdate(const uint64_t timestamp) const
    {
        const auto update = BookUpdate{
            getBestBid(),
            getBestAsk(),
            getBestBidVolume(),
            getBestAskVolume(),
            timestamp
            };

        for (auto* obs : observers_) {
            obs->onBookUpdate(update);
        }
    }
};