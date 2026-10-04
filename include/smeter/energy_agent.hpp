#pragma once
// EnergyAgent: turns raw usage + appliance register + tariff into answers and advice.
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "smeter/appliance.hpp"
#include "smeter/recommendations.hpp"
#include "smeter/slab_monitor.hpp"
#include "smeter/smart_bill.hpp"
#include "smeter/tariff.hpp"
#include "smeter/usage_tracker.hpp"

namespace sm {

struct AgentReport {
    bool data_ready = false;
    double now_wall = 0, power_w = 0;
    int day_of_month = 1, days_in_month = 30;
    double elapsed_days = 0, remaining_days = 0;
    // current status
    double units_month = 0;
    Bill bill;
    SlabStatus slab;
    // prediction
    double projected = 0;
    Bill proj_bill;
    // consumption & trend
    double today = 0, yesterday = 0, avg7 = 0, last7 = 0, avg_daily = 0;
    double pct_vs_usual = 0, pct_vs_yesterday = 0, trend_pct = 0;
    std::string trend = "stable", today_msg, spike_msg;
    bool have_baseline = false;
    PeakPeriod peak;
    std::optional<Spike> spike;
    // savings
    double save_units = 0, save_money = 0, needed_units = 0, daily_target = 0, daily_allowance = 0;
    // intelligence
    std::vector<ApplianceUsage> ranking;
    double modeled_month = 0;
    std::vector<Recommendation> recs;
    // analytics
    std::vector<double> daily14;
    Hours today_hours{}, typical_hours{};
    std::vector<double> weekly4;
};

class EnergyAgent {
public:
    EnergyAgent(SlabTariff t, std::vector<Appliance> a, const UsageTracker& u, double rate_peak = 8.5, double rate_offpeak = 4.0)
        : tariff_(std::move(t)), apps_(std::move(a)), usage_(u), rate_peak_(rate_peak), rate_off_(rate_offpeak) {}

    const SlabTariff& tariff() const { return tariff_; }
    const std::vector<Appliance>& appliances() const { return apps_; }

