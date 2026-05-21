#pragma once
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <vector>

enum class Side { Buy, Sell };

class ITradeObserver {
public:
    virtual ~ITradeObserver() = default;
    virtual void onTrade(uint64_t makerId, uint64_t takerId, uint32_t qty, int64_t price) = 0;
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
    explicit OrderBook(ITradeObserver* obs = nullptr) : observer(obs), pool(100000), limitPool(1000), lastTradePrice(0) {}

    void addOrder(uint64_t id, int64_t price, uint32_t quantity, uint32_t traderId, Side side, STPBehavior stpPolicy);

    void cancelOrder(uint64_t id);

    Order* getOrder(uint64_t id);

    int64_t getBestBid() const {
        if (bids.empty()) return 0;
        return bids.front()->price; // O(1) Highest Bid
    }

    int64_t getBestAsk() const {
        if (asks.empty()) return 0;
        return asks.front()->price; // O(1) Lowest Ask
    }

    BookSnapshot getSnapshot(int depth);

    int64_t getLastTradePrice() const {return lastTradePrice;}

private:
    ITradeObserver* observer;
    OrderPool pool;
    LimitPool limitPool;
    std::vector<LimitLevel*> bids;
    std::vector<LimitLevel*> asks;

    std::unordered_map<uint64_t, Order*> orderMap;

    int64_t lastTradePrice;

    void match(Order* incomingOrder);
    void executeMatch(Order* incomingOrder, LimitLevel* level);
};