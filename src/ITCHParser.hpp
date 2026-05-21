//
// Created by Simon Konieczny on 21/05/2026.
//

#ifndef LOB_ITCHPARSER_HPP
#define LOB_ITCHPARSER_HPP
#include <unordered_set>

#include "OrderBook.hpp"

#endif

#pragma once
#include "ITCH50.hpp"
#include "MmapReader.hpp"
#include <iostream>

class ITCHParser {
public:
    static inline uint16_t swap16(uint16_t val) { return __builtin_bswap16(val); }
    static inline uint32_t swap32(uint32_t val) { return __builtin_bswap32(val); }
    static inline uint64_t swap64(uint64_t val) { return __builtin_bswap64(val); }

    void parse(const std::string& filepath, OrderBook& book, const std::string& targetTicker) {
        MmapReader reader(filepath);
        const char* ptr = reader.data();
        const char* end = ptr + reader.size();

        std::unordered_set<uint64_t> activeTargetOrders;

        activeTargetOrders.reserve(10000000);

        size_t messageAddCount = 0;
        size_t messageCancelCount = 0;
        size_t messageDeleteCount = 0;
        size_t messageExecutedCount = 0;

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

                    Side side = (msg->side == 'B') ? Side::Buy : Side::Sell;
                    book.addOrder(orderId, static_cast<int64_t>(swap32(msg->price)),
                                  swap32(msg->shares), 0, side, STPBehavior::CancelBoth);
                    messageAddCount++;
                }
            }
            else if (msgType == 'E')
            {
                const auto* msg = reinterpret_cast<const ITCH5_OrderExecuted*>(ptr);
                uint64_t orderId = swap64(msg->orderRefNum);

                if (activeTargetOrders.find(orderId) != activeTargetOrders.end())
                {
                    book.reduceOrder(orderId, swap32(msg->executedShares));
                    messageExecutedCount++;
                }
            }
            else if (msgType == 'X')
            {
                const auto* msg = reinterpret_cast<const ITCH5_CancelOrder*>(ptr);
                uint64_t orderId = swap64(msg->orderRefNum);

                if (activeTargetOrders.find(orderId) != activeTargetOrders.end())
                {
                    book.reduceOrder(orderId, swap32(msg->canceledShares));
                    messageCancelCount++;
                }
            }
            else if (msgType == 'D')
            {
                const auto* msg = reinterpret_cast<const ITCH5_OrderDelete*>(ptr);
                uint64_t orderId = swap64(msg->orderRefNum);

                if (activeTargetOrders.find(orderId) != activeTargetOrders.end())
                {
                    book.cancelOrder(orderId);
                    activeTargetOrders.erase(orderId);
                    messageDeleteCount++;
                }
            }
            // Add else if (msgType == 'E') { ... } etc.

            ptr += msgLength;
        }

        std::cout << "Parsed " << messageAddCount << " AddOrder "
        << messageCancelCount << " CancelOrder "
        << messageDeleteCount << " OrderDelete "
        << messageExecutedCount << " OrderExecuted messages." << std::endl;
    }
};
