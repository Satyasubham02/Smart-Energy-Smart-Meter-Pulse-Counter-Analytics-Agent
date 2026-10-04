#pragma once
// Smart slab warning system + month-end projection helpers.
#include <algorithm>
#include <string>
#include "smeter/tariff.hpp"

namespace sm {

enum class Zone { Safe, Approaching, High };
inline const char* zoneName(Zone z) {
    return z == Zone::Safe ? "SAFE" : z == Zone::Approaching ? "APPROACHING" : "HIGH";
}

struct SlabStatus {
    int slab = 0, proj_slab = 0;
    std::string label, proj_label;
    double rate = 0, units = 0, projected = 0;
    double units_to_next = -1;   // from current units; -1 = last slab
    double proj_to_next = -1;    // from projected units
    double threshold = 0;        // the boundary that matters (crossed, or about to be crossed)
    double excess = 0;           // projected units above `threshold` (High zone only)
    bool close = false;          // current units within the "approach" margin of next slab
    Zone zone = Zone::Safe;
    std::string message;
};

/// "Close" = within max(5 units, 8 % of the slab's upper bound) of the next slab.
inline double approachMargin(double upto) { return std::max(5.0, 0.08 * upto); }

inline SlabStatus evaluateSlab(const SlabTariff& t, double units, double projected) {
    SlabStatus s;
    s.units = units;
    s.projected = std::max(projected, units);
    s.slab = getCurrentSlab(t, units);
    s.proj_slab = getCurrentSlab(t, s.projected);
    s.label = slabLabel(t, s.slab);
    s.proj_label = slabLabel(t, s.proj_slab);
    s.rate = t.slabs[s.slab].rate;
    s.units_to_next = getUnitsToNextSlab(t, units);
    s.proj_to_next = getUnitsToNextSlab(t, s.projected);
    const bool has_next_now = s.units_to_next >= 0, has_next_proj = s.proj_to_next >= 0;
    const double margin_now = has_next_now ? approachMargin(t.slabs[s.slab].upto) : 0;
    const double margin_proj = has_next_proj ? approachMargin(t.slabs[s.proj_slab].upto) : 0;
    s.close = has_next_now && s.units_to_next <= margin_now;
    const std::string note = s.close && s.proj_slab > s.slab
        ? " You have consumed " + numAuto(units) + " units; only " + numAuto(s.units_to_next) + " units remain before the " +
          slabLabel(t, s.slab + 1) + " slab starts."
        : "";

    // Boundary just below the projection, if the projection has only recently crossed it (reachable by saving
    // <= 25 % of the projected units), or if we are already in the top slab.
    const double lower = s.proj_slab > 0 ? t.slabs[s.proj_slab - 1].upto : 0;
    const bool crossed = s.proj_slab > 0 && (!has_next_proj || (s.projected - lower) <= 0.25 * s.projected);

    if (crossed) {                                                    // RED: projected to be in a higher slab
        s.zone = Zone::High;
        s.threshold = lower;
        s.excess = s.projected - lower;
        s.message = "Projected consumption of " + num(s.projected) + " units crosses into the " + s.proj_label + " slab (" +
                    t.currency + num(t.slabs[s.proj_slab].rate, 2) + "/unit).";
        if (s.close) s.message += " You are close to the next tariff slab (" + numAuto(s.units_to_next) + " units left).";
    } else if (has_next_proj && s.proj_slab == s.slab && s.close) {   // YELLOW: current units close to the boundary
        s.zone = Zone::Approaching;
        s.threshold = t.slabs[s.slab].upto;
        s.message = "You have consumed " + numAuto(units) + " units. Only " + numAuto(s.units_to_next) +
                    " units remain before the next tariff slab.";
    } else if (has_next_proj && s.proj_to_next <= margin_proj) {      // YELLOW: projection close to the boundary
        s.zone = Zone::Approaching;
        s.threshold = t.slabs[s.proj_slab].upto;
        s.message = "You are approximately " + numAuto(s.proj_to_next) + " units away from the " + num(s.threshold) +
                    "-unit threshold. Reducing high-load appliance usage may help reduce your projected bill." + note;
    } else {                                                          // GREEN
        s.zone = Zone::Safe;
        s.threshold = has_next_proj ? t.slabs[s.proj_slab].upto : 0;
        s.message = "You are in the " + s.proj_label + " slab (" + t.currency + num(t.slabs[s.proj_slab].rate, 2) +
                    "/unit); projected consumption is comfortably below the next slab (" + numAuto(s.proj_to_next) + " units to go)." + note;
    }
    return s;
}

/// Month-end projection: consumed so far + remaining days x expected daily use.
/// Expected daily use blends the recent 7-day average (60 %) with the month-to-date average (40 %).
inline double calculateProjectedConsumption(double month_kwh, double elapsed_days, double days_in_month,
                                            double recent_daily_avg /* <=0 if unknown */) {
    const double remaining = std::max(0.0, days_in_month - elapsed_days);
    double rate = 0;
    const double mtd_avg = elapsed_days >= 1.0 ? month_kwh / elapsed_days : 0.0;
    if (recent_daily_avg > 0 && mtd_avg > 0) rate = 0.6 * recent_daily_avg + 0.4 * mtd_avg;
    else if (recent_daily_avg > 0) rate = recent_daily_avg;
    else if (mtd_avg > 0) rate = mtd_avg;
    else if (elapsed_days > 0.05) rate = month_kwh / elapsed_days;
    return month_kwh + remaining * rate;
}

/// Units/day that must be cut over the remaining days to bring `projected` down to `threshold`.
inline double calculateDailySavingTarget(double projected, double threshold, double remaining_days) {
    if (threshold <= 0 || projected <= threshold || remaining_days <= 0) return 0.0;
    return (projected - threshold) / remaining_days;
}

}  // namespace sm
