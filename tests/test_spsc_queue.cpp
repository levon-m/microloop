/**
 * test_spsc_queue.cpp - Unit tests for SPSC queue
 */

#include "test_runner.h"
#include "spsc_queue.h"

TEST(SPSCQueue_Empty_InitiallyTrue) {
    SPSCQueue<int, 16> queue;
    ASSERT_TRUE(queue.empty());
    ASSERT_EQ(queue.size(), 0);
}

TEST(SPSCQueue_PushPop_BasicOperation) {
    SPSCQueue<int, 16> queue;

    ASSERT_TRUE(queue.push(42));
    ASSERT_FALSE(queue.empty());
    ASSERT_EQ(queue.size(), 1);

    int value;
    ASSERT_TRUE(queue.pop(value));
    ASSERT_EQ(value, 42);
    ASSERT_TRUE(queue.empty());
}

TEST(SPSCQueue_MultiplePushPop_MaintainsOrder) {
    SPSCQueue<int, 16> queue;

    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(queue.push(i));
    }

    ASSERT_EQ(queue.size(), 10);

    for (int i = 0; i < 10; i++) {
        int value;
        ASSERT_TRUE(queue.pop(value));
        ASSERT_EQ(value, i);
    }

    ASSERT_TRUE(queue.empty());
}

TEST(SPSCQueue_Full_RejectsPush) {
    SPSCQueue<int, 4> queue;  // Small queue (power of 2)

    // Fill queue (size 4 means 3 usable slots due to full/empty distinction)
    ASSERT_TRUE(queue.push(1));
    ASSERT_TRUE(queue.push(2));
    ASSERT_TRUE(queue.push(3));

    // Next push should fail (queue full)
    ASSERT_FALSE(queue.push(4));
}

TEST(SPSCQueue_PopEmpty_ReturnsFalse) {
    SPSCQueue<int, 16> queue;

    int value;
    ASSERT_FALSE(queue.pop(value));  // Empty queue
}

TEST(SPSCQueue_Wraparound_HandlesCorrectly) {
    SPSCQueue<int, 8> queue;

    // Fill and drain multiple times to test wraparound
    for (int cycle = 0; cycle < 5; cycle++) {
        // Fill
        for (int i = 0; i < 7; i++) {
            ASSERT_TRUE(queue.push(cycle * 100 + i));
        }

        // Drain
        for (int i = 0; i < 7; i++) {
            int value;
            ASSERT_TRUE(queue.pop(value));
            ASSERT_EQ(value, cycle * 100 + i);
        }

        ASSERT_TRUE(queue.empty());
    }
}

TEST(SPSCQueue_Struct_WorksWithPOD) {
    struct TestStruct {
        uint32_t timestamp;
        uint16_t id;
        uint16_t value;
    };

    SPSCQueue<TestStruct, 16> queue;

    TestStruct data1 = {1000, 10, 42};
    ASSERT_TRUE(queue.push(data1));

    TestStruct data2;
    ASSERT_TRUE(queue.pop(data2));

    ASSERT_EQ(data2.timestamp, 1000U);
    ASSERT_EQ(data2.id, 10);
    ASSERT_EQ(data2.value, 42);
}

TEST(SPSCQueue_Size_AccurateAfterOperations) {
    SPSCQueue<int, 16> queue;

    ASSERT_EQ(queue.size(), 0);

    queue.push(1);
    ASSERT_EQ(queue.size(), 1);

    queue.push(2);
    queue.push(3);
    ASSERT_EQ(queue.size(), 3);

    int dummy;
    queue.pop(dummy);
    ASSERT_EQ(queue.size(), 2);

    queue.pop(dummy);
    queue.pop(dummy);
    ASSERT_EQ(queue.size(), 0);
}

TEST(SPSCQueue_Performance_BurstPushPop) {
    SPSCQueue<uint32_t, 256> queue;

    uint32_t start = micros();

    // Push 200 items
    for (uint32_t i = 0; i < 200; i++) {
        queue.push(i);
    }

    // Pop 200 items
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t value;
        queue.pop(value);
    }

    uint32_t duration = micros() - start;

    Serial.print("\n400 queue operations (200 push + 200 pop) took ");
    Serial.print(duration);
    Serial.println(" µs");

    // Should be very fast (< 1ms)
    ASSERT_LT(duration, 1000U);
}
