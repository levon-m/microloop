#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Lock-free Single Producer Single Consumer (SPSC) Ring Buffer
 *
 * REAL-TIME SAFE: This queue is designed for real-time audio/MIDI applications.
 *
 * KEY PROPERTIES:
 * - Lock-free: No mutexes, no blocking, no priority inversion
 * - Wait-free: Both producer and consumer complete in bounded time
 * - Cache-friendly: Uses power-of-2 size for fast modulo (bit masking)
 * - POD only: Works with Plain Old Data types (no constructors/destructors)
 *
 * HOW IT WORKS:
 * - Two indices: writeIdx (producer) and readIdx (consumer)
 * - Producer only writes to writeIdx, consumer only writes to readIdx
 * - Both can READ each other's index (volatile ensures memory visibility)
 * - No data races because only one thread writes to each index
 *
 * PERFORMANCE:
 * - Push/Pop: O(1) constant time
 * - No dynamic allocation after construction
 * - Uses bitwise AND instead of modulo: (index & (SIZE-1)) vs (index % SIZE)
 *   Why? AND is single CPU cycle, modulo can be dozens of cycles
 *
 * LIMITATIONS:
 * - SIZE must be power of 2 (enforced at compile time)
 * - Only POD types (no std::string, no complex objects)
 * - Single producer, single consumer only (NOT thread-safe for multiple producers/consumers)
 *
 * TYPICAL USE:
 * - ISR → Thread: Audio samples, MIDI clock ticks
 * - Thread → ISR: Control commands
 * - Thread → Thread: Events, processed data
 *
 * @tparam T Element type (must be POD: Plain Old Data)
 * @tparam SIZE Number of elements (MUST be power of 2: 16, 32, 64, 128, 256, etc.)
 */
template<typename T, size_t SIZE>
class SPSCQueue {
    // Compile-time check: SIZE must be power of 2
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
    // Compile-time check: SIZE must be > 0
    static_assert(SIZE > 0, "SIZE must be greater than 0");

public:
    SPSCQueue() : writeIdx(0), readIdx(0) {}

    /**
     * @brief Push an element to the queue (PRODUCER side)
     *
     * REAL-TIME SAFETY:
     * - Constant time O(1) - no loops, no allocation
     * - Returns immediately if full (no blocking)
     * - Safe to call from ISR or real-time thread
     *
     * @param item The item to push (copied by value)
     * @return true if pushed successfully, false if queue is full
     */
    bool push(const T& item) {
        const uint32_t current_write = writeIdx;  // Atomic read (volatile)
        const uint32_t next_write = current_write + 1;

        // Check if full: next write position would collide with read position
        // We sacrifice one slot to distinguish full from empty:
        // - Empty: readIdx == writeIdx
        // - Full: (writeIdx + 1) == readIdx (after masking)
        if ((next_write & (SIZE - 1)) == (readIdx & (SIZE - 1))) {
            return false;  // Queue full
        }

        // Write data at current position
        buffer[current_write & (SIZE - 1)] = item;

        // Update write index (this makes the item visible to consumer)
        // IMPORTANT: This must happen AFTER writing data (memory ordering)
        writeIdx = next_write;

        return true;
    }

    /**
     * @brief Pop an element from the queue (CONSUMER side)
     *
     * REAL-TIME SAFETY:
     * - Constant time O(1)
     * - Returns immediately if empty
     * - Safe to call from ISR or real-time thread
     *
     * @param item Output parameter to store the popped item
     * @return true if popped successfully, false if queue is empty
     */
    bool pop(T& item) {
        const uint32_t current_read = readIdx;   // Atomic read (volatile)

        // Check if empty: read position caught up with write position
        if (current_read == writeIdx) {
            return false;  // Queue empty
        }

        // Read data at current position
        item = buffer[current_read & (SIZE - 1)];

        // Update read index (this frees the slot for producer)
        // IMPORTANT: This must happen AFTER reading data (memory ordering)
        readIdx = current_read + 1;

        return true;
    }

    /**
     * @brief Check if queue is empty
     * @return true if empty (consumer perspective)
     */
    bool isEmpty() const {
        return readIdx == writeIdx;
    }

    /**
     * @brief Check if queue is full
     * @return true if full (producer perspective)
     */
    bool isFull() const {
        const uint32_t next_write = writeIdx + 1;
        return (next_write & (SIZE - 1)) == (readIdx & (SIZE - 1));
    }

    /**
     * @brief Get approximate number of elements in queue
     *
     * WARNING: This is a snapshot and may be stale by the time you use it.
     * The actual count can change between reading writeIdx and readIdx.
     * Use this for debugging/monitoring only, NOT for control flow.
     *
     * @return Approximate number of elements
     */
    size_t size() const {
        const uint32_t write = writeIdx;
        const uint32_t read = readIdx;
        // Handle wraparound by using unsigned arithmetic
        return (write - read) & (SIZE - 1);
    }

    /**
     * @brief Get queue capacity (maximum elements that can be stored)
     * Note: Actual capacity is SIZE - 1 due to full/empty distinction
     */
    static constexpr size_t capacity() {
        return SIZE - 1;
    }

private:
    // Data buffer (static allocation, no heap)
    T buffer[SIZE];

    // Indices are volatile to ensure visibility across threads/ISRs
    // Producer only writes writeIdx, consumer only writes readIdx
    // Both can read the other's index safely (no data race)
    volatile uint32_t writeIdx;  // Next position to write (producer)
    volatile uint32_t readIdx;   // Next position to read (consumer)

    // Note: We could add padding here to prevent false sharing on multi-core systems
    // False sharing: writeIdx and readIdx on same cache line causes cache ping-pong
    // For Teensy 4.x (single core), this isn't necessary, but good practice for portability
    // Example: alignas(64) volatile uint32_t writeIdx; // 64-byte cache line alignment
};

// Type aliases for common MIDI/Audio use cases

// MIDI events: 32 slots, ~1.3ms buffer at 120 BPM (24 ticks/beat)
template<typename T>
using SmallSPSC = SPSCQueue<T, 32>;

// MIDI clocks: 256 slots, ~5 seconds at 120 BPM
template<typename T>
using MediumSPSC = SPSCQueue<T, 256>;

// Audio buffers: 1024 slots, chunky but good for sample streaming
template<typename T>
using LargeSPSC = SPSCQueue<T, 1024>;
