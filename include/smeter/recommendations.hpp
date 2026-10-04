#pragma once
// generateRecommendations(): data-driven advice built from the measured/projected data, the slab tariff
// and the appliance register - not generic tips.
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include "smeter/appliance.hpp"
#include "smeter/slab_monitor.hpp"
#include "smeter/usage_tracker.hpp"

namespace sm {

struct Recommendation {
    std::string id, title, detail;
    double units = 0, money = 0;  // estimated saving per month (or for the rest of this month)
    int priority = 0;
};

struct RecContext {
    const SlabTariff* tariff = nullptr;
    const std::vector<Appliance>* appliances = nullptr;
    std::vector<ApplianceUsage> ranking;
    SlabStatus slab;
    double units_month = 0, projected = 0, remaining_days = 0, daily_allowance = 0;
    std::optional<Spike> spike;
    PeakPeriod peak;
    bool have_baseline = false;
    double pct_vs_usual = 0;
    double rate_peak = 8.5, rate_offpeak = 4.0;  // time-of-use rates from the live-meter config
};

inline double slabSaving(const SlabTariff& t, double projected, double units) {
    return calculateBill(t, projected).total - calculateBill(t, std::max(0.0, projected - units)).total;
}
inline std::string minutesOrHours(double h) {
    return h < 1.0 ? num(std::ceil(h * 60 / 5) * 5) + " minutes" : numAuto(h) + " hours";
}

inline std::vector<Recommendation> generateRecommendations(const RecContext& c, size_t max_n = 6) {
    std::vector<Recommendation> out;
    const SlabTariff& t = *c.tariff;
    const std::string cur = t.currency;
    auto byName = [&](const std::string& n) { return findAppliance(*c.appliances, n); };

    // 1) Slab crossing: how many units to remove, what it is worth, and which appliance can deliver it.
    if (c.slab.zone == Zone::High && c.slab.excess > 0) {
        const double ex = c.slab.excess, thr = c.slab.threshold;
        const double money = calculateBill(t, c.projected).total - calculateBill(t, thr).total;
        out.push_back({"slab", "Projected " + num(c.projected) + " units - likely to enter the " + num(thr) + "+ range",
                       "Your projected monthly consumption is " + num(c.projected) + " units. You are likely to enter the " + num(thr) +
                       "+ unit range. Reducing approximately " + num(ex) + " units this month could lower your estimated bill by " + cur + num(money) + ".",
                       ex, money, 1000 + static_cast<int>(money)});
        if (c.remaining_days >= 1) {
            const Appliance* pick = nullptr; double pick_h = 0;
            for (size_t i = 0; i < c.ranking.size() && i < 4; ++i) {   // first appliance that can cover it with <= 50 % cut
                const Appliance* a = byName(c.ranking[i].name);
                if (!a || a->essential || a->watts <= 0) continue;
                const double need_h = ex / (a->watts / 1000.0 * c.remaining_days);
                if (!pick) { pick = a; pick_h = std::min(need_h, 0.5 * a->hours_per_day); }   // fallback: biggest non-essential
                if (need_h <= 0.5 * a->hours_per_day) { pick = a; pick_h = need_h; break; }
            }
            if (pick) {
                const double got = pick_h * pick->watts / 1000.0 * c.remaining_days;
                out.push_back({"action", "Trim " + pick->name + " by " + minutesOrHours(pick_h) + " per day",
                               "For the remaining " + numAuto(c.remaining_days) + " days, using " + pick->name + " about " + minutesOrHours(pick_h) +
                               " less each day removes ~" + num(got, 1) + " of the " + num(ex, 1) + " units you need to cut" +
                               (got + 0.05 < ex ? " - combine with a second appliance to fully avoid the slab." : "."),
                               got, slabSaving(t, c.projected, got), 900});
            }
        }
    } else if (c.slab.zone == Zone::Approaching) {
        const double diff = c.slab.slab + 1 < static_cast<int>(t.slabs.size()) ? t.slabs[c.slab.slab + 1].rate - c.slab.rate : 0;
        out.push_back({"approach", "Stay under " + num(c.daily_allowance, 1) + " units/day",
                       "Only " + numAuto(c.slab.units_to_next >= 0 ? c.slab.units_to_next : c.slab.proj_to_next) + " units separate you from the " +
                       num(c.slab.threshold) + "-unit threshold. Every unit above it costs " + cur + num(diff, 2) +
                       " more. Keeping daily use under " + num(c.daily_allowance, 1) + " units for the remaining " + numAuto(c.remaining_days) +
                       " days keeps you in the current slab.",
                       0, 0, 700});
    }

    // 2) Unusual spike today: attribute it to the appliances that normally run in that window.
    if (c.spike) {
        const Spike& s = *c.spike;
        // Which appliance could explain the extra load? Compare the spike's extra kW with each appliance's wattage.
        const double extra_kw = s.excess_kwh / std::max(1, s.end - s.start);
        std::vector<std::pair<double, std::string>> cand;   // (distance from extra load, label)
        for (auto& a : *c.appliances) {
            if (a.essential) continue;
            const double kw = a.watts / 1000.0, dist = std::fabs(kw - extra_kw) / extra_kw;
            if (kw >= 0.5 * extra_kw && kw <= 1.6 * extra_kw) cand.push_back({dist, a.name + " (" + num(a.watts) + " W)"});
        }
        std::sort(cand.begin(), cand.end());
        std::string who;
        for (size_t i = 0; i < cand.size() && i < 2; ++i) who += (i ? ", " : "") + cand[i].second;
        const double rep = s.excess_kwh * std::max(0.0, c.remaining_days);  // if this repeated every remaining day
        out.push_back({"spike", "Unusual spike " + hourLabel(s.start) + " - " + hourLabel(s.end),
                       "Consumption between " + hourLabel(s.start) + " and " + hourLabel(s.end) + " was " + num(s.actual_kwh, 1) + " kWh, " +
                       num((s.ratio - 1) * 100) + "% above your typical " + num(s.expected_kwh, 1) + " kWh for those hours." +
                       (who.empty() ? "" : " The extra load of ~" + num(extra_kw, 1) + " kW is consistent with: " + who + ".") +
                       " If this repeated on every remaining day it would add ~" + num(rep) + " units.",
                       rep, slabSaving(t, c.projected, rep), 800});
    }

    // 3) Biggest appliance: price a 30-minute/day cut with the real tariff.
    const ApplianceUsage* top_p = nullptr;
    for (auto& u : c.ranking) { const Appliance* a = byName(u.name); if (a && !a->essential) { top_p = &u; break; } }
    if (top_p) {
        const ApplianceUsage& top = *top_p;
        const double u = top.watts / 1000.0 * 0.5 * top.days_per_month;
        out.push_back({"top", top.name + " is " + num(top.share_pct) + "% of your usage",
                       top.name + " (" + num(top.watts) + " W, " + numAuto(top.hours_per_day) + " h/day) uses ~" + num(top.monthly_kwh) +
                       " units a month, about " + cur + num(top.monthly_cost) + " of your bill. Using it 30 minutes less per day saves ~" +
                       num(u, 1) + " units (" + cur + num(slabSaving(t, std::max(c.projected, top.monthly_kwh), u)) + ") per month.",
                       u, slabSaving(t, std::max(c.projected, top.monthly_kwh), u), 200});
    }

    // 4) Time-of-use: shiftable appliances that run in the 6-10 PM peak-rate window.
    {
        const Appliance* best = nullptr; double best_kwh = 0;
        for (auto& a : *c.appliances) {
            const double len = windowLength(a.windows);
            if (!a.shiftable || len <= 0) continue;
            const double kwh = applianceMonthlyKwh(a) * windowOverlap(a.windows, 18, 22) / len;
            if (kwh > best_kwh) { best_kwh = kwh; best = &a; }
        }
        if (best && best_kwh >= 1.0) {
            const double money = best_kwh * (c.rate_peak - c.rate_offpeak);
            out.push_back({"shift", "Move " + best->name + " out of the 6-10 PM peak window",
                           num(best_kwh, 1) + " units/month of " + best->name + " run during peak-rate hours. Under time-of-use pricing, shifting it to off-peak saves about " +
                           cur + num(money) + "/month.",
                           0, money, 300 + static_cast<int>(money)});
        }
    }

    // 5) Trend vs your usual pattern.
    if (c.have_baseline && c.pct_vs_usual >= 10)
        out.push_back({"trend", "Using " + num(c.pct_vs_usual) + "% more than usual today",
                       "Today's consumption is " + num(c.pct_vs_usual) + "% higher than your 7-day average for this time of day. Check what is running right now.", 0, 0, 100});
    else if (c.have_baseline && c.pct_vs_usual <= -10)
        out.push_back({"trend", "Using " + num(-c.pct_vs_usual) + "% less than usual today",
                       "Good progress - today is " + num(-c.pct_vs_usual) + "% below your 7-day average for this time of day.", 0, 0, 90});

    if (out.empty())
        out.push_back({"ok", "On track", "Projected " + num(c.projected) + " units this month; no action needed.", 0, 0, 1});
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.priority > b.priority; });
    if (out.size() > max_n) out.resize(max_n);
    return out;
}

}  // namespace sm
