#pragma once
// JSON serialisation for the energy-agent API (hand-written, no dependencies).
#include <iomanip>
#include <sstream>
#include "smeter/energy_agent.hpp"

namespace sm {

inline std::string jq(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
        else if (c == '\n') o += "\\n";
        else if (c < 0x20) o += ' ';
        else o += static_cast<char>(c);
    }
    return o + "\"";
}
inline std::string jn(double v, int d = 2) {
    if (!std::isfinite(v)) return "0";
    std::ostringstream o; o << std::fixed << std::setprecision(d) << v; return o.str();
}
template <class V> inline std::string jarr(const V& v, int d = 2) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + jn(v[i], d);
    return s + "]";
}

inline std::string billLinesJson(const SlabTariff& t, const Bill& b) {
    std::string s = "[";
    for (size_t i = 0; i < b.lines.size(); ++i)
        s += (i ? "," : "") + std::string("{\"slab\":") + jq(slabLabel(t, b.lines[i].slab)) + ",\"units\":" + jn(b.lines[i].units, 1) +
             ",\"rate\":" + jn(b.lines[i].rate) + ",\"amount\":" + jn(b.lines[i].amount) + "}";
    return s + "]";
}

inline std::string tariffJson(const SlabTariff& t) {
    std::string s = "{\"currency\":" + jq(t.currency) + ",\"fixed\":" + jn(t.fixed_charge) + ",\"slabs\":[";
    for (size_t i = 0; i < t.slabs.size(); ++i)
        s += (i ? "," : "") + std::string("{\"label\":") + jq(slabLabel(t, static_cast<int>(i))) + ",\"upto\":" +
             (t.slabs[i].upto >= kInf / 2 ? "null" : jn(t.slabs[i].upto, 0)) + ",\"rate\":" + jn(t.slabs[i].rate) + "}";
    return s + "]}";
}

inline std::string rankingJson(const std::vector<ApplianceUsage>& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i)
        s += (i ? "," : "") + std::string("{\"name\":") + jq(v[i].name) + ",\"watts\":" + jn(v[i].watts, 0) + ",\"hours\":" + jn(v[i].hours_per_day, 2) +
             ",\"days\":" + jn(v[i].days_per_month, 0) + ",\"daily_kwh\":" + jn(v[i].daily_kwh) + ",\"kwh\":" + jn(v[i].monthly_kwh, 1) +
             ",\"cost\":" + jn(v[i].monthly_cost) + ",\"share\":" + jn(v[i].share_pct, 1) + "}";
    return s + "]";
}

inline std::string agentJson(const AgentReport& r, const SlabTariff& t) {
    std::ostringstream o;
    o << "{\"ready\":" << (r.data_ready ? "true" : "false") << ",\"cur\":" << jq(t.currency) << ",\"now\":" << jn(r.now_wall, 0)
      << ",\"power_w\":" << jn(r.power_w, 1)
      << ",\"calendar\":{\"day\":" << r.day_of_month << ",\"dim\":" << r.days_in_month << ",\"elapsed\":" << jn(r.elapsed_days) << ",\"remaining\":" << jn(r.remaining_days) << "}"
      << ",\"status\":{\"units\":" << jn(r.units_month) << ",\"bill\":" << jn(r.bill.total) << ",\"energy_charge\":" << jn(r.bill.energy_charge)
      << ",\"fixed\":" << jn(r.bill.fixed_charge) << ",\"slab\":" << r.slab.slab << ",\"slab_label\":" << jq(r.slab.label) << ",\"rate\":" << jn(r.slab.rate)
      << ",\"to_next\":" << jn(r.slab.units_to_next, 1) << ",\"threshold\":" << jn(r.slab.threshold, 0) << ",\"zone\":" << jq(zoneName(r.slab.zone))
      << ",\"message\":" << jq(r.slab.message) << "}"
      << ",\"bill_lines\":" << billLinesJson(t, r.bill)
      << ",\"prediction\":{\"units\":" << jn(r.projected) << ",\"bill\":" << jn(r.proj_bill.total) << ",\"slab\":" << r.slab.proj_slab
      << ",\"slab_label\":" << jq(r.slab.proj_label) << ",\"to_next\":" << jn(r.slab.proj_to_next, 1) << "}"
      << ",\"consumption\":{\"today\":" << jn(r.today) << ",\"yesterday\":" << jn(r.yesterday) << ",\"avg7\":" << jn(r.avg7) << ",\"last7\":" << jn(r.last7)
      << ",\"avg_daily\":" << jn(r.avg_daily) << ",\"pct_vs_usual\":" << jn(r.pct_vs_usual, 1) << ",\"pct_vs_yesterday\":" << jn(r.pct_vs_yesterday, 1)
      << ",\"trend\":" << jq(r.trend) << ",\"trend_pct\":" << jn(r.trend_pct, 1) << ",\"today_msg\":" << jq(r.today_msg) << ",\"spike_msg\":" << jq(r.spike_msg);
    o << ",\"peak\":";
    if (r.peak.valid) o << "{\"start\":" << r.peak.start << ",\"end\":" << r.peak.end << ",\"label\":" << jq(hourLabel(r.peak.start) + " - " + hourLabel(r.peak.end))
                        << ",\"kwh\":" << jn(r.peak.kwh_per_day) << ",\"share\":" << jn(r.peak.share_pct, 1) << "}";
    else o << "null";
    o << ",\"spike\":";
    if (r.spike) o << "{\"start\":" << r.spike->start << ",\"end\":" << r.spike->end << ",\"actual\":" << jn(r.spike->actual_kwh) << ",\"expected\":" << jn(r.spike->expected_kwh)
                   << ",\"excess\":" << jn(r.spike->excess_kwh) << "}";
    else o << "null";
    o << "}"
      << ",\"savings\":{\"potential_units\":" << jn(r.save_units, 1) << ",\"potential_money\":" << jn(r.save_money) << ",\"needed_units\":" << jn(r.needed_units, 1)
      << ",\"daily_target\":" << jn(r.daily_target) << ",\"daily_allowance\":" << jn(r.daily_allowance) << "}"
      << ",\"intelligence\":{\"top_appliance\":" << jq(r.ranking.empty() ? "" : r.ranking[0].name) << ",\"top_kwh\":" << jn(r.ranking.empty() ? 0 : r.ranking[0].monthly_kwh, 1)
      << ",\"top_share\":" << jn(r.ranking.empty() ? 0 : r.ranking[0].share_pct, 1) << ",\"modeled_month\":" << jn(r.modeled_month, 1) << ",\"warning\":" << jq(r.slab.message)
      << ",\"recommendations\":[";
    for (size_t i = 0; i < r.recs.size(); ++i)
        o << (i ? "," : "") << "{\"id\":" << jq(r.recs[i].id) << ",\"title\":" << jq(r.recs[i].title) << ",\"detail\":" << jq(r.recs[i].detail)
          << ",\"units\":" << jn(r.recs[i].units, 1) << ",\"money\":" << jn(r.recs[i].money) << "}";
    o << "]},\"analytics\":{\"daily\":" << jarr(r.daily14) << ",\"hourly_today\":" << jarr(r.today_hours, 3) << ",\"hourly_typical\":" << jarr(r.typical_hours, 3)
      << ",\"weekly\":" << jarr(r.weekly4, 1) << ",\"appliances\":" << rankingJson(r.ranking) << "},\"tariff\":" << tariffJson(t) << "}";
    return o.str();
}

