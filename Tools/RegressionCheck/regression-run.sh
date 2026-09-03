#!/usr/bin/env bash
#
# regression-run.sh -- drive a regression run: preflight, PIE, evaluation.
#
# The documented entry point, so the loop is a shell script like every other tool here. The work
# is in regression_run.py, which needs the engine's interpreter -- there is no other Python on
# this machine.
#
#   ./regression-run.sh --all
#   ./regression-run.sh tier-light chain-early
#   ./regression-run.sh --family knockdown --realtime
#   ./regression-run.sh --all --dry-run        # preflight only, drives no PIE
#
# Exit 0 = every scenario green, 1 = a failure or a timeout, 2 = preflight blocked.
set -uo pipefail
ENGINE_PY="/c/Program Files (x86)/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
[ -x "$ENGINE_PY" ] || { echo "regression-run: no engine Python at $ENGINE_PY" >&2; exit 2; }
exec "$ENGINE_PY" "$(dirname "$0")/regression_run.py" "$@"
