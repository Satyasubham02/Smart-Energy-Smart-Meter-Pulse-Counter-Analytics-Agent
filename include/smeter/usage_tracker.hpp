#pragma once
// Keeps kWh per local hour for each day, and answers today / yesterday / week / month questions.
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <optional>
#include <vector>
#include "smeter/calendar.hpp"

namespace sm {

struct PeakPeriod { bool valid = false; int start = 0, end = 0; double kwh_per_day = 0, share_pct = 0; };
struct Baseline {  // "typical" kWh for each hour, from previous days
    Hours mean{}, sd{};
    int days = 0;
    bool valid() const { return days >= 3; }
};
struct Spike { int start, end; double actual_kwh, expected_kwh, excess_kwh, ratio; };  // [start,end) hours

class UsageTracker {
public:
    /// Called for every accepted meter pulse (energy in Wh at wall-clock time).
    void add(double wall_s, double wh) {
        const LocalTime L = localTime(wall_s);
        std::lock_guard<std::mutex> l(m_);
        days_[L.day][L.hour] += wh / 1000.0;
        while (days_.size() > 62) days_.erase(days_.begin());
    }
    void seedHour(long day, int hour, double kwh) { std::lock_guard<std::mutex> l(m_); days_[day][hour] = kwh; }
    void clear() { std::lock_guard<std::mutex> l(m_); days_.clear(); }
    std::map<long, Hours> snapshot() const { std::lock_guard<std::mutex> l(m_); return days_; }
    void restore(const std::map<long, Hours>& d) { std::lock_guard<std::mutex> l(m_); days_ = d; }

    double today(double now) const { std::lock_guard<std::mutex> l(m_); return total(localTime(now).day); }
    double yesterday(double now) const { std::lock_guard<std::mutex> l(m_); return total(localTime(now).day - 1); }
    /// Sum of the last n days including today.
    double lastDays(double now, int n) const {
        std::lock_guard<std::mutex> l(m_);
        const long d = localTime(now).day;
        double s = 0;
        for (int i = 0; i < n; ++i) s += total(d - i);
        return s;
    }
    /// Mean of the n full days before today that have data.
    double avgPrevDays(double now, int n, int* used = nullptr) const {
        std::lock_guard<std::mutex> l(m_);
        const long d = localTime(now).day;
        double s = 0; int c = 0;
        for (int i = 1; i <= n; ++i) if (total(d - i) > 0) { s += total(d - i); ++c; }
        if (used) *used = c;
        return c ? s / c : 0.0;
    }
    double monthToDate(double now) const {
        std::lock_guard<std::mutex> l(m_);
        const long d = localTime(now).day, mk = monthKeyOfDay(d);
        double s = 0;
        for (auto& [day, h] : days_) if (day <= d && monthKeyOfDay(day) == mk) s += sum(h);
        return s;
    }
    Hours hours(long day) const { std::lock_guard<std::mutex> l(m_); auto it = days_.find(day); return it == days_.end() ? Hours{} : it->second; }
    int daysWithData() const { std::lock_guard<std::mutex> l(m_); int c = 0; for (auto& [d, h] : days_) if (sum(h) > 0) ++c; return c; }

    /// Oldest -> newest kWh for the last n days (today last).
    std::vector<double> dailySeries(double now, int n) const {
        std::lock_guard<std::mutex> l(m_);
        const long d = localTime(now).day;
        std::vector<double> v;
        for (int i = n - 1; i >= 0; --i) v.push_back(total(d - i));
        return v;
    }

    Baseline baseline(double now, int max_days = 14) const {
        std::lock_guard<std::mutex> l(m_);
        const long d = localTime(now).day;
        std::vector<Hours> prev;
        for (int i = 1; i <= max_days; ++i) {
            auto it = days_.find(d - i);
            if (it != days_.end() && sum(it->second) > 0) prev.push_back(it->second);
        }
        Baseline b; b.days = static_cast<int>(prev.size());
        for (int h = 0; h < 24 && !prev.empty(); ++h) {
            double m = 0, v = 0;
            for (auto& p : prev) m += p[h];
            m /= prev.size();
            for (auto& p : prev) v += (p[h] - m) * (p[h] - m);
            b.mean[h] = m; b.sd[h] = std::sqrt(v / prev.size());
        }
        return b;
    }

    /// The 3-hour window with the highest typical consumption.
    PeakPeriod peakPeriod(double now) const {
        Baseline b = baseline(now, 30);
        PeakPeriod p;
        if (!b.valid()) return p;
        double best = -1, all = 0;
        for (double v : b.mean) all += v;
        for (int s = 0; s < 24; ++s) {
            double w = b.mean[s] + b.mean[(s + 1) % 24] + b.mean[(s + 2) % 24];
            if (w > best) { best = w; p.start = s; }
        }
        p.valid = true; p.end = (p.start + 3) % 24; p.kwh_per_day = best; p.share_pct = all > 0 ? best / all * 100.0 : 0;
        return p;
    }

private:
    static double sum(const Hours& h) { double s = 0; for (double v : h) s += v; return s; }
    double total(long day) const { auto it = days_.find(day); return it == days_.end() ? 0.0 : sum(it->second); }
    mutable std::mutex m_;
    std::map<long, Hours> days_;
};

/// kWh we would expect to have used by `hour_f` today, from the typical hourly profile.
inline double expectedSoFar(const Baseline& b, double hour_f) {
    double s = 0;
    const int whole = static_cast<int>(std::floor(hour_f));
    for (int h = 0; h < whole && h < 24; ++h) s += b.mean[h];
    if (whole < 24) s += (hour_f - whole) * b.mean[whole];
    return s;
}

/// Finds the worst run of consecutive completed hours that are far above the typical profile.
inline std::optional<Spike> detectConsumptionSpike(const Hours& today, double hour_f, const Baseline& b) {
    if (!b.valid()) return std::nullopt;
    const int done = static_cast<int>(std::floor(hour_f));  // completed hours only
    std::optional<Spike> best;
    int h = 0;
    while (h < done) {
        auto flagged = [&](int i) {
            double ex = today[i] - b.mean[i];
            return ex >= 0.15 && today[i] > b.mean[i] + 2 * b.sd[i] && today[i] > 1.5 * b.mean[i];
        };
        if (!flagged(h)) { ++h; continue; }
        int s = h; double act = 0, exp = 0;
        while (h < done && flagged(h)) { act += today[h]; exp += b.mean[h]; ++h; }
        Spike sp{s, h, act, exp, act - exp, exp > 0 ? act / exp : 0};
        if (sp.excess_kwh >= 0.3 && (!best || sp.excess_kwh > best->excess_kwh)) best = sp;
    }
    return best;
}

}  // namespace sm