inline std::string whatIfJson(const WhatIf& w, const std::string& cur) {
    if (!w.valid) return "{\"error\":\"unknown appliance\"}";
    return "{\"appliance\":" + jq(w.appliance) + ",\"cur\":" + jq(cur) + ",\"current_hours\":" + jn(w.current_hours) + ",\"new_hours\":" + jn(w.new_hours) +
           ",\"current_kwh\":" + jn(w.current_kwh, 1) + ",\"new_kwh\":" + jn(w.new_kwh, 1) + ",\"units_saved\":" + jn(w.units_saved, 1) +
           ",\"money_saved\":" + jn(w.money_saved) + ",\"pct_reduction\":" + jn(w.pct_reduction, 1) + ",\"pct_of_total\":" + jn(w.pct_of_total, 1) +
           ",\"bill_before\":" + jn(w.bill_before) + ",\"bill_after\":" + jn(w.bill_after) + ",\"summary\":" + jq(w.summary) + "}";
}

inline std::string billCalcJson(const BillCalcResult& r, const SlabTariff& t) {
    if (!r.valid) return "{\"error\":" + jq(r.error) + "}";
    return "{\"cur\":" + jq(t.currency) + ",\"units\":" + jn(r.in.units, 1) + ",\"days\":" + jn(r.in.days, 0) + ",\"month_days\":" + jn(r.in.month_days, 0) +
           ",\"charge\":" + jn(r.bill.total) + ",\"energy_charge\":" + jn(r.bill.energy_charge) + ",\"fixed\":" + jn(r.bill.fixed_charge) +
           ",\"lines\":" + billLinesJson(t, r.bill) + ",\"avg_per_day\":" + jn(r.avg_per_day) + ",\"projected_units\":" + jn(r.projected_units, 1) +
           ",\"projected_bill\":" + jn(r.proj_bill.total) + ",\"proj_lines\":" + billLinesJson(t, r.proj_bill) + ",\"slab\":" + jq(r.slab.label) +
           ",\"proj_slab\":" + jq(r.slab.proj_label) + ",\"rate\":" + jn(r.slab.rate) + ",\"to_next\":" + jn(r.slab.units_to_next, 1) +
           ",\"zone\":" + jq(zoneName(r.slab.zone)) + ",\"warning\":" + jq(r.warning) + ",\"action\":" + jq(r.action) +
           ",\"daily_target\":" + jn(r.daily_target) + ",\"daily_allowance\":" + jn(r.daily_allowance) + ",\"potential_units\":" + jn(r.potential_units, 1) +
           ",\"potential_money\":" + jn(r.potential_money) + ",\"has_prev\":" + (r.has_prev ? "true" : "false") + ",\"prev_change_pct\":" + jn(r.prev_change_pct, 1) +
           ",\"remaining_days\":" + jn(r.remaining_days, 1) + "}";
}

}  // namespace sm
