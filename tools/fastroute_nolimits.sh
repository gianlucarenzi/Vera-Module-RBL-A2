#!/bin/sh
# fastroute with no time limit: runs until it finishes on its own.
#
# The routing scripts (tools/route.sh, tools/route_power_fallback.sh,
# tools/route_incremental.sh) take the fastroute binary from $FASTROUTE.
# Point it at this wrapper:
#   - every --max-time=N becomes 10 years;
#   - --no-time-limits is added, which turns off the job timeout (otherwise
#     12 h by default, 24 h at most) and the wall-clock limits of each stage.
# Ctrl+C or SIGTERM still stop the run, and the best result so far is
# written; a second one exits at once.
#
# usage: FASTROUTE=tools/fastroute_nolimits.sh tools/route.sh WORKDIR
#   FASTROUTE_REAL=/path/to/fastroute selects the real binary
#   (default: fastroute in the PATH)
FR=${FASTROUTE_REAL:-$(command -v fastroute || true)}
if [ -z "$FR" ] || [ ! -x "$FR" ]; then
	echo "fastroute not found (set FASTROUTE_REAL)" >&2
	exit 1
fi
n=$#
while [ "$n" -gt 0 ]; do
	a=$1
	shift
	n=$((n - 1))
	case "$a" in
	--max-time=*) a=--max-time=315360000 ;;
	esac
	set -- "$@" "$a"
done
exec "$FR" "$@" --no-time-limits
