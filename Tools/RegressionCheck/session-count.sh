#!/usr/bin/env bash
# Count occurrences of a pattern in the CURRENT PIE session only.
# The log accumulates across sessions while the editor stays up, so counting the
# whole file stops a run early on samples an earlier session produced.
LOG="${2:-Saved/Logs/TheDream.log}"
START=$(grep -n "Bringing World .* up for play" "$LOG" 2>/dev/null | tail -1 | cut -d: -f1)
[ -z "$START" ] && { echo 0; exit 0; }
awk -v s="$START" 'NR>=s' "$LOG" | grep -c "$1" || true
