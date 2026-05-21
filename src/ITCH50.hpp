//
// Created by Simon Konieczny on 21/05/2026.
//

#ifndef LOB_ITCH50_HPP
#define LOB_ITCH50_HPP

#endif

#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct ITCH5_AddOrder {
    char msgType;           // 'A'
    uint16_t stockLocate;
    uint16_t trackingNum;
    uint8_t timestamp[6];   // 48-bit integer (nanoseconds since midnight)
    uint64_t orderRefNum;   // Unique ID for the order
    char side;              // 'B' for Buy, 'S' for Sell
    uint32_t shares;
    char stock[8];          // Ticker symbol, right-padded with spaces
    uint32_t price;         // Integer price (implied 4 decimal places)
};

// 'E' - Order Executed (Partial or Full fill against a hidden/dark order)
struct ITCH5_OrderExecuted {
    char msgType;           // 'E'
    uint16_t stockLocate;
    uint16_t trackingNum;
    uint8_t timestamp[6];
    uint64_t orderRefNum;
    uint32_t executedShares;
    uint64_t matchNumber;
};

// 'X' - Order Cancel (Partial reduction of shares)
struct ITCH5_CancelOrder
{
    char msgType;           // 'X'
    uint16_t stockLocate;
    uint16_t trackingNum;
    uint8_t timestamp[6];
    uint64_t orderRefNum;
    uint32_t canceledShares;  // num of shares being removed from the display size of the order as a result of cancellation
};

// 'D' - Order Delete (Complete removal of the order)
struct ITCH5_OrderDelete {
    char msgType;           // 'D'
    uint16_t stockLocate;
    uint16_t trackingNum;
    uint8_t timestamp[6];
    uint64_t orderRefNum;
};

struct ITCH5_OrderReplace {
    char msgType;           // 'U'
    uint16_t stockLocate;
    uint16_t trackingNum;
    uint8_t timestamp[6];
    uint64_t originalOrderRefNum;
    uint64_t newOrderRefNum;
    uint32_t shares;
    uint32_t price;
};

struct ITCH5_AddOrderMPID {
    char msgType;           // 'F'
    uint16_t stockLocate;
    uint16_t trackingNum;
    uint8_t timestamp[6];
    uint64_t orderRefNum;
    char side;
    uint32_t shares;
    char stock[8];
    uint32_t price;
    char mpid[4];           // The Market Maker ID
};

struct ITCH5_OrderExecutedWithPrice {
    char msgType;           // 'C'
    uint16_t stockLocate;
    uint16_t trackingNum;
    uint8_t timestamp[6];
    uint64_t orderRefNum;
    uint32_t executedShares;
    uint64_t matchNumber;
    char printable;
    uint32_t executionPrice;
};
#pragma pack(pop)