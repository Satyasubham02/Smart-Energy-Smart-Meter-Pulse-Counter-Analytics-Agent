# System Design & Architecture

## 1. Layered architecture
```mermaid
flowchart LR
  subgraph HW[Hardware / Kernel]
    M[Energy meter LED/S0] --> G[GPIO pin]
    G --> K["Linux GPIO driver\n(edge IRQ, timestamp, debounce)"]
    K --> C["/dev/gpiochipN"]
  end
  subgraph U["smeterd (user space, C++17)"]
    S1[GpioCdevSource] -->|Pulse ts_ns| Q[(SPSC lock-free ring)]
    S2[SimSource virtual clock] --> Q
    Q --> A[Analytics core]
    A --> SM[StateMachine]
    A --> ST[Storage: state file + CSV]
    A --> H[HttpServer]
  end
  C -->|ioctl + poll + read| S1
  H -->|JSON / Prometheus| CLI[smeter-cli]
  H --> P[Prometheus / curl]
```

## 2. Linux device-driver concepts used
| Concept | Where |
|---|---|
| Character device node `/dev/gpiochipN` | `GpioCdevSource::start` (`open`) |
| `ioctl` (`GPIO_V2_GET_LINE_IOCTL`) with `gpio_v2_line_request` | line request, edge + bias + debounce attributes |
| Kernel interrupt handling → event FIFO | kernel timestamps each edge (`gpio_v2_line_event.timestamp_ns`) |
| `poll()` / `eventfd` / blocking `read()` | event loop with clean shutdown |
| Kernel-side debounce (`GPIO_V2_LINE_ATTR_ID_DEBOUNCE`) | first debounce stage (hardware); analytics is second stage |
| `gpio-sim` + configfs/sysfs | hardware-in-the-loop testing (`scripts/gpio_sim_setup.sh`, `pulse-injector`) |
| `errno` handling, fd lifetime (RAII), `CLOCK_MONOTONIC` | all drivers |

> **Why not an in-kernel module?** The kernel module interface is C. The brief mandates C++ only, so the driver layer
> is the *user-space* GPIO uAPI — the same ABI that production user-space drivers use — with a pluggable HAL so a
> custom `/dev/smeterN` device can be added as another `PulseSource`.

## 3. Components
| Component | Responsibility |
|---|---|
| `PulseSource` (HAL) | start/stop, produce `Pulse{ts_ns}`, report clock, flag failure |
| `GpioCdevSource` | real GPIO driver client |
| `SimSource` | synthetic load, virtual clock, ground-truth energy |
| `SpscRing<T,N>` | wait-free hand-off between source thread and analytics |
| `Analytics` | debounce, power, energy, EWMA, anomaly, TOU cost, minute buckets, thread-safe snapshot |
| `StateMachine` | INIT/IDLE/MEASURING/OVERLOAD/FAULT |
| `Storage` | atomic state file (tmp + fsync + rename), CSV log |
| `HttpServer` | REST + Prometheus |
| `smeter-cli` | terminal dashboard |

## 4. Data structures
`Pulse{uint64 ts_ns}` · `Config` (meter constant, thresholds, tariff) · `Bucket{minute,wh,peak_w,cost,pulses}` ·
`Snapshot` (read-only view for API) · `PersistedState{pulses,energy_wh,cost}` · sliding window `deque<double>` (50 samples) ·
history `deque<Bucket>` (1440 = 24 h).

**Formulas:** `E_pulse = 1000/imp_per_kWh` Wh · `P = 3.6e6 / (imp_per_kWh · Δt)` W.

