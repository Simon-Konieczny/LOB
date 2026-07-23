//
// Created by Simon Konieczny on 25/05/2026.
//
#pragma once

#ifndef LOB_OFICALCULATOR_HPP
#define LOB_OFICALCULATOR_HPP

#include "IBookObserver.hpp"
#include "OrderBook.hpp"
#include <deque>
#include <mutex>

#endif

class OFICalculator : public IBookObserver
{
public:
    struct OFIEvent
    {
        uint64_t timestamp;
        int64_t value;
    };

    explicit OFICalculator() = default;

    [[nodiscard]] int64_t getOFI_1s() const { return rolling_1s_; }
    [[nodiscard]] int64_t getOFI_5s() const { return rolling_5s_; }
    [[nodiscard]] int64_t getOFI_30s() const { return rolling_30s_; }

    void onBookUpdate(const BookUpdate& update) override;
private:
    void pruneOldEvents(uint64_t now);

    std::deque<OFIEvent> history_1s_;
    std::deque<OFIEvent> history_5s_;
    std::deque<OFIEvent> history_30s_;

    // running sums
    int64_t rolling_1s_ = 0;
    int64_t rolling_5s_ = 0;
    int64_t rolling_30s_ = 0;

    // state tracking for t-1
    bool is_initialized_ = false;
    int64_t prev_bid_price_ = 0;
    uint32_t prev_bid_vol_ = 0;
    int64_t prev_ask_price_ = 0;
    uint32_t prev_ask_vol_ = 0;
};