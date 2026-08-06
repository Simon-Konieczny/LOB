//
// Created by Simon Konieczny on 21/05/2026.
//

#ifndef LOB_ITCHPARSER_HPP
#define LOB_ITCHPARSER_HPP
#include <unordered_set>

#include "NormalizedMsg.hpp"
#include "OrderBook.hpp"

#endif

#pragma once
#include "ITCH50.hpp"
#include "MmapReader.hpp"
#include <iostream>

template <typename MessageConsumer>
class ITCHParser {
    MessageConsumer& consumer;
public:
    explicit ITCHParser(MessageConsumer& cons) : consumer(cons) {}

    static inline uint16_t swap16(uint16_t val) { return __builtin_bswap16(val); }
    static inline uint32_t swap32(uint32_t val) { return __builtin_bswap32(val); }
    static inline uint64_t swap64(uint64_t val) { return __builtin_bswap64(val); }
    static inline uint64_t parse48BitTimestamp(const uint8_t* ts) {
        return (static_cast<uint64_t>(ts[0]) << 40) |
               (static_cast<uint64_t>(ts[1]) << 32) |
               (static_cast<uint64_t>(ts[2]) << 24) |
               (static_cast<uint64_t>(ts[3]) << 16) |
               (static_cast<uint64_t>(ts[4]) << 8)  |
               (static_cast<uint64_t>(ts[5]));
    }

