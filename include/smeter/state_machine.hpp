#pragma once
#include <string>

namespace sm {

enum class State { Init, Idle, Measuring, Overload, Fault };

inline const char* to_string(State s) {
    switch (s) {
        case State::Init: return "INIT";
        case State::Idle: return "IDLE";
        case State::Measuring: return "MEASURING";
        case State::Overload: return "OVERLOAD";
        case State::Fault: return "FAULT";
    }
    return "?";
}

/// Meter life-cycle state machine (see docs/ARCHITECTURE.md for the diagram).
class StateMachine {
public:
    explicit StateMachine(double overload_w, double hysteresis = 0.9)
        : limit_(overload_w), hyst_(hysteresis) {}

    State state() const { return s_; }

    /// Returns true if the state changed.
    bool on_start() { return go(s_ == State::Init ? State::Idle : s_); }
    bool on_power(double w) {
        switch (s_) {
            case State::Idle:      return go(w > limit_ ? State::Overload : State::Measuring);
            case State::Measuring: return w > limit_ ? go(State::Overload) : false;
            case State::Overload:  return w < limit_ * hyst_ ? go(State::Measuring) : false;
            default: return false;  // Init / Fault ignore samples
        }
    }
    bool on_idle_timeout() {
        return (s_ == State::Measuring || s_ == State::Overload) ? go(State::Idle) : false;
    }
    bool on_source_error() { return s_ != State::Fault ? go(State::Fault) : false; }
    bool on_recover() { return s_ == State::Fault ? go(State::Idle) : false; }

private:
    bool go(State n) { bool ch = n != s_; s_ = n; return ch; }
    State s_ = State::Init;
    double limit_, hyst_;
};

}  // namespace sm
