#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace sm {

/// Lock-free single-producer / single-consumer ring buffer.
/// Producer = pulse-source (ISR-like) thread, consumer = analytics thread.
/// N must be a power of two.
template <typename T, std::size_t N>
class SpscRing {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    bool push(const T& v) {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);
        if (h - t == N) {  // full -> drop, never block the producer
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        buf_[h & (N - 1)] = v;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    std::optional<T> pop() {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);
        if (t == h) return std::nullopt;
        T v = buf_[t & (N - 1)];
        tail_.store(t + 1, std::memory_order_release);
        return v;
    }
    std::size_t size() const { return head_.load() - tail_.load(); }
    std::size_t dropped() const { return dropped_.load(); }

private:
    std::array<T, N> buf_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::atomic<std::size_t> dropped_{0};
};

}  // namespace sm