    void parse(const std::string& filepath, const std::string& targetTicker) {
        MmapReader reader(filepath);
        const char* ptr = reader.data();
        const char* end = ptr + reader.size();

        std::unordered_set<uint64_t> activeTargetOrders;

        activeTargetOrders.reserve(10000000);

        size_t messageCount = 0;

        while (ptr < end) {
            // Read the 2-byte message length
            uint16_t msgLength = swap16(*reinterpret_cast<const uint16_t*>(ptr));
            ptr += 2;
            char msgType = *ptr;

            if (msgType == 'A') { // Add Order
                const auto* msg = reinterpret_cast<const ITCH5_AddOrder*>(ptr);

                // ticker filtering
                std::string_view ticker(msg->stock, 8);
                if (ticker == targetTicker)
                {
                    uint64_t orderId = swap64(msg->orderRefNum);
                    activeTargetOrders.insert(orderId);

                    NormalizedMsg normMsg;
                    normMsg.action = MsgAction::Add;
                    normMsg.side = (msg->side == 'B') ? Side::Buy : Side::Sell;
                    normMsg.quantity = swap32(msg->shares);
                    normMsg.orderId = orderId;
                    normMsg.price = static_cast<int64_t>(swap32(msg->price));
                    normMsg.timestamp = parse48BitTimestamp(msg->timestamp);

                    consumer.onMessage(normMsg);

                    messageCount++;
                }
            }
            else if (msgType == 'C')
            {
                const auto* msg = reinterpret_cast<const ITCH5_OrderExecutedWithPrice*>(ptr);
                uint64_t orderId = swap64(msg->orderRefNum);

                if (activeTargetOrders.contains(orderId)) {
                    NormalizedMsg normMsg;
                    normMsg.action = MsgAction::Execute;
                    normMsg.orderId = orderId;
                    normMsg.quantity = swap32(msg->executedShares);
                    normMsg.timestamp = parse48BitTimestamp(msg->timestamp);

                    consumer.onMessage(normMsg);

                    messageCount++;
                }
            }
            else if (msgType == 'E')
            {
                const auto* msg = reinterpret_cast<const ITCH5_OrderExecuted*>(ptr);
                uint64_t orderId = swap64(msg->orderRefNum);

                if (activeTargetOrders.contains(orderId))
                {
                    NormalizedMsg normMsg;
                    normMsg.action = MsgAction::Execute;
                    normMsg.orderId = orderId;
                    normMsg.quantity = swap32(msg->executedShares);
                    normMsg.timestamp = parse48BitTimestamp(msg->timestamp);

                    consumer.onMessage(normMsg);

                    messageCount++;
                }
            }
            else if (msgType == 'F')
            {
                const auto* msg = reinterpret_cast<const ITCH5_AddOrderMPID*>(ptr);

                if (std::string_view ticker(msg->stock, 8); ticker == targetTicker) {
                    uint64_t orderId = swap64(msg->orderRefNum);
                    activeTargetOrders.insert(orderId);

                    NormalizedMsg normMsg;
                    normMsg.action = MsgAction::Add;
                    normMsg.side = (msg->side == 'B') ? Side::Buy : Side::Sell;
                    normMsg.quantity = swap32(msg->shares);
                    normMsg.orderId = orderId;
                    normMsg.price = static_cast<int64_t>(swap32(msg->price));
                    normMsg.timestamp = parse48BitTimestamp(msg->timestamp);

                    consumer.onMessage(normMsg);

                    messageCount++;
                }
            }
            else if (msgType == 'X')
            {
                const auto* msg = reinterpret_cast<const ITCH5_CancelOrder*>(ptr);

                if (uint64_t orderId = swap64(msg->orderRefNum); activeTargetOrders.find(orderId) != activeTargetOrders.end())
                {
                    NormalizedMsg normMsg;
                    normMsg.action = MsgAction::Reduce;
                    normMsg.orderId = orderId;
                    normMsg.quantity = swap32(msg->canceledShares);
                    normMsg.timestamp = parse48BitTimestamp(msg->timestamp);

                    consumer.onMessage(normMsg);

                    messageCount++;
                }
            }
            else if (msgType == 'D')
            {
                const auto* msg = reinterpret_cast<const ITCH5_OrderDelete*>(ptr);

                if (uint64_t orderId = swap64(msg->orderRefNum); activeTargetOrders.contains(orderId))
                {
                    NormalizedMsg normMsg;
                    normMsg.action = MsgAction::Cancel;
                    normMsg.orderId = orderId;
                    normMsg.timestamp = parse48BitTimestamp(msg->timestamp);

                    consumer.onMessage(normMsg);
                    activeTargetOrders.erase(orderId);

                    messageCount++;
                }
            }
            else if (msgType == 'U')
            {
                const auto* msg = reinterpret_cast<const ITCH5_OrderReplace*>(ptr);

                if (uint64_t origOrderId = swap64(msg->originalOrderRefNum); activeTargetOrders.contains(origOrderId))
                {
                    uint64_t newOrderId = swap64(msg->newOrderRefNum);

                    NormalizedMsg normMsg;
                    normMsg.action = MsgAction::Replace;
                    normMsg.orderId = origOrderId;
                    normMsg.newOrderId = newOrderId;
                    normMsg.quantity = swap32(msg->shares);
                    normMsg.price = static_cast<int64_t>(swap32(msg->price));
                    normMsg.timestamp = parse48BitTimestamp(msg->timestamp);

                    consumer.onMessage(normMsg);

                    activeTargetOrders.erase(origOrderId);
                    activeTargetOrders.insert(newOrderId);

                    messageCount++;
                }
            }
            else if (msgType == 'P')
            {
                const auto* msg = reinterpret_cast<const ITCH5_TradeMessage*>(ptr);

                if (std::string_view(msg->stock, 8) == targetTicker)
                {
                    NormalizedMsg normMsg;

                    normMsg.action = MsgAction::Trade;
                    normMsg.quantity = swap32(msg->shares);
                    normMsg.price = static_cast<int64_t>(swap32(msg->price));
                    normMsg.timestamp = parse48BitTimestamp(msg->timestamp);
                    normMsg.side = (msg->side == 'B') ? Side::Buy : Side::Sell;

                    consumer.onMessage(normMsg);
                    messageCount++;
                }
            }

            ptr += msgLength;
        }

        std::cout << "Parsed " << messageCount << " messages." << std::endl;
    }
};