    AgentReport report(double now, double power_w) const {
        AgentReport r;
        r.now_wall = now; r.power_w = power_w;
        const LocalTime L = localTime(now);
        r.day_of_month = L.mday;
        r.days_in_month = daysInMonth(L.year, L.mon);
        r.elapsed_days = (L.mday - 1) + L.hour_f / 24.0;
        r.remaining_days = std::max(0.0, r.days_in_month - r.elapsed_days);

        r.ranking = rankAppliancesByConsumption(apps_, tariff_);
        for (auto& a : r.ranking) r.modeled_month += a.monthly_kwh;

        r.units_month = usage_.monthToDate(now);
        r.today = usage_.today(now);
        r.yesterday = usage_.yesterday(now);
        r.last7 = usage_.lastDays(now, 7);
        int used = 0;
        r.avg7 = usage_.avgPrevDays(now, 7, &used);
        r.avg_daily = r.elapsed_days >= 1 ? r.units_month / r.elapsed_days : r.avg7;
        r.data_ready = usage_.daysWithData() > 0;

        // projection (falls back to the appliance model when there is no history yet)
        r.projected = calculateProjectedConsumption(r.units_month, r.elapsed_days, r.days_in_month, used >= 3 ? r.avg7 : -1.0);
        if (!r.data_ready || r.projected < r.units_month) r.projected = std::max(r.units_month, r.modeled_month);
        r.bill = calculateBill(tariff_, r.units_month);
        r.proj_bill = calculateBill(tariff_, r.projected);
        r.slab = evaluateSlab(tariff_, r.units_month, r.projected);

        // trend: today vs the usual pattern for this time of day, vs yesterday, and 3-day vs previous 4-day
        const Baseline base = usage_.baseline(now);
        r.have_baseline = base.valid();
        const Hours th = usage_.hours(L.day);
        r.today_hours = th; r.typical_hours = base.mean;
        if (r.have_baseline) {
            const double exp = expectedSoFar(base, L.hour_f);
            if (exp > 0.05) r.pct_vs_usual = (r.today - exp) / exp * 100.0;
            r.today_msg = "Today's consumption is " + num(std::fabs(r.pct_vs_usual)) + "% " + (r.pct_vs_usual >= 0 ? "higher" : "lower") +
                          " than your 7-day average (for this time of day).";
            r.spike = detectConsumptionSpike(th, L.hour_f, base);
            if (r.spike) r.spike_msg = "Unusual consumption spike detected between " + hourLabel(r.spike->start) + " and " + hourLabel(r.spike->end) + ".";
        } else r.today_msg = "Collecting data - trends appear after a few days of readings.";
        {   // today so far vs yesterday up to the same time of day
            const Hours yh = usage_.hours(L.day - 1);
            double ys = 0; const int whole = static_cast<int>(std::floor(L.hour_f));
            for (int h = 0; h < whole && h < 24; ++h) ys += yh[h];
            if (whole < 24) ys += (L.hour_f - whole) * yh[whole];
            if (ys > 0.05) r.pct_vs_yesterday = (r.today - ys) / ys * 100.0;
        }
        r.daily14 = usage_.dailySeries(now, 14);
        auto s8 = usage_.dailySeries(now, 8);  // [0..6] = 7 full days before today
        double rec3 = (s8[4] + s8[5] + s8[6]) / 3.0, pri4 = (s8[0] + s8[1] + s8[2] + s8[3]) / 4.0;
        if (rec3 > 0 && pri4 > 0) {
            r.trend_pct = (rec3 - pri4) / pri4 * 100.0;
            r.trend = r.trend_pct > 5 ? "increasing" : r.trend_pct < -5 ? "decreasing" : "stable";
        }
        const auto v28 = usage_.dailySeries(now, 28);
        for (int w = 3; w >= 0; --w) {  // last four 7-day blocks, oldest first
            double s = 0; const auto& v = v28;
            for (int i = 0; i < 7; ++i) s += v[27 - w * 7 - i];
            r.weekly4.push_back(s);
        }
        r.peak = usage_.peakPeriod(now);

        // savings & targets
        r.needed_units = r.slab.excess;
        r.daily_target = calculateDailySavingTarget(r.projected, r.slab.zone == Zone::High ? r.slab.threshold : 0, r.remaining_days);
        if (r.slab.threshold > r.units_month && r.remaining_days > 0)
            r.daily_allowance = (r.slab.threshold - r.units_month) / r.remaining_days;
        // potential = trimming the top-3 appliances by 20 % for the rest of the month
        for (size_t i = 0; i < r.ranking.size() && i < 3; ++i)
            r.save_units += 0.2 * r.ranking[i].monthly_kwh / r.days_in_month * r.remaining_days;
        r.save_money = slabSaving(tariff_, r.projected, r.save_units);

        RecContext c;
        c.tariff = &tariff_; c.appliances = &apps_; c.ranking = r.ranking; c.slab = r.slab;
        c.units_month = r.units_month; c.projected = r.projected; c.remaining_days = r.remaining_days;
        c.daily_allowance = r.daily_allowance; c.spike = r.spike; c.peak = r.peak;
        c.have_baseline = r.have_baseline; c.pct_vs_usual = r.pct_vs_usual; c.rate_peak = rate_peak_; c.rate_offpeak = rate_off_;
        r.recs = generateRecommendations(c);
        return r;
    }

    /// "What if I save?" for one appliance using the projected monthly total as the baseline.
    WhatIf whatIf(const std::string& name, double new_hours, double now) const {
        const Appliance* a = findAppliance(apps_, name);
        if (!a) return WhatIf{};
        const AgentReport r = report(now, 0);
        return calculatePotentialSavings(tariff_, r.projected, *a, new_hours);
    }

private:
    SlabTariff tariff_;
    std::vector<Appliance> apps_;
    const UsageTracker& usage_;
    double rate_peak_, rate_off_;
};

/// Pre-fills `days_back` days of history (and today up to `wall_now`) from a load model, so the demo
/// starts with realistic month-to-date data instead of an empty month.
inline void seedHistory(UsageTracker& u, const std::function<double(double)>& load_w, double wall_now, int days_back = 14) {
    const LocalTime L = localTime(wall_now);
    const double midnight = wall_now - L.hour_f * 3600.0;
    for (int d = days_back; d >= 0; --d) {
        const double day0 = midnight - d * 86400.0, end = d == 0 ? wall_now : day0 + 86400.0;
        for (double t = day0; t < end; t += 60.0) {
            const double dt = std::min(60.0, end - t), mid = t + dt / 2;
            u.add(mid, load_w(mid) * dt / 3600.0);
        }
    }
}

}  // namespace sm
