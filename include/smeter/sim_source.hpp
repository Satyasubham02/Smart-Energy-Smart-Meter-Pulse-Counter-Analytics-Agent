#pragma once
// Software meter: generates pulses from a synthetic household load profile.
// Runs on a *virtual clock* (speed factor) so a day of data can be produced in minutes.
#include <chrono>
#include <cmath>
#include <ctime>
#include <functional>
#include <thread>
#include "smeter/pulse_source.hpp"

namespace sm {

class SimSource : public PulseSource {
public:
    SimSource(double imp_per_kwh, double speed, double start_hour, bool with_overload = true)
        : imp_(imp_per_kwh), speed_(speed), start_hour_(start_hour), overload_(with_overload) {}
    ~SimSource() override { stop(); }

    bool start(PulseQueue& q) override {
        t0_real_ = mono_ns();
        wall0_ = wall_start_s();
        truth_wh_ = 0;
        run_ = true;
        th_ = std::thread([this, &q] { loop(q); });
        return true;
    }
    void stop() override {
        run_ = false;
        if (th_.joinable()) th_.join();
    }
    std::string name() const override { return "simulator(x" + std::to_string(static_cast<int>(speed_)) + ")"; }
    uint64_t now_ns() const override {
        return t0_real_ + static_cast<uint64_t>((mono_ns() - t0_real_) * speed_);
    }
    double wall_start_s() const override {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        tm.tm_hour = static_cast<int>(start_hour_);
        tm.tm_min = static_cast<int>((start_hour_ - tm.tm_hour) * 60);
        tm.tm_sec = 0;
        return static_cast<double>(std::mktime(&tm));
    }
    /// Ground-truth energy actually "consumed" (for validation in tests).
    double truth_wh() const { return truth_wh_.load(); }

    /// Load profile in watts at virtual time `t` seconds since start.
    /// Optional: replace the built-in profile by a model of wall-clock time (e.g. the appliance model).
    void set_load_model(std::function<double(double)> m) { model_ = std::move(m); }

    double load_w(double t) const {
        if (model_) return model_(wall0_ + t);
        double w = 250.0 + 120.0 * std::sin(2 * M_PI * t / 3600.0);  // fridge/lights base
        double m = std::fmod(t, 1200.0);
        if (m >= 300 && m < 480) w += 2000.0;                          // kettle/heater cycle
        if (overload_) {
            double h = std::fmod(t, 3600.0);
            if (h >= 2400 && h < 2460) w += 6000.0;                    // fault: overload burst
        }
        return w;
    }

private:
    void loop(PulseQueue& q) {
        double acc = 0.0, t_prev = 0.0, truth = 0.0;
        while (run_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            double t = (now_ns() - t0_real_) / 1e9;
            double dt = t - t_prev;
            if (dt <= 0) continue;
            double w = load_w(0.5 * (t + t_prev));
            truth += w * dt / 3600.0;
            double total = w * dt / 3600.0 * imp_ / 1000.0;  // pulses emitted in this step
            double a1 = acc + total;
            for (double n = 1; n <= a1; n += 1) {
                double frac = (n - acc) / total;
                q.push(Pulse{t0_real_ + static_cast<uint64_t>((t_prev + frac * dt) * 1e9)});
            }
            acc = a1 - std::floor(a1);
            t_prev = t;
            truth_wh_ = truth;
        }
    }
    double imp_, speed_, start_hour_;
    bool overload_;
    uint64_t t0_real_ = 0;
    double wall0_ = 0;
    std::function<double(double)> model_;
    std::atomic<bool> run_{false};
    std::atomic<double> truth_wh_{0.0};
    std::thread th_;
};

}  // namespace sm
