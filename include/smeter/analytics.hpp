#pragma once
#include <cmath>
#include <ctime>
#include <deque>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "smeter/common.hpp"
#include "smeter/state_machine.hpp"

namespace sm {

struct Config {
    double imp_per_kwh = 1000.0;   // meter constant (pulses per kWh), printed on the meter
    double debounce_ms = 20.0;     // intervals shorter than this are contact bounce
    double overload_w = 5000.0;    // sanctioned-load limit
    double idle_timeout_s = 300.0; // no pulse for this long -> IDLE (0 W)
    double ewma_alpha = 0.3;       // smoothing for power
    double spike_sigma = 3.0;      // anomaly: mean + k*sigma
    std::size_t window = 50;       // anomaly window (pulses)
    // Time-of-use tariff, currency units per kWh.
    double rate_offpeak = 4.0;     // 22:00-06:00
    double rate_normal = 6.0;      // 06:00-18:00
    double rate_peak = 8.5;        // 18:00-22:00
};

/// One finished (or in-progress) one-minute aggregate.
struct Bucket {
    long minute = 0;      // wall-clock minutes since epoch
    double wh = 0, peak_w = 0, cost = 0;
    uint32_t pulses = 0;
    double avg_w() const { return wh * 60.0; }
};

struct Snapshot {
    State state = State::Init;
    std::string source;
    uint64_t pulses = 0, debounced = 0, out_of_order = 0, spikes = 0;
    double energy_kwh = 0, power_w = 0, smooth_w = 0, peak_w = 0, cost = 0, rate = 0;
    double wall_s = 0, overload_w = 0;
    std::vector<std::string> events;
};

/// Pure, thread-safe analytics core (no I/O) -> easy to unit test.
class Analytics {
public:
    using BucketSink = std::function<void(const Bucket&)>;

    Analytics(Config c, double wall0_s, uint64_t ts0_ns)
        : cfg_(c), sm_(c.overload_w), wall0_(wall0_s), ts0_(ts0_ns) {}

    void set_sink(BucketSink s) { sink_ = std::move(s); }
    /// Optional: called for every accepted pulse with (wall-clock seconds, Wh). Feeds the usage tracker.
    using EnergySink = std::function<void(double, double)>;
    void set_energy_sink(EnergySink s) { energy_sink_ = std::move(s); }
    void set_source_name(std::string n) { std::lock_guard<std::mutex> l(m_); src_ = std::move(n); }
    void restore(uint64_t pulses, double energy_wh, double cost) {
        std::lock_guard<std::mutex> l(m_);
        pulses_ = pulses; energy_wh_ = energy_wh; cost_ = cost;
    }
    void start() { std::lock_guard<std::mutex> l(m_); note(sm_.on_start()); }
    void log_event(const std::string& e) { std::lock_guard<std::mutex> l(m_); push_event(e); }

    /// Restart the meter: clears counters, history, events and state; re-bases the clocks.
    void reset(double wall0_s, uint64_t ts0_ns) {
        std::lock_guard<std::mutex> l(m_);
        sm_ = StateMachine(cfg_.overload_w);
        wall0_ = wall0_s; ts0_ = ts0_ns; now_ts_ = 0; last_ts_ = 0;
        have_last_ = have_ewma_ = false;
        pulses_ = debounced_ = out_of_order_ = spikes_ = 0;
        energy_wh_ = cost_ = inst_w_ = smooth_w_ = peak_w_ = 0;
        win_.clear(); events_.clear(); hist_.clear(); cur_ = Bucket{};
        push_event("counters reset by user");
    }

    void on_pulse(uint64_t ts) {
        std::lock_guard<std::mutex> l(m_);
        if (have_last_ && ts < last_ts_) { ++out_of_order_; return; }
        if (sm_.state() == State::Fault) note(sm_.on_recover());
        if (have_last_) {
            double dt = (ts - last_ts_) / 1e9;
            if (dt * 1000.0 < cfg_.debounce_ms) { ++debounced_; return; }
            double p = power_from_interval(dt);
            inst_w_ = p;
            smooth_w_ = have_ewma_ ? cfg_.ewma_alpha * p + (1 - cfg_.ewma_alpha) * smooth_w_ : p;
            have_ewma_ = true;
            peak_w_ = std::max(peak_w_, p);
            detect_spike(p, ts);
            if (sm_.on_power(p)) note(true, "power=" + fmt(p) + "W");
        }
        have_last_ = true;
        last_ts_ = ts;
        ++pulses_;
        const double wh = 1000.0 / cfg_.imp_per_kwh;
        const double wall = wall_of(ts);
        const double c = wh / 1000.0 * rate_at(wall);
        energy_wh_ += wh;
        if (energy_sink_) energy_sink_(wall, wh);
        cost_ += c;
        add_to_bucket(wall, wh, c);
    }

