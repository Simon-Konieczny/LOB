//
// Created by Simon Konieczny on 20/02/2026.
//

#pragma once
#include <atomic>
#include <vector>
#include <bit>
#include <stdexcept>

template<typename T>
class SPSCQueue {
public:
    SPSCQueue(size_t capacity) : head(0), tail(0), capacity_mask(capacity-1) {
        if (!std::has_single_bit(capacity))
        {
            throw std::invalid_argument("Capacity must be a power of 2");
        }
        buffer.resize(capacity);
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    bool push(const T& item) {
        const size_t currentTail = tail.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & capacity_mask;

        if (nextTail == head.load(std::memory_order_acquire)) {
            return false;
        }

        buffer[currentTail] = item;
        tail.store(nextTail, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const size_t currentHead = head.load(std::memory_order_relaxed);
        if (currentHead == tail.load(std::memory_order_acquire)) {
            return false;
        }

        item = buffer[currentHead];
        head.store((currentHead + 1) & capacity_mask, std::memory_order_release);
        return true;
    }

private:
    std::vector<T> buffer;

    const size_t capacity_mask;
    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;
};