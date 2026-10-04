#!/usr/bin/env bash
# End-to-end test: start daemon (simulator), query REST API + CLI, check persistence across restart.
set -euo pipefail
cd "$(dirname "$0")/.."
D=$(mktemp -d); PORT=18099
./build/smeterd --sim --speed 300 --start-hour 17.9 --port $PORT --data-dir "$D" --duration 5 >"$D/log" 2>&1 &
PID=$!
sleep 3
curl -sf localhost:$PORT/healthz | grep -q ok
curl -sf localhost:$PORT/api/status | tee "$D/s1.json" | grep -q '"state":"MEASURING\|"state":"OVERLOAD'
curl -sf localhost:$PORT/metrics | grep -q smeter_energy_kwh_total
./build/smeter-cli --port $PORT --once
# --- Smart Energy Agent API (save to files first: grep -q on a pipe would close curl early)
get() { curl -sf "$1" -o "$D/out.txt"; }
get localhost:$PORT/api/agent;                                   grep -q '"recommendations"' "$D/out.txt"
get localhost:$PORT/api/appliances;                              grep -q 'Air conditioner' "$D/out.txt"
get "localhost:$PORT/api/whatif?appliance=Air%20conditioner&hours=1"; grep -q '"units_saved"' "$D/out.txt"
get "localhost:$PORT/api/bill?units=92&days=27";                 grep -q '"to_next":8.0' "$D/out.txt"
get localhost:$PORT/;                                            grep -q 'id="t-agent"' "$D/out.txt"
echo "agent API OK"
# --- whole-daemon restart: same PID, API comes back, totals restored
P1=$(curl -sf localhost:$PORT/api/status | grep -o '"pulses":[0-9]*' | cut -d: -f2)
test "$(curl -s -o /dev/null -w '%{http_code}' -X POST localhost:$PORT/api/restart)" = 403
curl -sf -X POST -H 'X-Smeter: 1' localhost:$PORT/api/restart | grep -q '"ok":true'
sleep 2
kill -0 $PID                                   # exec keeps the PID
curl -sf localhost:$PORT/api/status > "$D/s2.json"
grep -q 'daemon restarted' "$D/s2.json"
P2=$(grep -o '"pulses":[0-9]*' "$D/s2.json" | cut -d: -f2)
test "$P2" -ge "$P1"                            # totals were restored, not zeroed
echo "daemon restart OK (pulses $P1 -> $P2, pid $PID unchanged)"
# --- counters reset
curl -sf -X POST -H 'X-Smeter: 1' localhost:$PORT/api/reset | grep -q '"ok":true'
sleep 1
curl -sf localhost:$PORT/api/status | grep -q 'counters reset by user'
echo "reset endpoint OK"
wait $PID || true
test -f "$D/smeter.state" && test -f "$D/energy_minutes.csv"
E1=$(grep energy_wh "$D/smeter.state" | cut -d= -f2)
./build/smeterd --sim --speed 300 --port $PORT --data-dir "$D" --duration 1 >"$D/log2" 2>&1
grep -q "restored counter" "$D/log2"
echo "persisted energy: $E1 Wh -- restart restored it"
echo "SYSTEM TEST PASSED"
