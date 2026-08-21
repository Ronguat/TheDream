#!/usr/bin/env bash
# docs-check.sh -- integrity checks for the standing docs, in regression-check's image.
#
# Every serious documentation failure this project has had was a maintenance failure at
# an edit boundary, and most are mechanically checkable. Each check below states the
# invariant it asserts and the failure shape it catches. Judgment stays human -- this
# script owns only what a grep can own.
#
#   ./Tools/DocsCheck/docs-check.sh              # check the repo's standing docs
#   ./Tools/DocsCheck/docs-check.sh --self-test  # prove the instrument can fail
#
# Exit 0 = all checks passed (WARNs allowed), 1 = a check failed, 2 = usage.
# Run at closedown step 3, and after any edit that moves text between docs.

set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 2

STANDING_DOCS=(CLAUDE.md Docs/Combat-Spec.md Docs/Working-In-Unreal.md
  Docs/Debug-Instruments.md Docs/Combat-Decisions.md Docs/Animation-Library.md
  Docs/Closing-Down.md)

FAILS=0; WARNS=0
row()  { printf '%-22s %-6s %s\n' "$1" "$2" "$3"; }
ok()   { row "$1" "PASS" "$2"; }
fail() { row "$1" "FAIL" "$2"; FAILS=$((FAILS+1)); }
warn() { row "$1" "WARN" "$2"; WARNS=$((WARNS+1)); }

# --- C1: terminal punctuation ------------------------------------------------
# Catches a truncated or spliced tail, which a content re-read misses because it checks
# fitness rather than integrity. A standing doc's last non-blank line must end like an
# ending: sentence punctuation (optionally wrapped in emphasis/quotes), a table row, a
# fence, or a rule.
check_terminal() { # $1=file -> 0 ok, 1 fail
  local last
  last=$(awk 'NF{l=$0} END{print l}' "$1")
  printf '%s' "$last" | grep -qE '([.!?][)"*_`'"'"']*[[:space:]]*$)|(\|[[:space:]]*$)|(^```[[:space:]]*$)|(^---[[:space:]]*$)'
}

# --- C2: table integrity -----------------------------------------------------
# Catches a row detached from its header, which renders as raw text, and blank lines
# splitting a table into runs GFM does not render as tables at all. Every maximal run
# of '|' lines must be at least two lines with a delimiter row second.
check_tables() { # $1=file -> prints offending line numbers, rc 1 if any
  awk '
    /^\|/ { if (run==0) start=NR; run++; if (run==2) second=$0; next }
    { if (run==1) print "orphan row at line " start;
      else if (run>1 && second !~ /^\|[-: |]+\|?[[:space:]]*$/) print "no delimiter row at line " start;
      run=0 }
    END { if (run==1) print "orphan row at line " start;
          else if (run>1 && second !~ /^\|[-: |]+\|?[[:space:]]*$/) print "no delimiter row at line " start }
  ' "$1" | { grep . && return 1 || return 0; }
}

