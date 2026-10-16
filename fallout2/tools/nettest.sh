#!/usr/bin/env bash
# Runs a co-op network test: a host and a client process on this machine,
# both headless, talking over localhost.
#
#   tools/nettest.sh <fallout-ce binary> <out dir> [host script] [client script] [--tamper]
#
# Scripts default to net_host / net_client (see coop_autotest.cc). With
# --tamper the client's game data differs: since 1.13.3 the host lets them
# in (as a new character; CLIENT_EXTRA="[coop]test_game=1" pretends to be Fallout 1,
# which the host refuses).
# Needs FALLOUT_DATA, like tools/regress.sh. CLIENT_BIN runs the client
# with another binary (e.g. a Windows build under Wine), HOST_WAIT sets the
# seconds to wait for the host (default 4), HOST_EXTRA adds host settings
# (e.g. "[coop]turn_time=3"), CLIENT_EXTRA client settings, HOST_RES and
# CLIENT_RES screen sizes (e.g. 1024x768). EXTRA_CLIENTS=n starts n more
# clients (players 3 and up, in <out dir>/client2...) with the same script
# (EXTRA_CLIENT_BIN: their binary, default CLIENT_BIN).

set -uo pipefail

here=$(dirname "$0")
bin=$1
out=$2
host_script=${3:-net_host}
client_script=${4:-net_client}
tamper=
[ "${5:-}" = "--tamper" ] && tamper=1

port=$((27100 + RANDOM % 500))

AUTOTEST_RES=${HOST_RES:-} AUTOTEST_SCRIPT=$host_script "$here/regress.sh" run "$bin" "$out/host" \
    "[coop]enabled=1" "[coop]mode=host" "[coop]port=$port" ${HOST_EXTRA:-} >"$out.host.txt" 2>&1 &
host_pid=$!

# The host computes its data checksum before it listens.
sleep "${HOST_WAIT:-4}"

extra_pids=()
for n in $(seq 2 $((${EXTRA_CLIENTS:-0} + 1))); do
    AUTOTEST_RES=${CLIENT_RES:-} AUTOTEST_SCRIPT=$client_script "$here/regress.sh" run "${EXTRA_CLIENT_BIN:-${CLIENT_BIN:-$bin}}" "$out/client$n" \
        "[coop]enabled=1" "[coop]mode=client" "[coop]host=127.0.0.1" "[coop]port=$port" ${CLIENT_EXTRA:-} >"$out.client$n.txt" 2>&1 &
    extra_pids+=($!)
    sleep "${CLIENT_STAGGER:-3}"
done

AUTOTEST_RES=${CLIENT_RES:-} AUTOTEST_TAMPER=$tamper AUTOTEST_SCRIPT=$client_script "$here/regress.sh" run "${CLIENT_BIN:-$bin}" "$out/client" \
    "[coop]enabled=1" "[coop]mode=client" "[coop]host=127.0.0.1" "[coop]port=$port" ${CLIENT_EXTRA:-}
client_rc=$?

for pid in "${extra_pids[@]}"; do
    wait "$pid" || client_rc=1
done

wait $host_pid
host_rc=$?

echo "--- host"
cat "$out.host.txt"
grep -a 'COOP NET' "$out/host/debug.log" "$out/client/debug.log" 2>/dev/null | sed "s|$out/||"

[ $client_rc -eq 0 ] && [ $host_rc -eq 0 ]
