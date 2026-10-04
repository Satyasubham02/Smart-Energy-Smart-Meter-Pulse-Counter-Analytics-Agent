// Dependency-free test runner: unit + integration tests for the smart meter agent.
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>
#include "smeter/analytics.hpp"
#include "smeter/http_server.hpp"
#include "smeter/ring_buffer.hpp"
#include "smeter/sim_source.hpp"
#include "smeter/state_machine.hpp"
#include "smeter/storage.hpp"

using namespace sm;

static int g_fail = 0, g_run = 0;
#define CHECK(c) do { if (!(c)) { std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))
static void run(const char* name, const std::function<void()>& f) {
    ++g_run; std::printf("[ RUN ] %s\n", name); f();
}
static constexpr uint64_t S = 1000000000ull;
// 2026-01-01 12:00 local-independent: use a fixed wall clock at noon local time.
static double noon_wall() { std::time_t n = std::time(nullptr); std::tm t{}; localtime_r(&n, &t); t.tm_hour = 12; t.tm_min = t.tm_sec = 0; return (double)std::mktime(&t); }

int main() {
    run("ring: push/pop order and overflow", [] {
        SpscRing<int, 4> r;
        for (int i = 0; i < 4; ++i) CHECK(r.push(i));
        CHECK(!r.push(99)); CHECK(r.dropped() == 1);
        for (int i = 0; i < 4; ++i) CHECK(*r.pop() == i);
        CHECK(!r.pop().has_value());
    });
    run("ring: producer/consumer threads lose nothing", [] {
        SpscRing<uint64_t, 1024> r;
        const uint64_t N = 200000; uint64_t sum = 0, got = 0;
        std::thread prod([&] { for (uint64_t i = 1; i <= N;) if (r.push(i)) ++i; });
        while (got < N) if (auto v = r.pop()) { sum += *v; ++got; }
        prod.join();
        CHECK(sum == N * (N + 1) / 2);
    });
    run("analytics: power from interval (1000 imp/kWh, 3.6 s -> 1000 W)", [] {
        Analytics a(Config{}, noon_wall(), 0);
        NEAR(a.power_from_interval(3.6), 1000.0, 1e-9);
        NEAR(a.power_from_interval(0.36), 10000.0, 1e-9);
    });
    run("analytics: energy and pulse count", [] {
        Analytics a(Config{}, noon_wall(), 0); a.start();
        for (int i = 0; i <= 1000; ++i) a.on_pulse(uint64_t(i) * 3600000000ull);  // 1000 W for 1000 intervals
        auto s = a.snapshot();
        CHECK(s.pulses == 1001); NEAR(s.energy_kwh, 1.001, 1e-9); NEAR(s.power_w, 1000.0, 1e-6);
        CHECK(s.state == State::Measuring);
    });
    run("analytics: debounce rejects contact bounce", [] {
        Analytics a(Config{}, noon_wall(), 0); a.start();
        a.on_pulse(1 * S); a.on_pulse(1 * S + 5000000ull); a.on_pulse(1 * S + 8000000ull);
        auto s = a.snapshot(); CHECK(s.pulses == 1); CHECK(s.debounced == 2);
    });
    run("analytics: out-of-order timestamps dropped", [] {
        Analytics a(Config{}, noon_wall(), 0); a.start();
        a.on_pulse(10 * S); a.on_pulse(5 * S);
        auto s = a.snapshot(); CHECK(s.pulses == 1); CHECK(s.out_of_order == 1);
    });
    run("analytics: overload and recovery with hysteresis", [] {
        Config c; c.overload_w = 5000; Analytics a(c, noon_wall(), 0); a.start();
        uint64_t t = 0; a.on_pulse(t);
        for (int i = 0; i < 5; ++i) { t += 360000000ull; a.on_pulse(t); }       // 10 kW
        CHECK(a.snapshot().state == State::Overload);
        for (int i = 0; i < 3; ++i) { t += 1 * S; a.on_pulse(t); }               // 3.6 kW < 4.5 kW
        CHECK(a.snapshot().state == State::Measuring);
    });
    run("analytics: spike detection", [] {
        Analytics a(Config{}, noon_wall(), 0); a.start();
        uint64_t t = 0; a.on_pulse(t);
        for (int i = 0; i < 30; ++i) { t += 12 * S; a.on_pulse(t); }             // steady 300 W
        CHECK(a.snapshot().spikes == 0);
        t += 1 * S; a.on_pulse(t);                                               // 3.6 kW jump
        CHECK(a.snapshot().spikes == 1);
    });
    run("analytics: idle timeout and decay", [] {
        Config c; c.idle_timeout_s = 60; Analytics a(c, noon_wall(), 0); a.start();
        a.on_pulse(0); a.on_pulse(4 * S);                                        // 900 W
        a.tick(20 * S);
        CHECK(a.snapshot().power_w < 900.0 && a.snapshot().power_w > 0);          // decaying upper bound
        a.tick(100 * S);
        CHECK(a.snapshot().state == State::Idle); NEAR(a.snapshot().power_w, 0.0, 1e-9);
    });
    run("analytics: TOU tariff and per-minute buckets", [] {
        Analytics a(Config{}, noon_wall(), 0); a.start();
        NEAR(a.rate_at(noon_wall()), 6.0, 1e-9);
        NEAR(a.rate_at(noon_wall() + 7 * 3600), 8.5, 1e-9);                       // 19:00
        NEAR(a.rate_at(noon_wall() + 11 * 3600), 4.0, 1e-9);                      // 23:00
        std::vector<Bucket> got; a.set_sink([&](const Bucket& b) { got.push_back(b); });
        for (int i = 0; i < 130; ++i) a.on_pulse(uint64_t(i) * S);               // 130 s of 1 Hz pulses
        a.tick(200 * S);
        CHECK(got.size() >= 2); NEAR(got[0].pulses + got[1].pulses + a.history(10).back().pulses, 130.0, 1.0);
    });
    run("state machine: all transitions", [] {
        StateMachine m(1000);
        CHECK(m.state() == State::Init); CHECK(m.on_start()); CHECK(m.state() == State::Idle);
        CHECK(m.on_power(100)); CHECK(m.state() == State::Measuring);
        CHECK(m.on_power(2000)); CHECK(m.state() == State::Overload);
        CHECK(!m.on_power(950));  CHECK(m.on_power(800)); CHECK(m.state() == State::Measuring);
        CHECK(m.on_idle_timeout()); CHECK(m.state() == State::Idle);
        CHECK(m.on_source_error()); CHECK(m.state() == State::Fault);
        CHECK(!m.on_power(10)); CHECK(m.on_recover()); CHECK(m.state() == State::Idle);
    });
    run("storage: state round-trip and CSV", [] {
        (void)!std::system("rm -rf /tmp/smtest && mkdir -p /tmp/smtest");
        Storage st("/tmp/smtest"); PersistedState a{12345, 6789.125, 42.5}, b;
        CHECK(st.save_state(a)); CHECK(st.load_state(b));
        CHECK(b.pulses == 12345); NEAR(b.energy_wh, 6789.125, 1e-6); NEAR(b.cost, 42.5, 1e-6);
        Bucket k; k.minute = 100; k.wh = 1.5; k.pulses = 2; st.append_bucket(k);
        CHECK(::access("/tmp/smtest/energy_minutes.csv", F_OK) == 0);
    });
    run("http: routing and JSON", [] {
        Analytics a(Config{}, noon_wall(), 0); a.start(); a.on_pulse(0); a.on_pulse(4 * S);
        HttpServer h(a, 0); int st; std::string ct;
        auto j = h.route("/api/status", st, ct); CHECK(st == 200); CHECK(j.find("\"pulses\":2") != std::string::npos);
        CHECK(h.route("/metrics", st, ct).find("smeter_pulses_total 2") != std::string::npos);
        h.route("/nope", st, ct); CHECK(st == 404);
        auto page = h.route("/", st, ct); CHECK(st == 200); CHECK(ct.find("text/html") == 0); CHECK(page.find("<canvas") != std::string::npos);
        CHECK(h.route("/api/status", st, ct).find("\"overload_w\":5000") != std::string::npos);
        CHECK(h.route("/api/history", st, ct).find("\"t\":[") != std::string::npos);
    });
    run("analytics: restart clears everything", [] {
        Analytics a(Config{}, noon_wall(), 0); a.start();
        for (int i = 0; i < 20; ++i) a.on_pulse(uint64_t(i) * 4 * S);
        CHECK(a.snapshot().pulses == 20);
        a.reset(noon_wall(), 100 * S); a.start();
        auto s = a.snapshot();
        CHECK(s.pulses == 0); NEAR(s.energy_kwh, 0.0, 1e-12); NEAR(s.cost, 0.0, 1e-12); NEAR(s.peak_w, 0.0, 1e-12);
        CHECK(s.state == State::Idle); CHECK(a.history(10).empty());
        a.on_pulse(100 * S); a.on_pulse(104 * S);
        CHECK(a.snapshot().pulses == 2);
    });
    run("integration: simulator -> queue -> analytics matches ground truth", [] {
        SimSource sim(1000.0, 600.0, 12.0, true); PulseQueue q;
        CHECK(sim.start(q));
        Analytics a(Config{}, sim.wall_start_s(), sim.now_ns()); a.start();
        auto t_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (std::chrono::steady_clock::now() < t_end) {
            while (auto p = q.pop()) a.on_pulse(p->ts_ns);
            a.tick(sim.now_ns());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        sim.stop();
        while (auto p = q.pop()) a.on_pulse(p->ts_ns);
        auto s = a.snapshot(); double truth = sim.truth_wh() / 1000.0;
        std::printf("    measured %.4f kWh vs truth %.4f kWh, pulses=%llu, dropped=%zu\n", s.energy_kwh, truth, (unsigned long long)s.pulses, q.dropped());
        CHECK(q.dropped() == 0);
        NEAR(s.energy_kwh, truth, 0.0015);                                       // within 1.5 Wh (< 2 pulses)
    });

    std::printf("\n%d tests, %d failed assertions\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
