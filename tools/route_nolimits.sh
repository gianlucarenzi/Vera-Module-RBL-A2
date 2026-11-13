#!/bin/sh
# Route the board from scratch with no time limit: tools/route.sh (planes,
# plane vias, buses first, then everything else), then, when connections
# are still unrouted, tools/route_power_fallback.sh (POWER also used as a
# signal layer, at a high cost), and last tools/repair_violations.sh on
# the result (clearance violations ripped up and routed again).
#
# usage: tools/route_nolimits.sh WORKDIR
#   before: zones filled (B) and saved, File > Export > Specctra DSN to
#   VERA-MODULE-RBL.dsn, KiCad closed.
#   Live view on http://127.0.0.1:7878 (each stage restarts the viewer:
#   reload the page).
#   POWER_FALLBACK=0 skips stage 3; POWER_COST sets its trace cost (25).
#   REPAIR=0 skips the violation repair.
#
# Result: WORKDIR/final_repaired.ses (the session of stage 3 if it ran and
# helped, otherwise the one of tools/route.sh, after the repair; without
# the repair WORKDIR/final_power.ses or WORKDIR/final.ses). Import it in pcbnew 5 (File > Import > Specctra
# Session), then B and DRC (see WORKFLOW.md, section 7).
set -e
TOOLS=$(cd "$(dirname "$0")" && pwd)
WORK=${1:?usage: tools/route_nolimits.sh WORKDIR}
FASTROUTE_REAL=${FASTROUTE_REAL:-$(command -v fastroute || true)}
export FASTROUTE_REAL
FASTROUTE="$TOOLS/fastroute_nolimits.sh"
export FASTROUTE

sh "$TOOLS/route.sh" "$WORK"
if [ "${POWER_FALLBACK:-1}" = 1 ]; then
	sh "$TOOLS/route_power_fallback.sh" "$WORK"
fi
if [ -f "$WORK/final_power.ses" ]; then
	DSN=$WORK/board_power.dsn
	RES=$WORK/final_power.ses
else
	DSN=$WORK/board_pv.dsn
	RES=$WORK/final.ses
fi
if [ "${REPAIR:-1}" = 1 ]; then
	sh "$TOOLS/repair_violations.sh" "$DSN" "$RES" "$WORK/final_repaired.ses"
	RES=$WORK/final_repaired.ses
fi
echo "result: $RES"