    /// Call periodically with the source clock: handles decay + idle timeout + minute roll-over.
    void tick(uint64_t now_ts) {
        std::lock_guard<std::mutex> l(m_);
        now_ts_ = now_ts;
        if (have_last_ && now_ts > last_ts_) {
            double since = (now_ts - last_ts_) / 1e9;
            if (since > cfg_.idle_timeout_s) {
                inst_w_ = smooth_w_ = 0;
                if (sm_.on_idle_timeout()) note(true, "no pulses for " + fmt(since) + "s");
            } else if (since > 0 && inst_w_ > 0) {
                // Power cannot be higher than one pulse per `since` seconds.
                double ub = power_from_interval(since);
                if (ub < inst_w_) inst_w_ = ub;
            }
        }
        flush_old(wall_of(now_ts));
    }

    void on_source_error() {
        std::lock_guard<std::mutex> l(m_);
        if (sm_.on_source_error()) note(true, "pulse source failure");
    }

    Snapshot snapshot() const {
        std::lock_guard<std::mutex> l(m_);
        Snapshot s;
        s.state = sm_.state(); s.source = src_;
        s.pulses = pulses_; s.debounced = debounced_; s.out_of_order = out_of_order_; s.spikes = spikes_;
        s.energy_kwh = energy_wh_ / 1000.0; s.power_w = inst_w_; s.smooth_w = smooth_w_;
        s.peak_w = peak_w_; s.cost = cost_;
        s.wall_s = wall_of(now_ts_ ? now_ts_ : ts0_);
        s.rate = rate_at(s.wall_s);
        s.overload_w = cfg_.overload_w;
        s.events.assign(events_.begin(), events_.end());
        return s;
    }
    std::vector<Bucket> history(std::size_t n) const {
        std::lock_guard<std::mutex> l(m_);
        std::vector<Bucket> out(hist_.end() - std::min(n, hist_.size()), hist_.end());
        if (cur_.pulses) out.push_back(cur_);
        return out;
    }

    // --- pure helpers (public for tests) ---
    double power_from_interval(double dt_s) const { return 3.6e6 / cfg_.imp_per_kwh / dt_s; }
    double rate_at(double wall_s) const {
        std::time_t t = static_cast<std::time_t>(wall_s);
        std::tm tm{};
        localtime_r(&t, &tm);
        if (tm.tm_hour >= 18 && tm.tm_hour < 22) return cfg_.rate_peak;
        if (tm.tm_hour >= 6 && tm.tm_hour < 18) return cfg_.rate_normal;
        return cfg_.rate_offpeak;
    }

private:
    double wall_of(uint64_t ts) const { return wall0_ + (static_cast<double>(ts) - static_cast<double>(ts0_)) / 1e9; }
    static std::string fmt(double v) { std::ostringstream o; o.precision(0); o << std::fixed << v; return o.str(); }

    void note(bool changed, const std::string& extra = "") {
        if (!changed) return;
        std::string e = std::string("-> ") + to_string(sm_.state()) + (extra.empty() ? "" : " (" + extra + ")");
        push_event(e);
    }
    void push_event(const std::string& e) {
        events_.push_back(e);
        if (events_.size() > 20) events_.pop_front();
    }
    void detect_spike(double p, uint64_t) {
        if (win_.size() >= 10) {
            double mean = 0, var = 0;
            for (double v : win_) mean += v;
            mean /= win_.size();
            for (double v : win_) var += (v - mean) * (v - mean);
            double sd = std::sqrt(var / win_.size());
            if (p > mean + cfg_.spike_sigma * sd && p > 1.5 * mean) {
                ++spikes_;
                push_event("anomaly: spike " + fmt(p) + "W vs mean " + fmt(mean) + "W");
            }
        }
        win_.push_back(p);
        if (win_.size() > cfg_.window) win_.pop_front();
    }
    void add_to_bucket(double wall, double wh, double c) {
        long minute = static_cast<long>(wall / 60.0);
        if (cur_.pulses && minute != cur_.minute) finalize();
        cur_.minute = minute;
        cur_.wh += wh; cur_.cost += c; ++cur_.pulses;
        cur_.peak_w = std::max(cur_.peak_w, inst_w_);
    }
    void flush_old(double wall) {
        if (cur_.pulses && static_cast<long>(wall / 60.0) != cur_.minute) finalize();
    }
    void finalize() {
        hist_.push_back(cur_);
        if (hist_.size() > 1440) hist_.pop_front();
        if (sink_) sink_(cur_);
        cur_ = Bucket{};
    }

    mutable std::mutex m_;
    Config cfg_;
    StateMachine sm_;
    double wall0_;
    uint64_t ts0_, now_ts_ = 0, last_ts_ = 0;
    bool have_last_ = false, have_ewma_ = false;
    uint64_t pulses_ = 0, debounced_ = 0, out_of_order_ = 0, spikes_ = 0;
    double energy_wh_ = 0, cost_ = 0, inst_w_ = 0, smooth_w_ = 0, peak_w_ = 0;
    std::deque<double> win_;
    std::deque<std::string> events_;
    std::deque<Bucket> hist_;
    Bucket cur_;
    BucketSink sink_;
    EnergySink energy_sink_;
    std::string src_;
};

}  // namespace sm
