#pragma once
// Smart Bill Calculator: manual inputs -> bill, projection, slab warning, action.
#include <string>
#include "smeter/slab_monitor.hpp"

namespace sm {

struct BillCalcInput {
    double units = 0;          // units consumed so far
    double days = 1;           // days elapsed in the billing period
    double prev_units = -1;    // optional: previous month's total units
    double avg_daily = -1;     // optional: expected average daily consumption
    double month_days = 30;    // length of the billing month
};

struct BillCalcResult {
    bool valid = false;
    std::string error;
    BillCalcInput in;
    Bill bill, proj_bill;
    SlabStatus slab;
    double avg_per_day = 0, projected_units = 0, remaining_days = 0;
    double daily_target = 0, daily_allowance = 0;
    double potential_units = 0, potential_money = 0;
    bool has_prev = false;
    double prev_change_pct = 0;
    std::string warning, action;
};

inline BillCalcResult smartBillCalculator(const SlabTariff& t, BillCalcInput in) {
    BillCalcResult r;
    if (in.units < 0 || in.days <= 0 || in.month_days <= 0) { r.error = "Units must be >= 0 and days > 0."; return r; }
    if (in.month_days < in.days) in.month_days = in.days;
    r.in = in;
    r.bill = calculateBill(t, in.units);
    r.avg_per_day = in.units / in.days;
    r.remaining_days = in.month_days - in.days;

    // Expected daily use for the rest of the month: the user's figure if given, else units/day so far
    // (early in the period, blended 50/50 with last month's daily average when that is known).
    double rate = r.avg_per_day;
    if (in.avg_daily > 0) rate = in.avg_daily;
    else if (in.prev_units > 0 && in.days < 7) rate = 0.5 * r.avg_per_day + 0.5 * in.prev_units / in.month_days;
    r.projected_units = in.units + r.remaining_days * rate;
    r.proj_bill = calculateBill(t, r.projected_units);
    r.slab = evaluateSlab(t, in.units, r.projected_units);

    if (in.prev_units > 0) { r.has_prev = true; r.prev_change_pct = (r.projected_units - in.prev_units) / in.prev_units * 100.0; }

    r.daily_target = calculateDailySavingTarget(r.projected_units, r.slab.threshold, r.remaining_days);
    if (r.slab.zone != Zone::High && r.slab.threshold > 0 && r.remaining_days > 0)
        r.daily_allowance = std::max(0.0, (r.slab.threshold - in.units) / r.remaining_days);

    // Potential savings
    if (r.slab.excess > 0) {
        r.potential_units = r.slab.excess;
        r.potential_money = r.proj_bill.total - calculateBill(t, r.slab.threshold).total;
    } else {
        r.potential_units = 0.10 * r.remaining_days * rate;  // a 10 % trim of the remaining usage
        r.potential_money = r.proj_bill.total - calculateBill(t, r.projected_units - r.potential_units).total;
    }

    r.warning = r.slab.message;
    if (r.slab.zone == Zone::Approaching && r.slab.close) r.warning += " You are close to the next tariff slab.";
    if (r.slab.zone == Zone::High)
        r.action = r.daily_target > 0
            ? "Reducing approximately " + num(r.daily_target, 1) + " units/day could help keep your projected consumption below the next slab."
            : "Your consumption has already crossed the slab limit for this period.";
    else if (r.slab.zone == Zone::Approaching && r.daily_allowance > 0)
        r.action = "Keep usage under " + num(r.daily_allowance, 1) + " units/day for the remaining " + numAuto(r.remaining_days) +
                   " days to stay in the current slab.";
    else
        r.action = "No action needed - you are on track. Cutting 10 % of remaining usage would save about " + t.currency +
                   num(r.potential_money) + ".";
    r.valid = true;
    return r;
}

}  // namespace sm
