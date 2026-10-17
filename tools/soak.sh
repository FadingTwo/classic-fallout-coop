#!/usr/bin/env bash
# Round-the-clock (or nightly) testing. Each run draws at random:
#   - what: the tour of every map, the session test (join, leave, rejoin,
#     load, trade, a fight with the turn timer) or a fight with chat;
#   - how: direct, through a local relay, through a slow relay (a line of
#     150-600 KB/s), the address sanitizer, or a Windows player 2 (Wine);
#   - screen sizes for both players, and a seed (map order, where player 2
#     walks).
# One line per run in <out>/summary.log, with everything needed to repeat
# it; failed runs keep their logs in <out>/fail-<n>, passed ones are removed.
#
#   nice tools/soak.sh <out dir>
#
# SOAK_UNTIL=HH:MM stops starting runs at that time (the next time it comes),
# SOAK_RUNS=n after n runs. Needs FALLOUT_DATA (see regress.sh). Optional:
# SOAK_ASAN_BIN, SOAK_WINE (a Wine runner script for the Windows build),
# DISPLAY for Wine. For Fallout 2 co-op: SOAK_BIN (its fallout2-ce),
# SOAK_TOOLS (its tools folder, for nettest.sh) and SOAK_WHAT (the tests to
# draw from, e.g. "tour session play"). SOAK_PORT_OFFSET moves the local
# relay servers' ports, so a second soak can run next to another.
set -uo pipefail

here=$(cd "$(dirname "$0")" && pwd)
out=$1
mkdir -p "$out"
bin=${SOAK_BIN:-$here/../build/fallout-ce}
tools=${SOAK_TOOLS:-$here}
read -r -a whats <<<"${SOAK_WHAT:-tour tour4 session session combat talk play}"

until_epoch=0
if [ -n "${SOAK_UNTIL:-}" ]; then
    until_epoch=$(date -d "${SOAK_UNTIL}" +%s)
    [ "$until_epoch" -le "$(date +%s)" ] && until_epoch=$(date -d "tomorrow ${SOAK_UNTIL}" +%s)
fi

offset=${SOAK_PORT_OFFSET:-0}
relay_port=$((27980 + offset))
slow_port=$((27981 + offset))
python3 "$here/coop_server.py" --port $((18090 + offset)) --bind 127.0.0.1 --root "$out/server" \
    --relay-port $relay_port --relay-bind 127.0.0.1 >"$out/server.log" 2>&1 &
server=$!
slow_server=
trap 'kill $server $slow_server 2>/dev/null' EXIT

start_slow_server() {
    kill $slow_server 2>/dev/null
    COOP_RELAY_TEST_RATE=$1 python3 "$here/coop_server.py" --port $((18091 + offset)) --bind 127.0.0.1 --root "$out/server-slow" \
        --relay-port $slow_port --relay-bind 127.0.0.1 >"$out/server-slow.log" 2>&1 &
    slow_server=$!
    sleep 1
}
sleep 1

# (1280x800: the Steam Deck.)
sizes=(640x480 800x600 1024x768 1280x720 1280x800 1366x768 1920x1080)
pick() { local list=("$@"); echo "${list[$((RANDOM % ${#list[@]}))]}"; }

