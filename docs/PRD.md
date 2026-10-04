# Project Requirements Document (PRD)

**Product:** Smart Energy Smart-Meter Pulse Counter & Analytics Agent (`smeterd`)  **Version:** 1.0

## 1. Purpose
Convert the pulse output (LED / S0 contact) of a legacy energy meter into trustworthy energy, power, cost and
anomaly information, exposed through a local API and terminal dashboard.

## 2. Scope
| In scope | Out of scope |
|---|---|
| Pulse capture via Linux GPIO char-device driver (+ simulator) | Writing a kernel-space module (kernel is C; project is C++ only) |
| Debounce, pulse->power/energy conversion, TOU billing | Cloud upload, multi-phase metering |
| Anomaly + overload detection, state machine | Billing-grade certification |
| Persistence, REST/Prometheus API, CLI dashboard | Authentication/TLS (bind to localhost by default) |

## 3. Functional requirements
| ID | Requirement | Verified by |
|---|---|---|
| FR-1 | Capture rising edges from a GPIO line with kernel timestamps | `GpioCdevSource`, gpio-sim test |
| FR-2 | Provide a simulator source behind the same interface | `SimSource`, integration test |
| FR-3 | Reject contact bounce (< configurable ms) | unit: debounce |
| FR-4 | Energy = pulses / imp-per-kWh; power = 3.6e6/(imp*dt) | unit: energy, power |
| FR-5 | EWMA-smoothed power, peak demand tracking | unit |
| FR-6 | Time-of-use cost (off-peak/normal/peak) | unit: TOU |
| FR-7 | One-minute aggregates, CSV log | unit + system test |
| FR-8 | Detect spikes (mean + k·sigma) and overload (with hysteresis) | unit |
| FR-9 | Life-cycle state machine INIT/IDLE/MEASURING/OVERLOAD/FAULT | unit |
| FR-10 | Counter survives restart/crash (atomic write) | unit + system test |
| FR-11 | HTTP API: `/api/status`, `/api/history?n=`, `/metrics`, `/healthz` | unit + system test |
| FR-12 | Terminal dashboard client `smeter-cli` | system test |

## 4. Non-functional requirements
| ID | Requirement | Target |
|---|---|---|
| NFR-1 | Language / OS | C++17 only, Linux only |
| NFR-2 | Accuracy | energy error < 2 pulses vs ground truth (met: <1 Wh) |
| NFR-3 | No pulse loss at 10 kW (≈ 2.8 pulses/s at 1000 imp/kWh) and bursts up to 4096 queued | lock-free SPSC ring, dropped counter |
| NFR-4 | Timestamp jitter independent of user-space scheduling | kernel-side timestamps |
| NFR-5 | Reliability | atomic persistence, FAULT state, graceful SIGTERM |
| NFR-6 | Resource use | < 5 MB RSS, < 1 % CPU on a Raspberry Pi class device |
| NFR-7 | Quality | -Wall -Wextra -Wpedantic clean; ASan/UBSan/TSan clean |
| NFR-8 | Portability | no third-party libraries |

## 5. Deliverables
Source (`include/`, `src/`), tests (`tests/`, `scripts/system_test.sh`), docs (`docs/`), README, CI workflow.

## 6. Development plan & timeline (6 weeks)
| Week | Stage | Work | Output |
|---|---|---|---|
| 1 | 1 | Idea, problem statement, scope | Introduction (README) |
| 2 | 2 | Requirements, PRD, plan | `docs/PRD.md` |
| 3 | 3 | Architecture, UML, structures, repo + CI | `docs/ARCHITECTURE.md` |
| 4 | 4 | HAL, ring buffer, analytics, simulator prototype | working `smeterd --sim` |
| 5 | 5 | GPIO driver path, storage, HTTP, tests, sanitizers | `docs/TESTING.md` |
| 6 | 6 | CLI, hardware-in-loop (gpio-sim), polish, demo | v1.0 tag |

## 7. Extension v2.0 - Smart Energy Agent
| ID | Requirement | Verified by |
|---|---|---|
| FR-13 | Slab tariff billing, current slab, units to next slab | `tests/test_agent.cpp` (tariff) |
| FR-14 | Smart Bill Calculator (units, days, optional previous month / daily average) | unit + `/api/bill` |
| FR-15 | Today / yesterday / 7-day / month consumption, peak period, month-end projection and bill | tracker + agent tests |
| FR-16 | Appliance register: watts, hours/day, days/month, monthly kWh and cost | ranking test |
| FR-17 | "What if I save?" using appliance power and the slab tariff | what-if test |
| FR-18 | Slab zones (green/yellow/red), spike detection, trend % | slab + spike tests |
| FR-19 | Data-driven recommendations, savings and daily target | recommendations test |
| FR-20 | Dashboard tabs: Live Meter, Smart Agent, What if I save?, Bill Calculator | system test |
Details: [SMART_AGENT.md](SMART_AGENT.md).
