// smeterd - Smart-meter pulse counter & analytics agent (daemon)
#include <atomic>
#include <csignal>
#include <functional>
#include <map>
#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include "smeter/analytics.hpp"
#include "smeter/energy_agent.hpp"
#include "smeter/gpio_source.hpp"
#include "smeter/http_server.hpp"
#include "smeter/sim_source.hpp"
#include "smeter/storage.hpp"

using namespace sm;

static volatile std::sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

static void usage() {
    std::cout <<
R"(smeterd - smart meter pulse counter & analytics agent
  --sim                 use the built-in meter simulator (default)
  --gpio CHIP LINE      use real hardware, e.g. --gpio /dev/gpiochip0 17
  --speed X             simulator time acceleration (default 60)
  --start-hour H        simulator virtual start hour 0-24 (default 17.5)
  --no-overload         disable simulated overload burst
  --imp N               meter constant, pulses/kWh (default 1000)
  --overload-w W        overload limit in watts (default 5000)
  --port P              HTTP port (default 8099)
  --bind ADDR           HTTP bind address (default 127.0.0.1)
  --data-dir DIR        persistence directory (default ./data)
  --profile P           simulator load: appliances (default, smart-agent demo) | classic
  --no-spike-demo       do not add the unusual evening AC spike to today's simulated load
  --duration S          exit after S real seconds (default: run forever)
)";
}

int main(int argc, char** argv) {
    Config cfg;
    bool use_gpio = false;
    std::string chip = "/dev/gpiochip0", data_dir = "./data", bind = "127.0.0.1";
    unsigned line = 17;
    double speed = 60, start_hour = 17.5, duration = 0;
    bool overload = true, spike_demo = true;
    std::string profile = "appliances";
    int port = 8099;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::cerr << "missing value for " << what << "\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "--sim") use_gpio = false;
        else if (a == "--gpio") { use_gpio = true; chip = next("--gpio"); line = std::atoi(next("--gpio LINE")); }
        else if (a == "--speed") speed = std::atof(next("--speed"));
        else if (a == "--start-hour") start_hour = std::atof(next("--start-hour"));
        else if (a == "--no-overload") overload = false;
        else if (a == "--imp") cfg.imp_per_kwh = std::atof(next("--imp"));
        else if (a == "--overload-w") cfg.overload_w = std::atof(next("--overload-w"));
        else if (a == "--port") port = std::atoi(next("--port"));
        else if (a == "--bind") bind = next("--bind");
        else if (a == "--data-dir") data_dir = next("--data-dir");
        else if (a == "--profile") profile = next("--profile");
        else if (a == "--no-spike-demo") spike_demo = false;
        else if (a == "--duration") duration = std::atof(next("--duration"));
        else { usage(); return a == "--help" ? 0 : 2; }
    }

    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    (void)!std::system(("mkdir -p '" + data_dir + "'").c_str());
    Storage store(data_dir);

    // ---- smart energy agent: tariff, appliance register, usage history ----
    SlabTariff tariff;
    const std::vector<Appliance> appliances = defaultAppliances();
    UsageTracker usage;
    std::function<double(double)> load_model;  // set only for the appliance-based simulator
    std::unique_ptr<PulseSource> src;
    if (use_gpio) src = std::make_unique<GpioCdevSource>(chip, line);
    else {
        auto sim = std::make_unique<SimSource>(cfg.imp_per_kwh, speed, start_hour, overload);
        if (profile == "appliances") {
            std::vector<Boost> boosts;
            if (spike_demo) boosts.push_back({"Air conditioner", localTime(sim->wall_start_s()).day, {19, 22}});  // hot evening today
            load_model = ApplianceLoadModel(appliances, boosts);
            sim->set_load_model(load_model);
        }
        src = std::move(sim);
    }

    PulseQueue q;
    if (!src->start(q)) { std::cerr << "cannot start pulse source\n"; return 1; }

    Analytics an(cfg, src->wall_start_s(), src->now_ns());
    an.set_source_name(src->name());
    an.set_sink([&](const Bucket& b) { store.append_bucket(b); });
    an.set_energy_sink([&](double wall, double wh) { usage.add(wall, wh); });
    auto seed_usage = [&] {   // simulator: start the month with realistic history
        if (load_model) seedHistory(usage, load_model, src->wall_start_s());
    };
    if (use_gpio) { std::map<long, Hours> h; if (store.load_usage(h)) usage.restore(h); }
    else seed_usage();
    EnergyAgent agent(tariff, appliances, usage, cfg.rate_peak, cfg.rate_offpeak);
    PersistedState ps;
    if (store.load_state(ps)) {
        an.restore(ps.pulses, ps.energy_wh, ps.cost);
        std::cout << "[smeterd] restored counter: " << ps.pulses << " pulses, " << ps.energy_wh / 1000 << " kWh\n";
    }
    an.start();
    if (std::getenv("SMETERD_RESTARTED")) an.log_event("daemon restarted (totals restored)");

    std::atomic<bool> restart_req{false}, reset_req{false};
    HttpServer http(an, static_cast<uint16_t>(port), bind);
    http.set_agent(&agent);
    http.set_restart_handler([&] { restart_req = true; });   // whole-daemon restart (re-exec)
    http.set_reset_handler([&] { reset_req = true; });       // counters reset only
    if (!http.start()) { std::cerr << "cannot start HTTP server\n"; return 1; }
    std::cout << "[smeterd] source=" << src->name() << "  http://" << bind << ":" << port << "/api/status\n";

    auto save = [&] {
        auto s = an.snapshot();
        store.save_state({s.pulses, s.energy_kwh * 1000.0, s.cost});
        if (use_gpio) store.save_usage(usage.snapshot());
    };
    const uint64_t t_begin = mono_ns();
    uint64_t last_save = t_begin;
    bool fault_reported = false;

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        while (auto p = q.pop()) an.on_pulse(p->ts_ns);   // drain lock-free queue
        if (restart_req) break;                            // whole-daemon restart: leave loop, clean up, re-exec below
        if (reset_req.exchange(false)) {                // "Reset counters" in the dashboard
            src->stop();
            while (q.pop()) {}
            src->failed = false; fault_reported = false;
            if (!src->start(q)) { std::cerr << "restart: cannot start pulse source\n"; an.on_source_error(); fault_reported = true; }
            usage.clear(); seed_usage();
            an.reset(src->wall_start_s(), src->now_ns());
            an.start();
            store.save_state({0, 0.0, 0.0});
            std::cout << "[smeterd] counters reset from dashboard\n";
            continue;
        }
        an.tick(src->now_ns());
        if (src->failed && !fault_reported) { an.on_source_error(); fault_reported = true; }
        const uint64_t now = mono_ns();
        if (now - last_save > 5'000'000'000ull) { save(); last_save = now; }
        if (duration > 0 && (now - t_begin) / 1e9 >= duration) break;
    }

    std::cout << "[smeterd] shutting down (dropped in queue: " << q.dropped() << ")\n";
    src->stop();
    http.stop();
    while (auto p = q.pop()) an.on_pulse(p->ts_ns);
    save();
    if (restart_req) {                                   // replace this process with a fresh copy (same PID)
        std::cout << "[smeterd] restarting daemon...\n" << std::flush;
        setenv("SMETERD_RESTARTED", "1", 1);
        char path[4096];
        ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
        if (n > 0) { path[n] = 0; execv(path, argv); }
        std::perror("[smeterd] re-exec failed");
        return 1;
    }
    return 0;
}