n=$(grep -c " run " "$out/summary.log" 2>/dev/null || echo 0)
runs=0
while true; do
    [ -n "${SOAK_RUNS:-}" ] && [ $runs -ge "$SOAK_RUNS" ] && break
    [ "$until_epoch" -gt 0 ] && [ "$(date +%s)" -ge "$until_epoch" ] && break
    n=$((n + 1))
    runs=$((runs + 1))

    seed=$((RANDOM * 32768 + RANDOM))
    RANDOM=$seed
    what=$(pick "${whats[@]}")
    how=$(pick direct relay relay slow asan wine)
    # Four players under the address sanitizer are four slow processes at
    # once: too heavy for a shared server.
    [ "$what" = tour4 ] && [ "$how" = asan ] && how=direct
    hostRes=$(pick "${sizes[@]}")
    clientRes=$(pick "${sizes[@]}")
    rate=0

    hostBin=$bin
    clientBin=$bin
    # COOP_FRAME_CHECK: every picture a player shows is checked against the host's.
    extra=(COOP_AUTOTEST_SEED=$seed ASAN_OPTIONS=detect_leaks=0 COOP_FRAME_CHECK=1)
    # Every third run checks the engine heap after each heap call.
    heapCheck=$((RANDOM % 3 == 0))
    [ $heapCheck = 1 ] && extra+=(COOP_HEAP_CHECK=1)
    # Every other run compares the host's pictures drawn from the last one
    # (and player 1's view restored after the others' parts) with a full
    # redraw: a missed change shows as many pixels.
    viewCheck=$((RANDOM % 2 == 0))
    [ $viewCheck = 1 ] && extra+=(COOP_HOST_VIEW_CHECK=1 COOP_HOST_RESTORE_CHECK=1)
    case $how in
    asan)
        # Engine heap blocks from malloc, so ASAN sees writes past them.
        if [ -z "${SOAK_ASAN_BIN:-}" ]; then how=direct; else hostBin=$SOAK_ASAN_BIN; clientBin=$SOAK_ASAN_BIN; extra+=(COOP_HEAP_SYSTEM=1); fi
        ;;
    wine)
        if [ -z "${SOAK_WINE:-}" ]; then how=direct; else clientBin=$SOAK_WINE; fi
        ;;
    relay)
        extra+=(COOP_AUTOTEST_RELAY=http://127.0.0.1:$relay_port/fallout-coop/relay CLIENT_EXTRA="[coop]host=192.0.2.1")
        ;;
    slow)
        rate=$(((RANDOM % 10 + 3) * 50000))
        start_slow_server $rate
        extra+=(COOP_AUTOTEST_RELAY=http://127.0.0.1:$slow_port/fallout-coop/relay CLIENT_EXTRA="[coop]host=192.0.2.1")
        ;;
    esac
    extra+=(COOP_AUTOTEST_RELAY_FILE="$out/code-$n.txt")

    case $what in
    tour) scripts=(net_tour_host net_tour_client); extra+=(COOP_AUTOTEST_TOUR=ALL COOP_AUTOTEST_ROUGH=1 HOST_EXTRA="[coop]turn_time=10") ;;
    session) scripts=(net_session_host net_session_client); extra+=(HOST_EXTRA="[coop]turn_time=3") ;;
    combat) scripts=(net_combat_host net_combat_client); extra+=(COOP_AUTOTEST_COMBAT_CHAT=1) ;;
    talk) scripts=(net_talk_host net_talk_client) ;;
    play) scripts=(net_play_host net_play_client) ;;
    barter) scripts=(net_talk_host net_talk_client); extra+=(COOP_AUTOTEST_NET_BARTER=1 HOST_EXTRA="[coop]talk=0") ;;
    tour4) scripts=(net_tour_host net_tour_client); extra+=(COOP_AUTOTEST_PLAYERS=4 EXTRA_CLIENTS=2 COOP_AUTOTEST_TOUR=ALL COOP_AUTOTEST_ROUGH=1 HOST_EXTRA="[coop]turn_time=10") ;;
    esac

    dir="$out/run-$n"
    rm -rf "$dir"
    start=$(date +%s)
    env "${extra[@]}" CLIENT_BIN="$clientBin" HOST_RES="$hostRes" CLIENT_RES="$clientRes" \
        AUTOTEST_TIMEOUT=10800 HOST_WAIT=20 \
        "$tools/nettest.sh" "$hostBin" "$dir" "${scripts[@]}" >"$dir.txt" 2>&1
    seconds=$(($(date +%s) - start))

    host=$(grep -h "result:" "$dir/host/autotest/result.txt" 2>/dev/null | tail -1)
    client=$(grep -h "result:" "$dir/client/autotest/result.txt" 2>/dev/null | tail -1)
    maps=$(grep -h "visited" "$dir/host/autotest/result.txt" 2>/dev/null | tail -1)
    asan=$(grep -l "ERROR: AddressSanitizer" "$dir"/*/stdout.log 2>/dev/null | wc -l)
    # A cache that can't get a block (full of blocks in use): many of these
    # mean the host thrashes its caches, slow for real players too.
    heap=$(grep -ac "Could not allocate block" "$dir/host/debug.log" 2>/dev/null)
    pictures=$(grep -ac "differs from the host's" "$dir/host/debug.log" 2>/dev/null)
    # The most pixels a picture from the last one or a restored view
    # differed from a full redraw by (a few: the engine draws a part a
    # pixel differently here and there than a whole, in single player too).
    drawn=$(grep -aoh "([0-9]* pixels at most)" "$dir/host/debug.log" 2>/dev/null | tr -dc '0-9\n' | sort -n | tail -1)
    status=PASS
    if [[ "$host" != *"PASS"* || "$client" != *"PASS"* || "$asan" != "0" || "${pictures:-0}" != "0" || "${drawn:-0}" -gt ${SOAK_DRAWN_MAX:-100} ]]; then
        status=FAIL
    fi
    # Players 3 and 4's clients (tour4) count too: a hung one has no result.
    for extra in "$dir"/client[2-9]; do
        [ -d "$extra" ] || continue
        if ! grep -qh "result: PASS" "$extra/autotest/result.txt" 2>/dev/null; then
            status=FAIL
            echo "    $(basename "$extra"): no PASS" >>"$out/summary.extra"
        fi
    done

    line="run $n $what $how"
    [ "$how" = slow ] && line="$line ${rate}B/s"
    [ $heapCheck = 1 ] && line="$line heapcheck"
    [ $viewCheck = 1 ] && line="$line viewcheck"
    echo "$(date '+%F %T') $line $hostRes/$clientRes seed $seed: $status (${maps:-$what}, ${seconds}s, asan errors $asan, heap warnings ${heap:-0}, wrong pictures ${pictures:-0}$([ $viewCheck = 1 ] && echo ", redraw differences ${drawn:-0} px at most"))" >>"$out/summary.log"
    if [ $status = FAIL ]; then
        grep -h "FAIL" "$dir"/*/autotest/result.txt 2>/dev/null | head -5 | sed 's/^/    /' >>"$out/summary.log"
        grep -h "HEAP CHECK" "$dir"/*/stdout.log 2>/dev/null | head -3 | sed 's/^/    /' >>"$out/summary.log"
        grep -ah "differs from the host's" "$dir/host/debug.log" 2>/dev/null | head -2 | sed 's/^/    /' >>"$out/summary.log"
        grep -ah "differs from a full redraw in" "$dir/host/debug.log" 2>/dev/null | sort -t' ' -k10 -n | tail -2 | sed 's/^/    /' >>"$out/summary.log"
        mv "$dir" "$out/fail-$n"
        mv "$dir.txt" "$out/fail-$n.txt" 2>/dev/null
        # Keep the newest SOAK_KEEP_FAILS (30) failures' logs.
        ls -d "$out"/fail-[0-9]* 2>/dev/null | grep -v '\.txt$' | sort -V | head -n -"${SOAK_KEEP_FAILS:-30}" | while read -r old; do
            rm -rf "$old" "$old.txt" "$old.host.txt"
        done
    else
        rm -rf "$dir" "$dir.txt" "$dir.host.txt"
    fi
done
