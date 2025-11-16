#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Lightweight non-owning view of a contiguous sequence of elements
 *
 * REAL-TIME SAFE: This is a zero-overhead abstraction over raw pointers.
 *
 * WHY USE SPAN?
 * - Bounds safety: Prevents out-of-bounds access (especially in debug builds)
 * - API clarity: Function signature shows "I need a buffer of N elements", not "T* and size_t"
 * - No ownership: Doesn't allocate or free, just views existing memory
 * - Zero overhead: In release builds, compiles to same asm as raw pointers
 *
 * COMPARED TO:
 * - Raw pointers (T*): No size info, easy to over-run buffer
 * - std::vector: Owns memory, can allocate (NOT real-time safe)
 * - std::array: Fixed size at compile time (too rigid)
 * - std::span (C++20): We're on C++17, so we roll our own
 *
 * TYPICAL USE CASES:
 * - Audio processing: processBlock(Span<float> inputL, Span<float> inputR)
 * - MIDI parsing: parseSysEx(Span<const uint8_t> data)
 * - Buffer operations: copyBuffer(Span<const float> src, Span<float> dst)
 *
 * PERFORMANCE:
 * - sizeof(Span) == sizeof(T*) + sizeof(size_t) (usually 8-16 bytes)
 * - No vtable, no inheritance overhead
 * - Inlined methods, zero function call overhead
 * - Compiler can optimize away bounds checks in release builds
 *
 * @tparam T Element type (can be const for read-only views)
 */
template<typename T>
class Span {
public:
    // Type aliases (standard library compatibility)
    using element_type = T;
    using value_type = typename std::remove_cv<T>::type;
    using size_type = size_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;

    /**
     * @brief Default constructor - empty span
     */
    constexpr Span() noexcept : data_(nullptr), size_(0) {}

    /**
     * @brief Construct from pointer and size
     *
     * REAL-TIME SAFE: No allocation, just stores pointer and size
     *
     * @param ptr Pointer to first element
     * @param count Number of elements
     */
    constexpr Span(T* ptr, size_type count) noexcept
        : data_(ptr), size_(count) {}

    /**
     * @brief Construct from C-style array
     *
     * EXAMPLE:
     *   float buffer[128];
     *   Span<float> span(buffer);  // Automatically deduces size = 128
     */
    template<size_t N>
    constexpr Span(T (&arr)[N]) noexcept
        : data_(arr), size_(N) {}

    /**
     * @brief Get pointer to first element
     */
    constexpr pointer data() const noexcept {
        return data_;
    }

    /**
     * @brief Get number of elements
     */
    constexpr size_type size() const noexcept {
        return size_;
    }

    /**
     * @brief Get size in bytes
     */
    constexpr size_type size_bytes() const noexcept {
        return size_ * sizeof(T);
    }

    /**
     * @brief Check if span is empty
     */
    constexpr bool empty() const noexcept {
        return size_ == 0;
    }

    /**
     * @brief Access element at index (with bounds check in debug)
     *
     * REAL-TIME TRADEOFF:
     * - Debug builds: Check bounds, crash if out-of-range (catches bugs early)
     * - Release builds: No check, just like raw pointer (maximum performance)
     *
     * WHY? Bounds checks are ~1-3 instructions. In an audio callback processing
     * 128 samples at 48kHz (2.6ms budget), every cycle counts. But in debug,
     * we want to catch buffer overruns immediately.
     *
     * @param idx Index of element
     * @return Reference to element
     */
    constexpr reference operator[](size_type idx) const noexcept {
        // Assert in debug builds, no-op in release
        #ifndef NDEBUG
        if (idx >= size_) {
            // On Teensy, this will trigger breakpoint in debugger or hang
            __builtin_trap();
        }
        #endif
        return data_[idx];
    }

    /**
     * @brief Get first element
     * WARNING: Undefined behavior if span is empty
     */
    constexpr reference front() const noexcept {
        return data_[0];
    }

    /**
     * @brief Get last element
     * WARNING: Undefined behavior if span is empty
     */
    constexpr reference back() const noexcept {
        return data_[size_ - 1];
    }

    /**
     * @brief Get subspan starting at offset with count elements
     *
     * EXAMPLE:
     *   float buffer[128];
     *   Span<float> full(buffer);
     *   Span<float> left = full.subspan(0, 64);   // First 64 samples
     *   Span<float> right = full.subspan(64, 64); // Last 64 samples
     *
     * @param offset Starting index
     * @param count Number of elements (if -1, take all remaining)
     * @return Subspan view
     */
    constexpr Span<T> subspan(size_type offset, size_type count = size_type(-1)) const noexcept {
        #ifndef NDEBUG
        if (offset > size_) {
            __builtin_trap();
        }
        #endif

        size_type actual_count = count;
        if (count == size_type(-1) || offset + count > size_) {
            actual_count = size_ - offset;
        }

        return Span<T>(data_ + offset, actual_count);
    }

    /**
     * @brief Get first N elements
     */
    constexpr Span<T> first(size_type count) const noexcept {
        return subspan(0, count);
    }

    /**
     * @brief Get last N elements
     */
    constexpr Span<T> last(size_type count) const noexcept {
        return subspan(size_ - count, count);
    }

    // Iterator support (for range-based for loops)
    constexpr pointer begin() const noexcept { return data_; }
    constexpr pointer end() const noexcept { return data_ + size_; }

private:
    T* data_;
    size_type size_;
};

/**
 * @brief Helper function to create a Span from a C-array
 *
 * EXAMPLE:
 *   float buffer[128];
 *   auto span = makeSpan(buffer);  // Deduces Span<float> with size 128
 */
template<typename T, size_t N>
constexpr Span<T> makeSpan(T (&arr)[N]) noexcept {
    return Span<T>(arr, N);
}

/**
 * @brief Helper function to create a Span from pointer and size
 */
template<typename T>
constexpr Span<T> makeSpan(T* ptr, size_t count) noexcept {
    return Span<T>(ptr, count);
}

/**
 * @brief Create a const Span (read-only view)
 *
 * EXAMPLE:
 *   float buffer[128];
 *   Span<const float> readOnly = makeConstSpan(buffer);
 *   // readOnly[0] = 1.0f;  // Compile error! Can't modify through const Span
 */
template<typename T, size_t N>
constexpr Span<const T> makeConstSpan(const T (&arr)[N]) noexcept {
    return Span<const T>(arr, N);
}

template<typename T>
constexpr Span<const T> makeConstSpan(const T* ptr, size_t count) noexcept {
    return Span<const T>(ptr, count);
}

// Type aliases for common audio/MIDI buffers

// Audio block: 128 samples (Teensy Audio Library default)
using AudioBuffer = Span<float>;
using ConstAudioBuffer = Span<const float>;

// MIDI message buffer
using MidiBuffer = Span<uint8_t>;
using ConstMidiBuffer = Span<const uint8_t>;
