//
// Created by Simon Konieczny on 25/05/2026.
//

#include "OFICalculator.hpp"

void OFICalculator::runOfiCalculator()
{
    size_t updateCount = 0;
    BookUpdate update = {};

    while (true)
    {
        if (bookUpdateQueue_.pop(update))
        {
            onBookUpdate(update);
            updateCount++;
        } else
        {
            if (producerDone_.load(std::memory_order_acquire))
            {
                if (!bookUpdateQueue_.pop(update)) break;
            }
            #if defined(__aarch64__) || defined(__arm__)
                        __asm__ volatile("yield" ::: "memory");
            #else
                        __asm__ volatile("pause" ::: "memory");
            #endif
        }
    }
}

void OFICalculator::runTradeCapture()
{
    size_t tradeCount = 0;
    TradeRecord tradeRecord = {};

    while (true)
    {
        if (tradeRecordQueue_.pop(tradeRecord))
        {
            onTrade(tradeRecord);
            tradeCount++;
        } else
        {
            if (producerDone_.load(std::memory_order_acquire))
            {
                if (!tradeRecordQueue_.pop(tradeRecord)) break;
            }
            #if defined(__aarch64__) || defined(__arm__)
                        __asm__ volatile("yield" ::: "memory");
            #else
                        __asm__ volatile("pause" ::: "memory");
            #endif
        }
    }
}

void OFICalculator::onBookUpdate(const BookUpdate& update)
{
    // Handle initial state
    if (!is_initialized_)
    {
        prev_bid_price_ = update.bidPrice;
        prev_bid_vol_ = update.bidVol;
        prev_ask_price_ = update.askPrice;
        prev_ask_vol_ = update.askVol;
        is_initialized_ = true;
        return;
    }

    // Bid-side change (Demand)
    int64_t dW = 0;
    if (update.bidPrice > prev_bid_price_) {
        dW = update.bidVol;
    } else if (update.bidPrice == prev_bid_price_) {
        dW = static_cast<int64_t>(update.bidVol) - prev_bid_vol_;
    } else {
        dW = -static_cast<int64_t>(prev_bid_vol_);
    }

    // Ask-side change (Supply)
    int64_t dV = 0;
    if (update.askPrice < prev_ask_price_) {
        dV = update.askVol;
    } else if (update.askPrice == prev_ask_price_) {
        dV = static_cast<int64_t>(update.askVol) - prev_ask_vol_;
    } else {
        dV = -static_cast<int64_t>(prev_ask_vol_);
    }

    // Compute Current OFI
    int64_t current_ofi = dW - dV;

    // Update state for t+1
    prev_bid_price_ = update.bidPrice;
    prev_bid_vol_ = update.bidVol;
    prev_ask_price_ = update.askPrice;
    prev_ask_vol_ = update.askVol;

    // Store and aggregate
    if (current_ofi != 0)
    {
        const OFIEvent ev = {update.timestamp, current_ofi};

        history_1s_.push_back(ev);
        history_5s_.push_back(ev);
        history_30s_.push_back(ev);

        rolling_1s_ += current_ofi;
        rolling_5s_ += current_ofi;
        rolling_30s_ += current_ofi;
    }

    // Clean up old data to maintain windows
    pruneOldEvents(update.timestamp);
}

// now is the passed timestamp of the event
void OFICalculator::pruneOldEvents(uint64_t now)
{
    while (!history_1s_.empty() && (now - history_1s_.front().timestamp) > 1'000'000'000) {
        rolling_1s_ -= history_1s_.front().value;
        history_1s_.pop_front();
    }
    while (!history_5s_.empty() && (now - history_5s_.front().timestamp) > 5'000'000'000) {
        rolling_5s_ -= history_5s_.front().value;
        history_5s_.pop_front();
    }
    while (!history_30s_.empty() && (now - history_30s_.front().timestamp) > 30'000'000'000) {
        rolling_30s_ -= history_30s_.front().value;
        history_30s_.pop_front();
    }
}

void OFICalculator::onTrade(const TradeRecord& tradeRecord)
{
    recentTrades.push_front(tradeRecord);
    if (recentTrades.size() > 5) recentTrades.pop_back();
}
