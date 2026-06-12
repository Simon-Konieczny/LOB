//
// Created by Simon Konieczny on 24/05/2026.
//
#ifdef __APPLE__
#include <stddef.h>
typedef size_t rsize_t;
#endif

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <numeric>

#include "../../src/SPSCQueue.hpp"

// --- 1. INITIALIZATION TESTS ---

TEST(SPSCQueueTest, InitializationSuccessOnPowerOfTwo) {
    EXPECT_NO_THROW(SPSCQueue<int> q1(2));
    EXPECT_NO_THROW(SPSCQueue<int> q2(1024));
    EXPECT_NO_THROW(SPSCQueue<int> q3(65536));
}

TEST(SPSCQueueTest, InitializationThrowsOnNonPowerOfTwo) {
    EXPECT_THROW(SPSCQueue<int> q1(3), std::invalid_argument);
    EXPECT_THROW(SPSCQueue<int> q2(100), std::invalid_argument);
    EXPECT_THROW(SPSCQueue<int> q3(1023), std::invalid_argument);
}

// --- 2. SINGLE-THREADED BEHAVIOR TESTS ---

TEST(SPSCQueueTest, EmptyQueueReturnsFalseOnPop) {
    SPSCQueue<int> q(8);
    int item = -1;
    EXPECT_FALSE(q.pop(item));
    EXPECT_EQ(item, -1); // Value should remain unchanged
}

TEST(SPSCQueueTest, PushAndPopMaintainFIFO) {
    SPSCQueue<int> q(8);

    EXPECT_TRUE(q.push(10));
    EXPECT_TRUE(q.push(20));
    EXPECT_TRUE(q.push(30));

    int item;
    EXPECT_TRUE(q.pop(item));
    EXPECT_EQ(item, 10);

    EXPECT_TRUE(q.pop(item));
    EXPECT_EQ(item, 20);

    EXPECT_TRUE(q.pop(item));
    EXPECT_EQ(item, 30);

    // Queue should now be empty
    EXPECT_FALSE(q.pop(item));
}

TEST(SPSCQueueTest, FullQueueReturnsFalseOnPush) {
    // Note: A standard ring buffer with this implementation uses 1 slot
    // to distinguish full from empty. Effective capacity is (capacity - 1).
    const size_t capacity = 4;
    SPSCQueue<int> q(capacity);

    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));

    // The 4th push should fail (nextTail == head)
    EXPECT_FALSE(q.push(4));
}

TEST(SPSCQueueTest, WrapAroundLogic) {
    SPSCQueue<int> q(4); // Effective capacity: 3

    // Fill the queue
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));

    int item;

    // Pop one, freeing up a slot and moving the head forward
    EXPECT_TRUE(q.pop(item));
    EXPECT_EQ(item, 1);

    // Push another, moving the tail forward (wrapping around the capacity_mask)
    EXPECT_TRUE(q.push(4));
    EXPECT_FALSE(q.push(5)); // Should be full again

    // Verify remaining contents
    EXPECT_TRUE(q.pop(item)); EXPECT_EQ(item, 2);
    EXPECT_TRUE(q.pop(item)); EXPECT_EQ(item, 3);
    EXPECT_TRUE(q.pop(item)); EXPECT_EQ(item, 4);
    EXPECT_FALSE(q.pop(item)); // Empty
}

TEST(SPSCQueueTest, WorksWithComplexTypes) {
    SPSCQueue<std::string> q(8);

    EXPECT_TRUE(q.push("Hello"));
    EXPECT_TRUE(q.push("World"));

    std::string item;
    EXPECT_TRUE(q.pop(item));
    EXPECT_EQ(item, "Hello");
    EXPECT_TRUE(q.pop(item));
    EXPECT_EQ(item, "World");
}

// --- 3. CONCURRENCY TESTS ---

TEST(SPSCQueueTest, ConcurrentProducerConsumer) {
    const size_t capacity = 1024;
    const int num_messages = 1000000; // 1 Million messages to stress test memory ordering

    SPSCQueue<int> q(capacity);
    std::vector<int> consumed_data;
    consumed_data.reserve(num_messages);

    // Producer Thread
    std::thread producer([&]() {
        for (int i = 0; i < num_messages; ++i) {
            // Spin until the push is successful (queue not full)
            while (!q.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    // Consumer Thread
    std::thread consumer([&]() {
        for (int i = 0; i < num_messages; ++i) {
            int item;
            // Spin until the pop is successful (queue not empty)
            while (!q.pop(item)) {
                std::this_thread::yield();
            }
            consumed_data.push_back(item);
        }
    });

    producer.join();
    consumer.join();

    // Verification
    ASSERT_EQ(consumed_data.size(), num_messages);
    for (int i = 0; i < num_messages; ++i) {
        // If data races occurred, we would see dropped, duplicated, or out-of-order numbers
        ASSERT_EQ(consumed_data[i], i) << "Data race detected at index " << i;
    }
}