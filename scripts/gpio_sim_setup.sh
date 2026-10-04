#!/usr/bin/env bash
# Creates a virtual GPIO chip (kernel gpio-sim module) so the REAL GPIO driver path can be tested
# without hardware. Needs root + Linux >= 5.19 (CONFIG_GPIO_SIM). Tooling only; all project code is C++.
set -euo pipefail
sudo modprobe gpio-sim
CFG=/sys/kernel/config/gpio-sim/smeter
sudo mkdir -p $CFG/bank0
echo 4 | sudo tee $CFG/bank0/num_lines >/dev/null
echo "pulse-in" | sudo tee $CFG/bank0/line0/name >/dev/null 2>&1 || { sudo mkdir -p $CFG/bank0/line0; echo pulse-in | sudo tee $CFG/bank0/line0/name >/dev/null; }
echo 1 | sudo tee $CFG/live >/dev/null
CHIP=$(basename "$(ls -d /sys/devices/platform/gpio-sim.*/gpiochip* | head -1)")
PULL=$(ls /sys/devices/platform/gpio-sim.*/$CHIP/sim_gpio0/pull)
echo "chip: /dev/$CHIP  line: 0"
echo "inject: sudo ./build/pulse-injector --pull $PULL --watts 3600 --count 30"
echo "run   : sudo ./build/smeterd --gpio /dev/$CHIP 0"
