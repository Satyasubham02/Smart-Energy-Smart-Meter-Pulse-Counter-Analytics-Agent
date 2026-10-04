#pragma once
#include <cstdint>
#include <ctime>
namespace sm {

/// Monotonic clock in nanoseconds (same clock the kernel GPIO driver timestamps with).
inline uint64_t mono_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

/// One metering pulse (one LED blink / S0 contact closure) with a kernel-grade timestamp.
struct Pulse {
    uint64_t ts_ns;
};

}  // namespace sm