## 5. Class diagram
```mermaid
classDiagram
  class PulseSource{<<interface>> +start(PulseQueue&) bool +stop() +name() string +now_ns() uint64 +wall_start_s() double +failed atomic_bool}
  class GpioCdevSource{-chip -line -debounce_us -chip_fd -line_fd -stop_fd -loop()}
  class SimSource{-imp -speed +load_w(t) double +truth_wh() double}
  class SpscRing~T,N~{+push(T) bool +pop() optional~T~ +dropped() size_t}
  class Analytics{+on_pulse(ts) +tick(now) +on_source_error() +snapshot() Snapshot +history(n) vector~Bucket~ +set_sink()}
  class StateMachine{+on_start() +on_power(w) +on_idle_timeout() +on_source_error() +on_recover()}
  class Storage{+save_state() +load_state() +append_bucket()}
  class HttpServer{+start() +stop() +route(path)}
  PulseSource <|-- GpioCdevSource
  PulseSource <|-- SimSource
  PulseSource --> SpscRing : produces
  SpscRing --> Analytics : consumed by
  Analytics *-- StateMachine
  Analytics ..> Storage : bucket sink
  HttpServer --> Analytics : snapshot()
```

## 6. Sequence diagram — one pulse
```mermaid
sequenceDiagram
  participant Meter
  participant Kernel as Kernel GPIO driver
  participant Src as GpioCdevSource thread
  participant Q as SPSC ring
  participant Main as smeterd main loop
  participant An as Analytics
  participant API as HttpServer
  Meter->>Kernel: rising edge (IRQ)
  Kernel->>Kernel: debounce + timestamp, queue event
  Kernel-->>Src: poll() wakes, read(gpio_v2_line_event)
  Src->>Q: push(Pulse ts)
  Main->>Q: pop() every 50 ms
  Main->>An: on_pulse(ts)
  An->>An: interval -> W, EWMA, anomaly, energy, cost, bucket
  An->>An: StateMachine.on_power(W)
  API->>An: snapshot() (mutex)
  An-->>API: Snapshot -> JSON
```

## 7. State machine diagram
```mermaid
stateDiagram-v2
  [*] --> INIT
  INIT --> IDLE: start
  IDLE --> MEASURING: pulse, P <= limit
  IDLE --> OVERLOAD: pulse, P > limit
  MEASURING --> OVERLOAD: P > limit
  OVERLOAD --> MEASURING: P < 0.9 x limit
  MEASURING --> IDLE: idle timeout
  OVERLOAD --> IDLE: idle timeout
  INIT --> FAULT: source error
  IDLE --> FAULT: source error
  MEASURING --> FAULT: source error
  OVERLOAD --> FAULT: source error
  FAULT --> IDLE: pulse received (recover)
```

## 8. Threading model
Source thread (producer) → lock-free ring → main thread (consumer, 50 ms cadence) → `Analytics` (mutex) ← HTTP thread (reader). The source thread never blocks; overflow increments a drop counter.

## 9. Implementation plan, environment, Git strategy
* Tools: g++ ≥ 9 (C++17), GNU make, Linux kernel headers (`linux/gpio.h`), optional `gpio-sim`, curl, ASan/UBSan/TSan.
* Setup: `sudo apt install build-essential linux-libc-dev curl` → `make`.
* Branching: `main` (stable, tagged) ← `develop` ← `feature/<name>`; PRs must pass CI (`.github/workflows/ci.yml`). Conventional commits.
* Tracking: `docs/PROGRESS.md` (log of issues and solutions), GitHub Issues/Projects per PRD requirement ID.

## 10. Smart Energy Agent extension (v2.0)
See [SMART_AGENT.md](SMART_AGENT.md). New modules (all header-only, no change to the existing pipeline except one optional hook):
`calendar`, `tariff`, `slab_monitor`, `appliance`, `usage_tracker`, `smart_bill`, `recommendations`, `energy_agent`, `agent_json`.
```mermaid
classDiagram
  class EnergyAgent{+report(now,power) AgentReport +whatIf(name,hours,now) WhatIf}
  class UsageTracker{+add(wall,wh) +today() +monthToDate() +baseline() +peakPeriod()}
  class SlabTariff{slabs fixed_charge currency}
  class Appliance{name watts hours_per_day days_per_month windows}
  class ApplianceLoadModel{+operator()(wall) double}
  EnergyAgent --> UsageTracker
  EnergyAgent --> SlabTariff
  EnergyAgent o-- Appliance
  ApplianceLoadModel o-- Appliance
  Analytics ..> UsageTracker : energy hook
  SimSource ..> ApplianceLoadModel : load model
  HttpServer --> EnergyAgent
```
