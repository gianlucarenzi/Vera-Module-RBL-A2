#!/bin/sh
# Route the board from scratch with no time limit: tools/route.sh (planes,
# plane vias, buses first, then everything else), then, when connections
# are still unrouted, tools/route_power_fallback.sh (POWER also used as a
# signal layer, at a high cost).
#
# usage: tools/route_nolimits.sh WORKDIR
#   before: zones filled (B) and saved, File > Export > Specctra DSN to
#   VERA-MODULE-RBL.dsn, KiCad closed.
#   Live view on http://127.0.0.1:7878 (each stage restarts the viewer:
#   reload the page).
#   POWER_FALLBACK=0 skips stage 3; POWER_COST sets its trace cost (25).
#
# Result: WORKDIR/final_power.ses if stage 3 ran and helped, otherwise
# WORKDIR/final.ses. Import it in pcbnew 5 (File > Import > Specctra
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
	echo "result: $WORK/final_power.ses"
else
	echo "result: $WORK/final.ses"
fi
