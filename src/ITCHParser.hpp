//
// Created by Simon Konieczny on 21/05/2026.
//

#ifndef LOB_ITCHPARSER_HPP
#define LOB_ITCHPARSER_HPP
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
                if (ticker != targetTicker)
                {
                    ptr += msgLength;
                    continue;
                }

                uint64_t orderId = swap64(msg->orderRefNum);
                uint32_t shares = swap32(msg->shares);
                uint32_t price = swap32(msg->price); // Note: 1234500 means $123.45

                Side side = (msg->side == 'B') ? Side::Buy : Side::Sell;

                book.addOrder(orderId, price, shares, 0, side, STPBehavior::CancelBoth);
                messageCount++;
            }
            else if (msgType == 'X')
            {
                const auto* msg = reinterpret_cast<const ITCH5_CancelOrder*>(ptr);

                // ticker filtering
                std::string_view ticker(msg->stock, 8);
                if (ticker != targetTicker)
                {
                    ptr += msgLength;
                    continue;
                }
            }
            // Add else if (msgType == 'E') { ... } etc.

            ptr += msgLength;
        }

        std::cout << "Parsed " << messageCount << " messages." << std::endl;
    }
};
