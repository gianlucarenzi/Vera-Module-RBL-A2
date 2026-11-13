#!/bin/sh
# Stage 3 after tools/route.sh: when connections are still unrouted, route
# them using the POWER plane (3V3) as a signal layer too. Only use it when
# it is really needed.
#
# usage: tools/route_power_fallback.sh WORKDIR
#   WORKDIR is the one given to tools/route.sh, which must have finished:
#   board_pv.dsn, board.ses and report.json are read from it. The result is
#   WORKDIR/final_power.ses, to import in pcbnew 5 (File > Import > Specctra
#   Session) instead of final.ses. Live view on http://127.0.0.1:7878
#
#   POWER_COST   trace cost on POWER, compared with 1 on F.Cu and B.Cu
#                (default 25: POWER only where the outer layers have no room)
#   MAX_TIME     seconds (default 7200); FASTROUTE=tools/fastroute_nolimits.sh
#                for no limit at all
#
# How it works:
#   - start from the stage 2 session of tools/route.sh: everything already
#     routed stays;
#   - POWER becomes a signal layer again, with its 3V3 plane still in place:
#     fastroute does not treat the planes of signal layers as obstacles for
#     the other nets (plane_as_obstacle=false), so the traces cross it and 3V3
#     stays connected through the plane vias;
#   - GND stays a pure plane: it is the reference of B.Cu and is never cut.
#
# After the import:
#   - refill the zones (B): the 3V3 zone flows around the new traces;
#   - in the DRC, check that no 3V3 pad or via was left on an island;
#   - the stack is F.Cu, POWER, GND, B.Cu: POWER is the reference plane of
#     F.Cu, so a POWER trace under fast F.Cu signals (buses, VGA) breaks their
#     return path. Check where the POWER traces went (printed at the end) and
#     move them by hand where needed.
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd)
WORK=${1:?usage: tools/route_power_fallback.sh WORKDIR}
C=${POWER_COST:-25}
MAXT=${MAX_TIME:-7200}
FR=${FASTROUTE:-$(command -v fastroute || true)}
if [ -z "$FR" ] || [ ! -x "$FR" ]; then
	echo "fastroute is not installed, see tools/route.sh" >&2
	exit 1
fi
cd "$WORK"
for f in board_pv.dsn board.ses report.json; do
	[ -f "$f" ] || { echo "$WORK/$f is missing: run tools/route.sh first" >&2; exit 1; }
done
LEFT=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["stats"]["unrouted"])' report.json)
echo "after tools/route.sh: $LEFT connections unrouted"
[ "$LEFT" -gt 0 ] || { echo "nothing to route: final.ses is the result"; exit 0; }

# POWER back to a signal layer (its 3V3 plane stays); GND stays a plane
python3 - board_pv.dsn board_power.dsn <<'EOF'
import re, sys
t = open(sys.argv[1]).read()
t, n = re.subn(r'(\(layer POWER\s*\(type )power\)', r'\1signal)', t)
assert n == 1, 'layer POWER not found as a power layer'
assert '(plane 3V3 (polygon POWER ' in t, 'no 3V3 plane on POWER'
open(sys.argv[2], 'w').write(t)
EOF
# the plane vias are fixed in the DSN: drop them from the starting session
python3 "$TOOLS/strip_protect_ses.py" board.ses stage2_np.ses

# layers F.Cu, POWER, GND, B.Cu (GND is a plane: its cost is not used)
COSTS="1,$C,$C,1"
"$FR" -de board_power.dsn -do power.ses --initial-session=stage2_np.ses \
	--report=report_power.json --diagnose --max-time="$MAXT" \
	--router.min_trace_width_um=200 --router.plane_nets=GND,3V3 \
	--router.fanout.enabled=false --no-neckdown-classes=90R \
	--router.layers.preferred_direction_trace_cost="$COSTS" \
	--router.layers.undesired_direction_trace_cost="$COSTS" --live > run_power.log 2>&1

LEFT2=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["stats"]["unrouted"])' report_power.json)
echo "with POWER at cost $C: $LEFT2 connections unrouted (were $LEFT)"
if [ "$LEFT2" -ge "$LEFT" ]; then
	echo "POWER did not help: keep final.ses"
	exit 0
fi
python3 "$TOOLS/dedupe_ses.py" power.ses final_power.ses
python3 "$TOOLS/ses_narrow.py" final_power.ses
python3 - final_power.ses <<'EOF'
import re, sys
t = open(sys.argv[1]).read()
unit, res = re.search(r'\(resolution (\w+) (\d+)\)', t).groups()
res = float(res) * {'um': 1000.0, 'mm': 1.0, 'mil': 1 / 0.0254, 'inch': 1 / 25.4}[unit]  # units per mm


def scope_end(s, i):
    d = 0
    for k in range(i, len(s)):
        d += {'(': 1, ')': -1}.get(s[k], 0)
        if d == 0:
            return k + 1


n = 0
for m in re.finditer(r'\(net ("[^"]*"|[^\s()]+)', t):
    body = t[m.start():scope_end(t, m.start())]
    for w in re.finditer(r'\(path POWER \S+((?:\s+-?[\d.]+)+)\s*\)', body):
        p = [float(v) / res for v in w.group(1).split()]
        # session y grows upwards, KiCad y downwards
        pts = ' -> '.join('(%.2f, %.2f)' % (p[i], -p[i + 1]) for i in range(0, len(p), 2))
        print('  POWER trace, net %s: %s mm' % (m.group(1).strip('"'), pts))
        n += 1
print('%d traces on POWER: refill the zones and check 3V3 for islands in the DRC' % n)
EOF
echo "session: $WORK/final_power.ses; still unrouted (with diagnosis): $WORK/report_power.json"
