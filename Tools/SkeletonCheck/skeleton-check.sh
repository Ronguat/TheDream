#!/usr/bin/env bash
# skeleton-check.sh -- what sits on which skeleton, and how a migrated pack joins the master.
#
# The project runs two skeletons on purpose: SK_Master carries the consolidated packs and the
# character mesh, and SwordShield's SK_Mannequin carries the vendor library, reached by one
# compatible entry on the master. That split is cheap to keep and invisible to read, which
# is what this exists for -- a pack migrated from AnimLibrary lands on a third skeleton and
# nothing announces it.
#
#   ./Tools/SkeletonCheck/skeleton-check.sh                 # report the distribution and any drift
#   ./Tools/SkeletonCheck/skeleton-check.sh --fold <path>   # consolidate one skeleton into the master
#
# A migrated pack does NOT have to be folded. Adding it to the master's compatible list is
# enough to make its clips playable; folding is how it stops being a separate skeleton.
#
# Exit 0 = no drift, 1 = drift found or the fold failed, 2 = usage.
# Needs the editor open: it drives Python through Tools/AnimPipeline/run-in-editor.py.

set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PY="/c/Program Files (x86)/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
RUNNER="$ROOT/Tools/AnimPipeline/run-in-editor.py"
MODULE="$(cygpath -m "$ROOT/Tools/SkeletonCheck/skeleton_check.py")"

case "${1:-}" in
  "")      CALL="report()" ;;
  --fold)  [ $# -ge 2 ] || { echo "skeleton-check: --fold needs a skeleton path" >&2; exit 2; }
           CALL="fold(\"$2\")" ;;
  *)       echo "usage: skeleton-check.sh [--fold /Game/Path/SK_Something]" >&2; exit 2 ;;
esac

SCRIPT="$(mktemp --suffix=.py)"
trap 'rm -f "$SCRIPT"' EXIT
cat > "$SCRIPT" <<EOF
import importlib.util
spec = importlib.util.spec_from_file_location("skeleton_check", r"$MODULE")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
print("SKELETON-CHECK-RESULT %d" % (mod.$CALL or 0))
EOF

OUT="$("$PY" "$RUNNER" "$SCRIPT" --timeout 300 2>&1)"
echo "$OUT" | sed 's/^\[Info\] //'
echo "$OUT" | grep -q "SKELETON-CHECK-RESULT 0" && exit 0
exit 1
