#!/usr/bin/env bash
# install-hooks.sh -- copies the versioned hooks into this clone's .git/hooks.
#
#   ./Tools/GitHooks/install-hooks.sh
#
# Run once per clone; git never versions .git/hooks, so a fresh clone starts without them.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HOOKS="$(git -C "$ROOT" rev-parse --git-path hooks)"
case "$HOOKS" in /*|[A-Za-z]:*) ;; *) HOOKS="$ROOT/$HOOKS" ;; esac
mkdir -p "$HOOKS"
for h in pre-push; do
  cp "$ROOT/Tools/GitHooks/$h" "$HOOKS/$h" && chmod +x "$HOOKS/$h" && echo "installed $h -> $HOOKS/$h"
done
