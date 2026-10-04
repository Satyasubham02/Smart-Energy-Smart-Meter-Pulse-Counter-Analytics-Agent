#pragma once
#include <atomic>
#include <string>
#include "smeter/common.hpp"
#include "smeter/ring_buffer.hpp"

namespace sm {

using PulseQueue = SpscRing<Pulse, 4096>;

/// Hardware abstraction layer: anything that can produce timestamped pulses.
/// Implementations: GpioCdevSource (real /dev/gpiochipN) and SimSource (software meter).
class PulseSource {
public:
    virtual ~PulseSource() = default;
    virtual bool start(PulseQueue& q) = 0;
    virtual void stop() = 0;
    virtual std::string name() const = 0;
    /// Clock in the same domain as Pulse::ts_ns (virtual for the simulator).
    virtual uint64_t now_ns() const { return mono_ns(); }
    /// Wall-clock epoch seconds corresponding to now_ns() at start (for tariff hour-of-day).
    virtual double wall_start_s() const { return static_cast<double>(std::time(nullptr)); }
    std::atomic<bool> failed{false};  // set by the source thread on unrecoverable I/O error
};

}  // namespace sm
