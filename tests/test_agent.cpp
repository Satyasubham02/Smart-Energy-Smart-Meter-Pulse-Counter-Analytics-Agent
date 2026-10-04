// Unit + integration tests for the Smart Energy Agent extension (tariff, bill calculator, tracker,
// appliances, what-if, slab warnings, spike detection, recommendations, API).
#include <cstdio>
#include <functional>
#include "smeter/analytics.hpp"
#include "smeter/energy_agent.hpp"
#include "smeter/agent_json.hpp"
#include "smeter/http_server.hpp"
#include "smeter/storage.hpp"

using namespace sm;
static int g_fail = 0, g_run = 0;
#define CHECK(c) do { if (!(c)) { std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))
static void run(const char* n, const std::function<void()>& f) { ++g_run; std::printf("[ RUN ] %s\n", n); f(); }
static bool has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

// local wall-clock time on a fixed date
static double wallAt(int y, int mon1, int d, double hour) {
    std::tm t{}; t.tm_year = y - 1900; t.tm_mon = mon1 - 1; t.tm_mday = d; t.tm_hour = static_cast<int>(hour);
    t.tm_min = static_cast<int>((hour - t.tm_hour) * 60); t.tm_isdst = -1;
    return static_cast<double>(std::mktime(&t));
}

int main() {
    const SlabTariff T;

    run("tariff: telescopic bill", [&] {
        NEAR(calculateBill(T, 92).total, 92 * 2.5 + 30, 1e-9);
        NEAR(calculateBill(T, 150).total, 250 + 50 * 3.5 + 30, 1e-9);
        NEAR(calculateBill(T, 250).total, 250 + 350 + 50 * 5 + 30, 1e-9);
        NEAR(calculateBill(T, 350).energy_charge, 250 + 350 + 500 + 50 * 7, 1e-9);
        NEAR(calculateBill(T, 0).total, 0, 1e-9);
        CHECK(calculateBill(T, 250).lines.size() == 3);
    });
    run("tariff: current slab and units to next slab", [&] {
        CHECK(getCurrentSlab(T, 92) == 0); CHECK(getCurrentSlab(T, 100) == 0); CHECK(getCurrentSlab(T, 100.5) == 1);
        CHECK(getCurrentSlab(T, 500) == 3);
        NEAR(getUnitsToNextSlab(T, 92), 8, 1e-9); NEAR(getUnitsToNextSlab(T, 96), 4, 1e-9);
        NEAR(getUnitsToNextSlab(T, 500), -1, 1e-9);
        CHECK(slabLabel(T, 0) == "1-100"); CHECK(slabLabel(T, 1) == "101-200"); CHECK(slabLabel(T, 3) == "300+");
    });
    run("energy formulas", [&] {
        NEAR(calculateEnergyConsumption(1500, 2), 3.0, 1e-12);
        Appliance ac{"AC", 1500, 6, 30, {}, false};
        NEAR(applianceDailyKwh(ac), 9.0, 1e-12); NEAR(applianceMonthlyKwh(ac), 270.0, 1e-12);
    });
    run("projection and daily saving target", [&] {
        NEAR(calculateProjectedConsumption(100, 10, 30, -1), 300, 1e-9);                   // 10/day for 20 more days
        NEAR(calculateProjectedConsumption(100, 10, 30, 12), 100 + 20 * (0.6 * 12 + 0.4 * 10), 1e-9);
        NEAR(calculateProjectedConsumption(100, 30, 30, 5), 100, 1e-9);                    // month over
        NEAR(calculateDailySavingTarget(214, 200, 14), 1.0, 1e-9);
        NEAR(calculateDailySavingTarget(190, 200, 14), 0.0, 1e-9);
    });
    run("slab warnings: green / yellow / red", [&] {
        auto g = evaluateSlab(T, 50, 80);   CHECK(g.zone == Zone::Safe);
        auto y1 = evaluateSlab(T, 96, 98);  CHECK(y1.zone == Zone::Approaching); NEAR(y1.units_to_next, 4, 1e-9);
        CHECK(has(y1.message, "Only 4 units remain before the next tariff slab"));
        auto y2 = evaluateSlab(T, 150, 198); CHECK(y2.zone == Zone::Approaching); CHECK(has(y2.message, "2 units away from the 200-unit threshold"));
        auto r = evaluateSlab(T, 150, 214);  CHECK(r.zone == Zone::High); NEAR(r.excess, 14, 1e-9); NEAR(r.threshold, 200, 1e-9);
        auto r2 = evaluateSlab(T, 92, 101);  CHECK(r2.zone == Zone::High);
        auto early = evaluateSlab(T, 16, 190); CHECK(early.zone == Zone::Approaching);   // early month: not a false "reduce 90 units" alarm
        auto top = evaluateSlab(T, 320, 400); CHECK(top.zone == Zone::High);
    });
    run("smart bill calculator (92 units example)", [&] {
        BillCalcInput in; in.units = 92; in.days = 27; in.month_days = 30;
        auto r = smartBillCalculator(T, in);
        CHECK(r.valid); NEAR(r.bill.total, 92 * 2.5 + 30, 1e-9); NEAR(r.avg_per_day, 92.0 / 27, 1e-9);
        NEAR(r.projected_units, 92 + 3 * 92.0 / 27, 1e-9);
        NEAR(r.slab.units_to_next, 8, 1e-9);
        CHECK(has(r.warning, "close to the next tariff slab") || has(r.warning, "remain before the next tariff slab"));
        CHECK(has(r.action, "units/day")); CHECK(r.daily_target > 0 && r.daily_target < 2);
        in.avg_daily = 2.0; in.prev_units = 95;                                         // optional inputs
        auto r2 = smartBillCalculator(T, in);
        NEAR(r2.projected_units, 92 + 3 * 2.0, 1e-9); CHECK(r2.has_prev);
        BillCalcInput bad; bad.days = 0; CHECK(!smartBillCalculator(T, bad).valid);
    });
    run("appliance ranking sorted, shares sum to 100, costs sum to energy charge", [&] {
        auto apps = defaultAppliances(); auto rk = rankAppliancesByConsumption(apps, T);
        double sh = 0, cost = 0, kwh = 0;
        for (size_t i = 0; i < rk.size(); ++i) { sh += rk[i].share_pct; cost += rk[i].monthly_cost; kwh += rk[i].monthly_kwh; if (i) CHECK(rk[i - 1].monthly_kwh >= rk[i].monthly_kwh); }
        NEAR(sh, 100, 1e-6); NEAR(cost, calculateBill(T, kwh).energy_charge, 1e-6);
        CHECK(rk[0].name == "Air conditioner"); CHECK(kwh > 150 && kwh < 260);
    });
    run("what-if: AC 6h -> 4h uses real watts and tariff", [&] {
        Appliance ac{"Air conditioner", 1500, 6, 30, {}, false};
        auto w = calculatePotentialSavings(T, 300, ac, 4);   // household projected at 300 units/month
        NEAR(w.units_saved, 1.5 * 2 * 30, 1e-9); NEAR(w.pct_reduction, 100.0 / 3, 1e-6);
        NEAR(w.money_saved, calculateBill(T, 300).total - calculateBill(T, 210).total, 1e-9);
        CHECK(has(w.summary, "save approximately 90 units/month")); CHECK(has(w.summary, "demo tariff"));
        auto up = calculatePotentialSavings(T, 300, ac, 8); CHECK(up.units_saved < 0); CHECK(has(up.summary, "Increasing"));
    });
    run("tracker: today / yesterday / month / baseline / peak", [&] {
        UsageTracker u; const double now = wallAt(2026, 5, 15, 12.0);
        const long today = localTime(now).day;
        for (int d = 0; d <= 14; ++d) for (int h = 0; h < 24; ++h) u.seedHour(today - d, h, h >= 18 && h < 21 ? 0.5 : 0.1);   // evening peak
        NEAR(u.yesterday(now), 3 * 0.5 + 21 * 0.1, 1e-9);
        NEAR(u.today(now), 3 * 0.5 + 21 * 0.1, 1e-9);
        NEAR(u.monthToDate(now), 15 * (3 * 0.5 + 21 * 0.1), 1e-9);                  // 1st..15th May
        NEAR(u.lastDays(now, 7), 7 * 3.6, 1e-9);
        int used = 0; NEAR(u.avgPrevDays(now, 7, &used), 3.6, 1e-9); CHECK(used == 7);
        auto pk = u.peakPeriod(now); CHECK(pk.valid && pk.start == 18 && pk.end == 21);
        CHECK(u.baseline(now).valid());
        NEAR(u.dailySeries(now, 3).size(), 3, 0);
    });
    run("spike detection finds the unusual window", [&] {
        UsageTracker u; const double now = wallAt(2026, 5, 15, 23.0); const long today = localTime(now).day;
        for (int d = 1; d <= 10; ++d) for (int h = 0; h < 24; ++h) u.seedHour(today - d, h, 0.3);
        for (int h = 0; h < 23; ++h) u.seedHour(today, h, (h >= 19 && h < 22) ? 1.8 : 0.3);
        auto sp = detectConsumptionSpike(u.hours(today), 23.0, u.baseline(now));
        CHECK(sp.has_value()); if (sp) { CHECK(sp->start == 19 && sp->end == 22); NEAR(sp->excess_kwh, 3 * 1.5, 1e-9); }
        UsageTracker flat; for (int d = 0; d <= 10; ++d) for (int h = 0; h < 24; ++h) flat.seedHour(localTime(now).day - d, h, 0.3);
        CHECK(!detectConsumptionSpike(flat.hours(localTime(now).day), 23.0, flat.baseline(now)).has_value());
    });
    run("recommendations: data-driven, ranked, projected 214 example", [&] {
        auto apps = defaultAppliances();
        RecContext c; c.tariff = &T; c.appliances = &apps; c.ranking = rankAppliancesByConsumption(apps, T);
        c.units_month = 150; c.projected = 214; c.remaining_days = 14; c.slab = evaluateSlab(T, 150, 214);
        c.have_baseline = true; c.pct_vs_usual = 18;
        auto recs = generateRecommendations(c);
        CHECK(!recs.empty()); CHECK(recs[0].id == "slab");
        CHECK(has(recs[0].detail, "projected monthly consumption is 214 units")); CHECK(has(recs[0].detail, "Reducing approximately 14 units"));
        NEAR(recs[0].money, calculateBill(T, 214).total - calculateBill(T, 200).total, 1e-9);
        bool action = false, fridge = false;
        for (auto& r : recs) { if (r.id == "action") action = true; if (has(r.title, "Refrigerator")) fridge = true; }
        CHECK(action); CHECK(!fridge);                                              // never tells you to trim the fridge
        for (size_t i = 1; i < recs.size(); ++i) CHECK(recs[i - 1].priority >= recs[i].priority);
    });
    run("load model + seeding + full agent report", [&] {
        auto apps = defaultAppliances(); const double now = wallAt(2026, 5, 15, 17.5);
        ApplianceLoadModel model(apps, {{"Air conditioner", localTime(now).day, {19, 22}}});
        CHECK(model(wallAt(2026, 5, 15, 12.0)) > 100);                              // fridge + computer
        UsageTracker u; seedHistory(u, model, now);
        NEAR(u.hours(localTime(now).day)[18] + u.hours(localTime(now).day)[19], 0.0, 1e-9);   // today only up to 17:30
        EnergyAgent ag(T, apps, u);
        auto r = ag.report(now, 300);
        CHECK(r.data_ready); CHECK(r.units_month > 50 && r.units_month < 140);      // 14 full days + part of 15th
        CHECK(r.projected > r.units_month); CHECK(r.projected > 150 && r.projected < 260);
        CHECK(r.ranking[0].name == "Air conditioner"); CHECK(r.have_baseline); CHECK(r.peak.valid);
        CHECK(!r.recs.empty()); CHECK(r.daily14.size() == 14); CHECK(r.weekly4.size() == 4);
        // later in the evening the injected spike appears
        for (double t = now; t < wallAt(2026, 5, 15, 22.5); t += 60) u.add(t, model(t) / 60.0);
        auto r2 = ag.report(wallAt(2026, 5, 15, 22.5), 300);
        CHECK(r2.spike.has_value()); CHECK(has(r2.spike_msg, "Unusual consumption spike detected between 7 PM and 10 PM"));
        CHECK(r2.pct_vs_usual > 20); CHECK(has(r2.today_msg, "higher than your 7-day average"));
        auto w = ag.whatIf("air conditioner", 1.5, wallAt(2026, 5, 15, 22.5)); CHECK(w.valid); CHECK(w.units_saved > 0);
        CHECK(!ag.whatIf("toaster", 1, now).valid);
    });
    run("usage persistence round-trip", [&] {
        (void)!std::system("rm -rf /tmp/smag && mkdir -p /tmp/smag");
        Storage st("/tmp/smag"); std::map<long, Hours> a, b; Hours h{}; h[3] = 1.25; h[20] = 0.5; a[20000] = h; a[20001] = h;
        CHECK(st.save_usage(a)); CHECK(st.load_usage(b)); CHECK(b.size() == 2); NEAR(b[20000][3], 1.25, 1e-6); NEAR(b[20001][20], 0.5, 1e-6);
    });
    run("analytics energy hook feeds the tracker (existing analytics unchanged)", [&] {
        const double w0 = wallAt(2026, 5, 15, 12.0); Analytics a(Config{}, w0, 0); a.start(); UsageTracker u;
        a.set_energy_sink([&](double wall, double wh) { u.add(wall, wh); });
        for (int i = 0; i <= 100; ++i) a.on_pulse(uint64_t(i) * 3600000000ull);   // 101 pulses @1000 imp/kWh, 3.6 s apart
        NEAR(u.today(w0), a.snapshot().energy_kwh, 1e-9); NEAR(u.today(w0), 0.101, 1e-9);
    });
    run("API: /api/agent /api/whatif /api/bill /api/appliances", [&] {
        auto apps = defaultAppliances(); const double now = wallAt(2026, 5, 15, 17.5);
        ApplianceLoadModel model(apps); UsageTracker u; seedHistory(u, model, now);
        EnergyAgent ag(T, apps, u); Analytics an(Config{}, now, 0); an.start();
        HttpServer h(an, 0); h.set_agent(&ag); int st; std::string ct;
        auto j = h.route("/api/agent", st, ct); CHECK(st == 200); CHECK(has(j, "\"recommendations\"")); CHECK(has(j, "\"prediction\""));
        j = h.route("/api/whatif?appliance=Air%20conditioner&hours=1", st, ct); CHECK(st == 200); CHECK(has(j, "\"units_saved\""));
        h.route("/api/whatif?appliance=nothing", st, ct); CHECK(st == 400);
        j = h.route("/api/bill?units=92&days=27", st, ct); CHECK(st == 200); CHECK(has(j, "\"to_next\":8.0"));
        h.route("/api/bill?units=5&days=0", st, ct); CHECK(st == 400);
        CHECK(has(h.route("/api/appliances", st, ct), "Air conditioner"));
        CHECK(has(h.route("/api/status", st, ct), "\"state\""));                    // old endpoint still works
    });

    std::printf("\n%d tests, %d failed assertions\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