# --- C3: pointer manifest ----------------------------------------------------
# Catches a cross-file pointer whose target moved. Each row asserts a literal a doc
# points at; a miss means the target moved and the pointer did not.
# Format: file:::literal:::which pointer relies on it.
MANIFEST='
CLAUDE.md:::| Light | 150 ms | **200 ms** |:::Combat-Spec cedes the ladder table to CLAUDE.md
CLAUDE.md:::### When a slice ships:::Closing-Down step 5 routes by this section
CLAUDE.md:::## Working Rules:::Debug-Instruments points the loop-coverage rule here
Docs/Combat-Spec.md:::### Stamina:::Debug-Instruments names this section as the stamina rule
Docs/Combat-Spec.md:::## The laws:::the Exchange entry points at the graduated laws
Docs/Combat-Spec.md:::ladder table lives in `CLAUDE.md`:::the spec must keep saying where its table went
Docs/Combat-Decisions.md:::## Known traps:::CLAUDE.md loop step greps this section
Docs/Combat-Decisions.md:::## Tuning map:::CLAUDE.md routing table sends knob rows here
Docs/Combat-Decisions.md:::## What has been superseded:::the supersession check reads this table
Docs/Combat-Decisions.md:::## Symbol index:::CLAUDE.md names it as a working section
Docs/Combat-Decisions.md:::## Slice briefs:::CLAUDE.md sends every slice pickup here
Docs/Combat-Decisions.md:::- **Knockdown**:::CLAUDE.md Current Focus points at this brief -- MOVE THIS ROW when a slice ships
Docs/Combat-Decisions.md:::- **Netcode**:::CLAUDE.md network section defers status to this brief
Docs/Debug-Instruments.md:::### Scenario matrix:::Working-In-Unreal sends verification here
Tools/RegressionCheck/regression-check.sh:::--self-test:::CLAUDE.md and W-I-U both name the script and its self-test
Docs/Toolset-Snapshot.tsv:::list_toolsets:::Working-In-Unreal diffs the registry against this file
'
check_manifest() { # $1=manifest-string -> prints misses, rc 1 if any
  local bad=0 line f pat why
  while IFS= read -r line; do
    [ -z "$line" ] && continue
    f=${line%%:::*}; rest=${line#*:::}; pat=${rest%%:::*}; why=${rest#*:::}
    if [ ! -f "$f" ] || ! grep -qF -- "$pat" "$f"; then
      echo "missing in $f: \"$pat\" ($why)"; bad=1
    fi
  done <<EOF
$1
EOF
  return $bad
}

# --- C4: symbol-index freshness ---------------------------------------------
# Catches the index falling behind its own archive, which is undetectable from inside it.
# The index preamble carries "Current through **2026-MM-DD**"; the newest dated
# entry may not be newer than it. Regeneration updates the line.
check_index() { # $1=decisions-file -> rc 1 if stale, prints detail
  local arch cur
  arch=$(grep -oE '^## 2026-[0-9]{2}-[0-9]{2}' "$1" | sed 's/^## 2026-//' | sort | tail -1)
  cur=$(grep -oE 'Current through \*\*2026-[0-9]{2}-[0-9]{2}\*\*' "$1" | grep -oE '2026-[0-9]{2}-[0-9]{2}' | sed 's/^2026-//' | head -1)
  if [ -z "$cur" ]; then echo "no 'Current through' line in index preamble"; return 1; fi
  if [ -z "$arch" ]; then echo "no dated entries found"; return 1; fi
  if [ "$arch" \> "$cur" ]; then echo "newest entry $arch outruns index (current through $cur) -- regenerate"; return 1; fi
  return 0
}

# --- C5 (WARN): trap-body shortlist ------------------------------------------
# Catches an orphaned trap body -- an edit that replaced a header instead of inserting
# before it leaves the body reading as prose belonging to the previous trap. Orphans
# cannot be told from continuation paragraphs mechanically, so this only shortlists
# paragraphs in the traps section that open unformatted; the closedown eye judges them.
check_trap_shortlist() { # $1=decisions-file -> prints shortlist (never fails)
  sed -n '/^## Known traps/,/^## Tuning map/p' "$1" |
  awk 'prev_blank && /^[A-Za-z]/ && !/^[A-Z][a-z]+:/ {n++; if (n<=8) printf "  line ~%d: %.60s\n", NR, $0}
       {prev_blank = (NF==0)} END {if (n>8) printf "  ...and %d more\n", n-8; if (n>0) exit 1}'
}

# --- C6: always-read duplication ---------------------------------------------
# Catches text duplicated between the two always-read files, which is pure double-pay
# every session. Any 10-word normalized shingle shared by both fails unless allowlisted
# here with a reason.
NGRAM_ALLOW='
'
check_ngrams() { # $1=fileA $2=fileB -> prints shared shingles, rc 1 if any
  perl -e '
    use strict; use warnings;
    my %allow = map { $_ => 1 } grep { length } split /\n/, q('"$NGRAM_ALLOW"');
    sub shingles {
      my ($f) = @_;
      open my $h, "<", $f or die $!;
      local $/; my $t = lc <$h>; close $h;
      $t =~ s/[`*_#>|"\x27]|—|–/ /g; $t =~ s/[[:punct:]]/ /g;
      my @w = split /\s+/, $t; my %s;
      for my $i (0 .. $#w - 9) { $s{ join " ", @w[$i .. $i+9] } = 1 }
      return \%s;
    }
    my $a = shingles($ARGV[0]); my $b = shingles($ARGV[1]);
    my @hit = grep { $b->{$_} && !$allow{$_} } keys %$a;
    # collapse overlapping shingles into one report each run of hits
    my $bad = 0;
    my %seen;
    for my $h (sort @hit) {
      my @w = split / /, $h;
      my $key = join " ", @w[0..4];
      next if $seen{$key}++;
      print "shared 10-gram: \"$h\"\n"; $bad = 1;
    }
    exit $bad;
  ' "$1" "$2"
}

# --- C7 (WARN): trailer audit ------------------------------------------------
# Catches a missing Co-Authored-By trailer. Parses real trailers rather than string-
# matching, which a message that merely *discusses* the trailer defeats.
# A flagged commit is not automatically wrong -- one you did not author says so
# in its message instead (20121e3 is the model) -- so this warns, never fails.
check_trailers() { # -> prints trailer-less commits since origin/main
  git rev-parse --verify -q origin/main >/dev/null 2>&1 || { echo "  (no origin/main)"; return 0; }
  git log origin/main..HEAD --format='%h%x09%(trailers:key=Co-Authored-By,valueonly)' 2>/dev/null |
    awk -F'\t' '$1!="" && $2=="" {print "  " $1 " has no parsed Co-Authored-By trailer"; n++} END {exit (n>0)}'
}

# --- C8 (WARN): budgets ------------------------------------------------------
# Backstops, never gates: the criterion is the closedown questions. A line count is
# checkable in a second and fitness is not, so the number crowds out the criterion
# unless it is explicitly demoted. ~280 / ~500 per Closing-Down.
check_budget() { # $1=file $2=limit
  local n; n=$(wc -l < "$1")
  [ "$n" -le "$2" ] && return 0
  echo "  $1 at $n lines against ~$2 backstop"; return 1
}

# ==== self-test ===============================================================
self_test() {
  local t; t=$(mktemp -d) || exit 2
  local bad=0
  expect() { # $1=desc $2=want(0|1) ; runs "$@" from $3...
    local desc=$1 want=$2; shift 2
    "$@" >/dev/null 2>&1; local got=$?
    [ "$got" -eq "$want" ] && return 0
    echo "SELF-TEST FAIL: $desc (wanted rc $want, got $got)"; bad=1
  }
  printf 'A doc that ends properly.\n' > "$t/good.md"
  printf 'A doc that ends mid-sen\n'   > "$t/trunc.md"
  printf '| a | b |\n|---|---|\n| 1 | 2 |\n' > "$t/table.md"
  printf 'text\n\n| lonely row |\n\ntext.\n'  > "$t/orphan.md"
  printf '| head |\n| data |\n'               > "$t/nodelim.md"
  printf '## 2026-08-18 — entry\nCurrent through **2026-08-18**.\n' > "$t/fresh.md"
  printf '## 2026-08-19 — entry\nCurrent through **2026-08-18**.\n' > "$t/stale.md"
  printf 'the quick brown fox jumps over the lazy sleeping dog twice\n' > "$t/ng1.md"
  printf 'again the quick brown fox jumps over the lazy sleeping dog twice\n' > "$t/ng2.md"
  printf 'nothing shared here at all beyond ordinary short words\n' > "$t/ng3.md"

  expect "terminal: proper ending passes"      0 check_terminal "$t/good.md"
  expect "terminal: truncation fails"          1 check_terminal "$t/trunc.md"
  expect "tables: proper table passes"         0 check_tables "$t/table.md"
  expect "tables: orphan row fails"            1 check_tables "$t/orphan.md"
  expect "tables: missing delimiter fails"     1 check_tables "$t/nodelim.md"
  expect "manifest: present literal passes"    0 check_manifest "$t/good.md:::properly:::fixture"
  expect "manifest: absent literal fails"      1 check_manifest "$t/good.md:::absent-string:::fixture"
  expect "index: current date passes"          0 check_index "$t/fresh.md"
  expect "index: newer entry fails"            1 check_index "$t/stale.md"
  expect "ngrams: shared shingle fails"        1 check_ngrams "$t/ng1.md" "$t/ng2.md"
  expect "ngrams: no shared shingle passes"    0 check_ngrams "$t/ng1.md" "$t/ng3.md"
  rm -rf "$t"
  if [ "$bad" -eq 0 ]; then echo "SELF-TEST PASSED (11 assertions)"; exit 0; fi
  exit 1
}

# ==== main ====================================================================
case "${1:-}" in
  --self-test) self_test ;;
  "") ;;
  *) echo "usage: docs-check.sh [--self-test]"; exit 2 ;;
esac

echo "docs-check -- standing-doc integrity ($(date +%F))"
echo

for f in "${STANDING_DOCS[@]}"; do
  if check_terminal "$f"; then ok "terminal-punct" "$f"; else fail "terminal-punct" "$f ends mid-thought"; fi
done

for f in "${STANDING_DOCS[@]}"; do
  out=$(check_tables "$f") && ok "tables" "$f" || fail "tables" "$f: $out"
done

out=$(check_manifest "$MANIFEST") && ok "pointer-manifest" "all $(printf '%s\n' "$MANIFEST" | grep -c ':::') pointers resolve" || fail "pointer-manifest" "$out"

out=$(check_index Docs/Combat-Decisions.md) && ok "index-freshness" "index current" || fail "index-freshness" "$out"

out=$(check_trap_shortlist Docs/Combat-Decisions.md) && ok "trap-shortlist" "no unformatted paragraph openers" || { warn "trap-shortlist" "review these openers:"; printf '%s\n' "$out"; }

out=$(check_ngrams CLAUDE.md Docs/Working-In-Unreal.md) && ok "always-read-dup" "no shared 10-grams" || fail "always-read-dup" "$out"

out=$(check_trailers) && ok "trailers" "all commits since origin/main carry parsed trailers" || { warn "trailers" "confirm these are not Claude-authored:"; printf '%s\n' "$out"; }

out=$(check_budget CLAUDE.md 280) && ok "budget" "CLAUDE.md inside backstop" || warn "budget" "$out"
out=$(check_budget Docs/Working-In-Unreal.md 500) && ok "budget" "Working-In-Unreal.md inside backstop" || warn "budget" "$out"

echo
if [ "$FAILS" -gt 0 ]; then echo "RESULT: $FAILS FAIL, $WARNS WARN"; exit 1; fi
echo "RESULT: all passed, $WARNS WARN"
exit 0
