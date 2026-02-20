//
// Created by Simon Konieczny on 20/02/2026.
//

#pragma once
#include <cstdint>

#include "OrderBook.hpp"

enum class MsgType : uint8_t {
    NewOrder = 1,
    CancelOrder = 2
};

#pragma pack(push, 1)
struct NewOrderMsg {
    MsgType type;
    uint64_t orderId;
    int64_t price;
    uint32_t qty;
    Side side;
};

struct CancelOrderMsg {
    MsgType type;
    uint64_t orderId;
};
#pragma pack(pop)