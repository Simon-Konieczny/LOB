//
// Created by Simon Konieczny on 20/02/2026.
//

#pragma once
#include <atomic>
#include <vector>

template<typename T, size_t Capacity>
class SPSCQueue {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    SPSCQueue() : head(0), tail(0) {
        buffer.resize(Capacity);
    }

    bool push(const T& item) {
        const size_t currentTail = tail.load(std::memory_order_relaxed);
        const size_t nextTail = (currentTail + 1) & (Capacity - 1);

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
        head.store((currentHead + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

private:
    std::vector<T> buffer;
    alignas(64) std::atomic<size_t> head;
    alignas(64) std::atomic<size_t> tail;
};