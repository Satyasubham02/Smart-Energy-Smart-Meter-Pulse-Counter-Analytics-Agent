#pragma once
// Appliance register, energy formulas, ranking, "What if I save?" and the appliance-based load model.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "smeter/calendar.hpp"
#include "smeter/tariff.hpp"

namespace sm {

struct Window { double start, end; };  // hours of day [start,end); end < start wraps past midnight

inline double windowLength(const std::vector<Window>& w) {
    double s = 0;
    for (auto& x : w) s += x.end >= x.start ? x.end - x.start : 24.0 - x.start + x.end;
    return s;
}
inline bool inWindows(const std::vector<Window>& w, double h) {
    for (auto& x : w) {
        if (x.end >= x.start ? (h >= x.start && h < x.end) : (h >= x.start || h < x.end)) return true;
    }
    return false;
}
/// Hours of the (non-wrapping) interval [a,b) that fall inside the appliance's usage windows.
inline double windowOverlap(const std::vector<Window>& w, double a, double b) {
    double s = 0;
    for (auto& x : w) {
        auto part = [&](double lo, double hi) { s += std::max(0.0, std::min(hi, b) - std::max(lo, a)); };
        if (x.end >= x.start) part(x.start, x.end); else { part(x.start, 24); part(0, x.end); }
    }
    return s;
}

struct Appliance {
    std::string name;
    double watts;           // approximate power draw while running
    double hours_per_day;   // typical running hours on a day it is used
    double days_per_month;  // days per month it is used
    std::vector<Window> windows;  // when it usually runs (used by the simulator & recommendations)
    bool shiftable = false;       // can the user move its usage to another time of day?
    bool essential = false;       // always-on / non-discretionary (never recommended for trimming)
};

/// Energy (kWh) = Power (W) x hours / 1000
inline double calculateEnergyConsumption(double watts, double hours) { return watts * hours / 1000.0; }
inline double applianceDailyKwh(const Appliance& a) { return calculateEnergyConsumption(a.watts, a.hours_per_day); }
/// Monthly kWh = daily kWh x days used per month
inline double applianceMonthlyKwh(const Appliance& a) { return applianceDailyKwh(a) * a.days_per_month; }

struct ApplianceUsage {
    std::string name;
    double watts, hours_per_day, days_per_month, daily_kwh, monthly_kwh, monthly_cost, share_pct;
};

/// Ranks appliances by monthly kWh. Cost = the slab energy charge for the combined consumption,
/// split in proportion to each appliance's share (so the costs add up to the energy charge).
inline std::vector<ApplianceUsage> rankAppliancesByConsumption(const std::vector<Appliance>& apps, const SlabTariff& t) {
    double total = 0;
    for (auto& a : apps) total += applianceMonthlyKwh(a);
    const double energy_charge = calculateBill(t, total).energy_charge;
    std::vector<ApplianceUsage> out;
    for (auto& a : apps) {
        double m = applianceMonthlyKwh(a), share = total > 0 ? m / total : 0;
        out.push_back({a.name, a.watts, a.hours_per_day, a.days_per_month, applianceDailyKwh(a), m, energy_charge * share, share * 100.0});
    }
    std::sort(out.begin(), out.end(), [](auto& x, auto& y) { return x.monthly_kwh > y.monthly_kwh; });
    return out;
}

inline const Appliance* findAppliance(const std::vector<Appliance>& apps, std::string name) {
    auto low = [](std::string s) { for (auto& c : s) c = std::tolower(static_cast<unsigned char>(c)); return s; };
    name = low(name);
    for (auto& a : apps) if (low(a.name) == name) return &a;
    for (auto& a : apps) if (low(a.name).find(name) != std::string::npos) return &a;
    return nullptr;
}

struct WhatIf {
    bool valid = false;
    std::string appliance, summary;
    double current_hours = 0, new_hours = 0, current_kwh = 0, new_kwh = 0, units_saved = 0, money_saved = 0;
    double pct_reduction = 0, pct_of_total = 0, bill_before = 0, bill_after = 0;
};

/// "What if I save?": change one appliance's daily hours and re-price the month with the slab tariff.
/// `baseline_month_units` = projected (or modelled) total monthly consumption.
inline WhatIf calculatePotentialSavings(const SlabTariff& t, double baseline_month_units, const Appliance& a, double new_hours) {
    WhatIf w;
    w.appliance = a.name;
    w.current_hours = a.hours_per_day;
    w.new_hours = std::max(0.0, std::min(24.0, new_hours));
    w.current_kwh = applianceMonthlyKwh(a);
    w.new_kwh = calculateEnergyConsumption(a.watts, w.new_hours) * a.days_per_month;
    w.units_saved = w.current_kwh - w.new_kwh;
    const double base = std::max(baseline_month_units, w.current_kwh);
    w.bill_before = calculateBill(t, base).total;
    w.bill_after = calculateBill(t, std::max(0.0, base - w.units_saved)).total;
    w.money_saved = w.bill_before - w.bill_after;
    w.pct_reduction = w.current_kwh > 0 ? w.units_saved / w.current_kwh * 100.0 : 0;
    w.pct_of_total = base > 0 ? w.units_saved / base * 100.0 : 0;
    const double d = std::fabs(w.current_hours - w.new_hours);
    const std::string unit = std::fabs(d - 1.0) < 0.05 ? " hour/day" : " hours/day";
    if (w.units_saved >= 0)
        w.summary = "Reducing " + a.name + " usage by " + numAuto(d) + unit + " could save approximately " + num(w.units_saved) +
                    " units/month and " + t.currency + num(w.money_saved) + "/month under the current demo tariff.";
    else
        w.summary = "Increasing " + a.name + " usage by " + numAuto(d) + unit + " would add approximately " + num(-w.units_saved) +
                    " units/month and cost about " + t.currency + num(-w.money_saved) + " more per month under the current demo tariff.";
    w.valid = true;
    return w;
}

/// Demo household (about 190 units/month). Edit freely - everything else adapts.
inline std::vector<Appliance> defaultAppliances() {
    return {
        {"Air conditioner", 1200, 2.5, 25, {{22, 24}, {0, 0.5}}, false},
        {"Refrigerator", 120, 8, 30, {{0, 24}}, false, true},          // compressor duty-cycle spread over the day
        {"Water heater (geyser)", 1500, 0.4, 30, {{18.5, 18.9}}, true},
        {"Washing machine", 500, 1.0, 12, {{19, 20}}, true},
        {"Fan", 60, 10, 30, {{20, 24}, {0, 6}}, false},
        {"Television", 80, 4, 30, {{19, 23}}, false},
        {"Lights", 60, 6, 30, {{18, 24}}, false},
        {"Computer / laptop", 100, 5, 26, {{10, 15}}, false},
        {"Microwave", 1000, 0.2, 20, {{7.5, 7.6}, {19, 19.1}}, false},
        {"Water pump", 750, 0.4, 30, {{6, 6.2}, {18, 18.2}}, true},
    };
}

/// One-off extra usage (e.g. a very hot evening: the AC runs 7-10 PM) used to demo spike detection.
struct Boost { std::string appliance; long day; Window w; };

/// Instantaneous household load (W) at a wall-clock time, built from the appliance register.
/// Deterministic: day-to-day variation comes from a hash of the day number.
class ApplianceLoadModel {
public:
    ApplianceLoadModel(std::vector<Appliance> apps, std::vector<Boost> boosts = {})
        : apps_(std::move(apps)), boosts_(std::move(boosts)) {}

    double operator()(double wall_s) const {
        const LocalTime L = localTime(wall_s);
        double watts = 0;
        for (auto& a : apps_) {
            const double len = windowLength(a.windows);
            if (len <= 0) continue;
            const size_t k = a.name.size();
            if (static_cast<double>((L.day * 7 + static_cast<long>(k)) % 30) >= a.days_per_month) continue;  // not used today
            const double scale = 1.0 + 0.08 * std::sin(L.day * 1.3 + k);                                 // +-8 % daily variation
            if (inWindows(a.windows, L.hour_f)) watts += a.watts * a.hours_per_day / len * scale;
            for (auto& b : boosts_)
                if (b.day == L.day && b.appliance == a.name && inWindows({b.w}, L.hour_f)) watts += a.watts;
        }
        return watts;
    }

private:
    std::vector<Appliance> apps_;
    std::vector<Boost> boosts_;
};

}  // namespace sm
