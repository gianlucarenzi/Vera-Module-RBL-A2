#!/bin/sh
# Last optimization after routing: fix the clearance violations.
#
# usage: tools/repair_violations.sh DSN IN.ses OUT.ses [MAX_ROUNDS]
#   DSN: the one used for routing (WORKDIR/board_pv.dsn, or board_power.dsn
#   after tools/route_power_fallback.sh). Work files go next to OUT.ses.
#   MAX_ROUNDS default 5; MAX_TIME seconds per reroute (default 3600,
#   unlimited with FASTROUTE=tools/fastroute_nolimits.sh); LIVE=0 without
#   the live view (http://127.0.0.1:7878).
#
# Each round:
#   1. load the session in fastroute without routing (autorouter off) and
#      write the report: unrouted connections and clearance
#      violations, measured by fastroute;
#   2. tools/ses_violations.py sorts them: same net and rounding (within
#      0.002 mm) are ignored, the real ones with a movable trace or via get
#      that copper ripped up, the real ones between fixed items (pads, plane
#      vias) are listed to fix by hand;
#   3. fastroute routes the ripped connections again (the rest of the
#      session stays), then tools/dedupe_ses.py and tools/chamfer_ses.py;
#   4. the round is kept only if the real violations went down and the
#      unrouted connections did not go up; otherwise the previous session
#      is the result.
# KiCad's DRC stays the reference after the import.
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd)
DSN=$(cd "$(dirname "${1:?usage: tools/repair_violations.sh DSN IN.ses OUT.ses [MAX_ROUNDS]}")" && pwd)/$(basename "$1")
IN=$(cd "$(dirname "${2:?}")" && pwd)/$(basename "$2")
OUT=$(cd "$(dirname "${3:?}")" && pwd)/$(basename "$3")
ROUNDS=${4:-5}
MAXT=${MAX_TIME:-3600}
FR=${FASTROUTE:-$(command -v fastroute || true)}
if [ -z "$FR" ] || [ ! -x "$FR" ]; then
	echo "fastroute is not installed, see tools/route.sh" >&2
	exit 1
fi
# absolute: the script changes directory below
case $FR in /*) ;; *) FR=$(pwd)/$FR ;; esac
L=--live
[ "${LIVE:-1}" = 0 ] && L=
cd "$(dirname "$OUT")"
P=repair
COMMON="--router.min_trace_width_um=200 --router.plane_nets=GND,3V3 --no-neckdown-classes=90R --router.fanout.enabled=false"

check() {   # check SES JSON: report of the session as it is
	# autorouter, optimizer and fanout off: fastroute only loads the session
	# and measures it (ignoring every net class is not enough: it still
	# routed some connections, and ripped others up to make room)
	python3 "$TOOLS/strip_protect_ses.py" "$1" "$1.np" > /dev/null
	"$FR" -de "$DSN" -do "$1.chk" --initial-session="$1.np" --report="$2" --max-time=600 \
		--router.autorouter.enabled=false --router.optimizer.enabled=false \
		--multi-start=1 $COMMON > "$2.log" 2>&1
}
unrouted() {
	python3 -c 'import json, sys; s = json.load(open(sys.argv[1]))["stats"]; print(s["unrouted"] + (s.get("unrouted_ignored_classes") or 0))' "$1"
}
real() {    # number of real violations in the output of ses_violations.py
	awk '/^real, (movable|fixed)/ {n += $NF} END {print n + 0}' "$1"
}

cp "$IN" "${P}_0.ses"
CUR=${P}_0.ses
check "$CUR" "${P}_0.json"
U=$(unrouted "${P}_0.json")
python3 "$TOOLS/ses_violations.py" "$DSN" "$CUR" "${P}_0.json" "${P}_1_ripped.ses" > "${P}_0.txt"
V=$(real "${P}_0.txt")
echo "start: $U unrouted connections, $V real clearance violations"
r=1
while [ "$r" -le "$ROUNDS" ] && [ "$V" -gt 0 ]; do
	n=$(awk '/^RIPPED/ {print $2}' "${P}_$((r - 1)).txt")
	if [ "$n" -eq 0 ]; then
		echo "round $r: only violations between fixed items left"
		break
	fi
	python3 "$TOOLS/strip_protect_ses.py" "${P}_${r}_ripped.ses" "${P}_${r}_np.ses" > /dev/null
	"$FR" -de "$DSN" -do "${P}_${r}_routed.ses" --initial-session="${P}_${r}_np.ses" \
		--report="${P}_${r}_routed.json" --max-time="$MAXT" $COMMON $L > "${P}_${r}.log" 2>&1
	python3 "$TOOLS/dedupe_ses.py" "${P}_${r}_routed.ses" "${P}_${r}.ses" > /dev/null
	python3 "$TOOLS/chamfer_ses.py" "$DSN" "${P}_${r}.ses" "${P}_${r}.ses" > "${P}_${r}_chamfer.txt"
	check "${P}_${r}.ses" "${P}_${r}.json"
	U2=$(unrouted "${P}_${r}.json")
	python3 "$TOOLS/ses_violations.py" "$DSN" "${P}_${r}.ses" "${P}_${r}.json" "${P}_$((r + 1))_ripped.ses" > "${P}_${r}.txt"
	V2=$(real "${P}_${r}.txt")
	echo "round $r: ripped $n items -> $U2 unrouted, $V2 real violations"
	if [ "$V2" -ge "$V" ] || [ "$U2" -gt "$U" ]; then
		echo "round $r did not help: keeping the previous session"
		break
	fi
	CUR=${P}_${r}.ses
	U=$U2
	V=$V2
	r=$((r + 1))
done
cp "$CUR" "$OUT"
echo "result: $OUT ($U unrouted, $V real violations)"
# the final list: what is left to fix by hand
python3 "$TOOLS/ses_violations.py" "$DSN" "$CUR" "${CUR%.ses}.json" /dev/null | grep -v '^RIPPED'
python3 "$TOOLS/ses_narrow.py" "$OUT"
