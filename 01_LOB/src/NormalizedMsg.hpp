//
// Created by Simon Konieczny on 21/05/2026.
//

#ifndef LOB_NORMALIZEDMSG_HPP
#define LOB_NORMALIZEDMSG_HPP
#include "OrderBook.hpp"

#endif

#pragma once
#include <cstdint>

enum class MsgAction : uint8_t
{
    Add,
    Reduce,
    Cancel,
    Replace,
    Execute,
    Trade
};

struct NormalizedMsg
{
    MsgAction action = {};
    Side side = {};
    uint32_t quantity = {};
    uint64_t orderId = {};
    uint64_t newOrderId = {};
    int64_t price = {};
    uint64_t timestamp = {};
};
