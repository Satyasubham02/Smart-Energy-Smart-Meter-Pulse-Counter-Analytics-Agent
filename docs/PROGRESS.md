# Progress log, issues & solutions

| Stage | Done | Issues → solutions |
|---|---|---|
| 1 Intro | Idea, scope (README) | — |
| 2 PRD | Requirements, timeline | Kernel-module vs "C++ only" conflict → use user-space GPIO uAPI + HAL |
| 3 Design | Architecture, UML, repo, CI | Need testing without hardware → `SimSource` + `gpio-sim` |
| 4 Prototype | HAL, ring, analytics, sim, daemon | Sim must run fast → virtual clock with speed factor; pulse timestamps interpolated inside each step |
| 5 Test | 14 tests, sanitizers, system test | Idle meter reports stale power → decay upper bound `3.6e6/(imp·since)`; false spike alarms with tiny σ → also require `P > 1.5·mean` |
| 6 Final | CLI, docs, HIL scripts | — |

| 7 Extension | Smart Energy Agent: tariff, bill calculator, tracker, appliances, what-if, slab zones, spikes, recommendations, 4-tab dashboard | Early-month false "cut 90 units" alarm -> 25 % reach rule; fridge suggested for trimming -> `essential` flag; spike blamed wrong appliance -> match extra kW to wattage; `grep -q` on piped curl failed -> save to file first |
| 8 Interface v3 | Sidebar web UI, power gauge, light/dark toggle, Budget & Carbon tab, colour CLI with agent summary (`--compact`, `--no-color`, `--no-agent`) | `printf` with `std::string` args warned under -Wformat -> pass `.c_str()`; budget allowance must respect slabs -> invert the telescopic tariff instead of dividing by a flat rate |
