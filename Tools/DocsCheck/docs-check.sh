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
  Docs/Unreal-Findings.md Docs/Debug-Instruments.md Docs/Combat-Decisions.md
  Docs/Animation-Library.md Docs/Closing-Down.md)

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
CLAUDE.md:::### When a slice ships:::Closing-Down step 5 routes by this section
CLAUDE.md:::## Working Rules:::Debug-Instruments points the loop-coverage rule here
Docs/Combat-Spec.md:::### Stamina:::Debug-Instruments names this section as the stamina rule
Docs/Combat-Spec.md:::## The laws:::the Exchange entry points at the graduated laws
Docs/Combat-Spec.md:::| Light | 150 ms | 200 ms |:::the ladder table lives here; the vocabulary and the checker both lean on it
Docs/Combat-Decisions.md:::## Known traps:::CLAUDE.md loop step greps this section
Docs/Combat-Decisions.md:::## Tuning map:::CLAUDE.md routing table sends knob rows here
Docs/Combat-Decisions.md:::## What has been superseded:::the supersession check reads this table
Docs/Combat-Decisions.md:::## Symbol index:::CLAUDE.md names it as a working section
Docs/Combat-Decisions.md:::## Slice briefs:::CLAUDE.md sends every slice pickup here
Docs/Combat-Decisions.md:::- **Polish**:::CLAUDE.md Current Focus points at this brief -- MOVE THIS ROW when a slice ships
Docs/Combat-Decisions.md:::- **Netcode**:::CLAUDE.md network section defers status to this brief
Docs/Debug-Instruments.md:::### Scenario matrix:::Working-In-Unreal sends verification here
Tools/RegressionCheck/regression-check.sh:::--self-test:::CLAUDE.md and W-I-U both name the script and its self-test
Tools/CommentCheck/comment-check.sh:::--self-test:::CLAUDE.md names the script; Closing-Down step 3 runs it
Tools/CommentCheck/comment-check.sh:::--baseline:::CLAUDE.md and Closing-Down both name the flag as the sanctioned reset
Tools/CommentCheck/baseline.txt:::comment lines:::CLAUDE.md sends a C7 failure here to raise one line
Docs/Toolset-Snapshot.tsv:::list_toolsets:::Working-In-Unreal diffs the registry against this file
Tools/DocsCheck/claim-scan.pl:::engine behaviour:::Working-In-Unreal names the no-surface categories the scanner must accept
Tools/DocsCheck/claim-scan.pl:::--working-only:::the flag that excludes the append-only archive of dated entries
Docs/Working-In-Unreal.md:::claim-scan.pl:::the method section names the scanner that enforces surface-and-date
Docs/Working-In-Unreal.md:::Docs/Unreal-Findings.md:::the pre-read points at the lookup half it was split from
Docs/Unreal-Findings.md:::Working-In-Unreal.md:::the lookup half points back at the pre-read that governs it
CLAUDE.md:::Docs/Unreal-Findings.md:::the doc list carries the new file and its trigger
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

# --- C5b: unqualified capability claims --------------------------------------
# Catches the failure this project kept hitting from the other side: a claim that something
# cannot be done, naming a callable but neither the surface it was tried on nor the date. An
# MCP-only result then reads identically to one tested across MCP, editor Python and C++, so
# nothing invites a re-test and the claim becomes permanent. Detection lives in claim-scan.pl.
check_claims() { # $1..=files -> prints shortlist, rc 1 if any unqualified
  perl "$ROOT/Tools/DocsCheck/claim-scan.pl" "$@"
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
# unless it is explicitly demoted.
#
# The two files are policed for different things, which is why the numbers differ by
# more than their sizes do. CLAUDE.md is read in full every session, so its number is
# a real cost and stays tight. Working-In-Unreal is triggered, and it is *expected* to
# grow -- every engine limit re-measured lands in it. Its number is therefore a prompt
# to subdivide rather than a request to cut: the file has clean seams (driving the
# editor, building C++, writing assets, what is scriptable, measuring, git), and
# crossing the line means it is time to consider splitting one out, not to trim prose
# that earned its place.
check_budget() { # $1=file $2=limit $3=what crossing it means
  local n; n=$(wc -l < "$1")
  [ "$n" -le "$2" ] && return 0
  echo "  $1 at $n lines against ~$2 -- $3"; return 1
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
  printf 'The MCP layer cannot call `save_dirty_packages` *(2026-08-27)*.\n' > "$t/claim_ok.md"
  printf 'There is no way to call `save_dirty_packages` at all.\n'          > "$t/claim_bare.md"
  printf 'Python cannot call `save_dirty_packages` on this asset.\n'        > "$t/claim_nodate.md"
  printf 'Enumerate before concluding `save_dirty_packages` cannot be run.\n' > "$t/claim_meta.md"

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
  expect "claims: qualified claim passes"      0 check_claims "$t/claim_ok.md"
  expect "claims: no surface, no date fails"   1 check_claims "$t/claim_bare.md"
  expect "claims: surface without date fails"  1 check_claims "$t/claim_nodate.md"
  expect "claims: meta discussion passes"      0 check_claims "$t/claim_meta.md"
  rm -rf "$t"
  if [ "$bad" -eq 0 ]; then echo "SELF-TEST PASSED (15 assertions)"; exit 0; fi
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

# Scoped to the docs where tooling-capability claims live. Combat-Spec's "cannot" is gameplay
# language and the decision log's is code behaviour; neither is a claim about a scripting surface.
# Unreal-Findings is scanned --working-only: its dated findings are append-only and already sit
# under dated headers, exactly as the decision log's archive does.
claims_rc=0
out=$(check_claims CLAUDE.md Docs/Working-In-Unreal.md Docs/Debug-Instruments.md \
        Docs/Anim-Pipeline.md Docs/Animation-Library.md) || claims_rc=1
out2=$(check_claims --working-only Docs/Unreal-Findings.md) || claims_rc=1
out="$out$out2"
[ "$claims_rc" -eq 0 ] \
  && ok "claim-qualification" "every capability claim names a surface and a date" \
  || { fail "claim-qualification" "unqualified capability claims:"; printf '%s\n' "$out"; }

out=$(check_ngrams CLAUDE.md Docs/Working-In-Unreal.md) && ok "always-read-dup" "no shared 10-grams" || fail "always-read-dup" "$out"

out=$(check_trailers) && ok "trailers" "all commits since origin/main carry parsed trailers" || { warn "trailers" "confirm these are not Claude-authored:"; printf '%s\n' "$out"; }

out=$(check_budget CLAUDE.md 280 "read in full every session; audit it against the closedown questions") && ok "budget" "CLAUDE.md inside backstop" || warn "budget" "$out"
# Lowered from 750 when the lookup half moved to Unreal-Findings.md. This file's whole instruction
# is "read front to back", so the backstop is what keeps that instruction honest rather than
# aspirational; findings growth belongs in Unreal-Findings.md, which is deliberately unbudgeted.
out=$(check_budget Docs/Working-In-Unreal.md 600 "the lookup half belongs in Docs/Unreal-Findings.md, not here") && ok "budget" "Working-In-Unreal.md inside backstop" || warn "budget" "$out"

echo
if [ "$FAILS" -gt 0 ]; then echo "RESULT: $FAILS FAIL, $WARNS WARN"; exit 1; fi
echo "RESULT: all passed, $WARNS WARN"
exit 0
