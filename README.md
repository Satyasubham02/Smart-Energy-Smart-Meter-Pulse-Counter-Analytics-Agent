# ⚡ Smart Energy Smart-Meter Pulse Counter & Analytics Agent

A real-time, Linux-only, **C++17** agent that reads the pulse output (LED / S0 contact) of an energy meter through the
Linux GPIO character-device driver, and turns it into energy, power, cost, anomaly and overload information, served via
REST/Prometheus and a terminal dashboard. No third-party libraries.

![build](https://img.shields.io/badge/C%2B%2B-17-blue) ![os](https://img.shields.io/badge/OS-Linux-green)

## Stage 1 – Introduction
**Objective.** Build a dependable software/hardware pipeline from a physical pulse to actionable analytics.

**Problem.** Millions of legacy meters expose only a blinking LED (e.g. 1000 imp/kWh). Reading it reliably needs: precise
edge timestamps, contact-bounce rejection, no lost pulses under load, power-cut safe counters, and instant insight
(is the load abnormal? what does it cost at peak tariff?).

**Scope.** Pulse capture (GPIO driver + simulator) · debounce · power/energy · TOU billing · anomaly/overload detection ·
state machine · persistence · HTTP API · CLI dashboard. Single-phase, local network.

**Outcome / applications.** Home energy monitors, sub-metering of flats or machines, factory load alarms, solar/EV
monitoring, edge gateways feeding Prometheus/Grafana.

## Architecture (summary)
```
 Meter LED ─► GPIO ─► Linux GPIO driver (IRQ, timestamp, debounce) ─► /dev/gpiochipN
                                                                         │ ioctl/poll/read
   SimSource ───────────────┐                                            ▼
                            ├─► SPSC lock-free ring ─► Analytics ─► StateMachine
   GpioCdevSource ◄─────────┘                            │  ├─► Storage (state file + CSV)
                                                         │  └─► HttpServer ─► smeter-cli / Prometheus / curl
```
Full diagrams (architecture, class, sequence, state) in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

**Device-driver concepts:** char device node, `ioctl` line request, kernel IRQ→event FIFO with kernel timestamps,
kernel-side debounce, `poll`/`eventfd`, `gpio-sim` for test. *(The brief requires C++ only, and kernel modules are C, so
the driver layer is the user-space GPIO uAPI behind a pluggable `PulseSource` HAL.)*

## Project layout
```
include/smeter/   common, ring_buffer, pulse_source (HAL), gpio_source, sim_source,
                  analytics, state_machine, storage, http_server, dashboard (embedded web UI)
src/              smeterd.cpp (daemon) · smeter_cli.cpp (dashboard) · pulse_injector.cpp (gpio-sim driver)
tests/            test_main.cpp (unit + integration)
scripts/          system_test.sh · gpio_sim_setup.sh
docs/             PRD.md · ARCHITECTURE.md · TESTING.md · PROGRESS.md
.github/workflows ci.yml
```

## Build & run
Requirements: Linux, g++ ≥ 9, make, curl (for the system test).
```bash
make                                   # builds build/smeterd, smeter-cli, pulse-injector
./build/smeterd --sim --speed 60 &     # simulator: 1 real second = 1 virtual minute
./build/smeter-cli                     # live dashboard (Ctrl-C to quit)
curl localhost:8099/api/status
curl localhost:8099/api/history?n=30
curl localhost:8099/metrics            # Prometheus format
```
**Interactive web dashboard:** open <http://127.0.0.1:8099/> in a browser (served by `smeterd` itself, no extra tools).
Live power chart with hover values and overload line, per-minute history (Avg/Peak/Energy, 15m-24h ranges), state badge,
event log, pause/resume and CSV export.
Real hardware (e.g. Raspberry Pi, meter LED via photodiode/optocoupler on GPIO 17):
```bash
sudo ./build/smeterd --gpio /dev/gpiochip0 17 --imp 1000
```
Options: `./build/smeterd --help`.

| Endpoint | Description |
|---|---|
| `/` | interactive web dashboard |
| `/api/status` | state, power, energy, cost, counters, recent events |
| `/api/history?n=60` | per-minute avg/peak watts and Wh |
| `/metrics` | Prometheus exposition |
| `/healthz` | liveness |
| `/api/agent` `/api/appliances` `/api/whatif` `/api/bill` `/api/tariff` | Smart Energy Agent API (see docs/SMART_AGENT.md) |
| `POST /api/restart` | restart the whole `smeterd` process (clean shutdown, save totals, re-exec, same PID); needs header `X-Smeter: 1`; dashboard **Restart daemon** button |
| `POST /api/reset` | zero counters/history and restart the simulation; dashboard **Reset counters** button |

Data in `./data`: `smeter.state` (crash-safe counter) and `energy_minutes.csv`.

## Testing
```bash
make test          # 30 unit + integration tests (15 core + 15 smart-agent), ASan/UBSan
make tsan          # ThreadSanitizer
make system-test   # daemon + API + CLI + restart persistence
```
Hardware-in-the-loop with the kernel `gpio-sim` module: see [docs/TESTING.md](docs/TESTING.md).
Measured: simulator energy matches ground truth within 1 Wh with zero dropped pulses.

## Interface v3 (this revision)
Same daemon, API and tests; only the interface and a few features changed:
* **New web layout** - sidebar navigation, teal theme, semicircle **power gauge**, manual **light/dark toggle** (remembered in the browser).
* **New tab: Budget & Carbon** - set a monthly bill budget (status On track / At risk / Over budget, projected bill vs budget, daily unit allowance
  computed by inverting the slab tariff) and see CO2 so far / projected with a per-appliance breakdown (editable emission factor, default 0.82 kg/kWh).
* **CLI** - colour dashboard with power gauge, month bill + projection, slab zone and top-3 advice; new flags `--compact` (one-line status for
  status bars), `--no-color`, `--no-agent`. Colour turns off automatically when piped or when `NO_COLOR` is set.

## Smart Energy Agent (v2.0)
Beyond metering, the daemon now acts as an energy advisor: slab-tariff bill, month-end projection, tariff-zone warnings (green/yellow/red),
spike and trend detection, an appliance register with per-appliance cost, a **"What if I save?"** calculator, a **Smart Bill Calculator**,
and data-driven recommendations. Open **http://127.0.0.1:8099/** and use the tabs: *Live Meter - Smart Agent - What if I save? - Bill Calculator*.
```bash
./build/smeterd --sim --start-hour 21.3 --speed 60     # jump to the evening to see the unusual-spike warning
curl "localhost:8099/api/bill?units=92&days=27"        # bill calculator
curl "localhost:8099/api/whatif?appliance=air&hours=1.5"
```
Full guide, function map and viva Q&A: [docs/SMART_AGENT.md](docs/SMART_AGENT.md). Demo tariff and appliances are editable (`SlabTariff`, `defaultAppliances()`).

## Documentation
[PRD & timeline](docs/PRD.md) · [Architecture & UML](docs/ARCHITECTURE.md) · [Testing](docs/TESTING.md) · [Progress log](docs/PROGRESS.md)

## Publish to GitHub
```bash
git remote add origin https://github.com/<you>/smart-meter-agent.git
git push -u origin main develop
```

## License
MIT
