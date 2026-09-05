#!/usr/bin/env bash
# comment-check.sh -- keeps WHY out of code comments, in docs-check's image.
#
# The rule lives in CLAUDE.md: comments carry WHAT, and HOW where the mechanism is
# not plain from reading; they never carry WHY. WHY goes to Docs/Combat-Spec.md when
# it still governs play and to a dated Decisions.md entry otherwise, and the
# symbol index routes a symbol back to it.
#
# This script owns only what a grep can own -- the markers of WHY, not WHY itself.
# C1 and C2 fail on the forms that cannot be anything else. C3 and C4 shortlist for a
# human eye. C5 is a backstop, never a gate.
#
#   ./Tools/CommentCheck/comment-check.sh              # check Source/ and Tools/
#   ./Tools/CommentCheck/comment-check.sh --list C1    # print every hit for one check
#   ./Tools/CommentCheck/comment-check.sh --self-test  # prove the instrument can fail
#
# Exit 0 = all checks passed (WARNs allowed), 1 = a check failed, 2 = usage.
# Run at closedown alongside docs-check.

set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 2

# This file is exempt from its own scan: its check patterns and its self-test
# fixtures must contain the very strings the checks forbid.
SELF="Tools/CommentCheck/comment-check.sh"

BLOCK_MAX=8
CAP=12         # hits shown per check in the summary; --list raises it

FAILS=0; WARNS=0
row()  { printf '%-22s %-6s %s\n' "$1" "$2" "$3"; }
ok()   { row "$1" "PASS" "$2"; }
fail() { row "$1" "FAIL" "$2"; FAILS=$((FAILS+1)); }
warn() { row "$1" "WARN" "$2"; WARNS=$((WARNS+1)); }

sources() { # every scanned file, self excluded
  find Source Tools -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.sh' \) \
    | grep -vF "$SELF" | sort
}

# --- extractor ---------------------------------------------------------------
# Emits "file<TAB>lineno<TAB>text" for each comment line. String literals are
# blanked before the scan so a URL in a string is not read as a line comment, and
# a date in code is not read as a date in a comment.
extract() {
  awk '
    function blank(s,   out,i,c,q) {
      out=""; q=""
      for (i=1; i<=length(s); i++) {
        c=substr(s,i,1)
        if (q=="") {
          if (c=="\"" || c=="\047") { q=c; out=out " "; continue }
          out=out c
        } else {
          if (c=="\\") { i++; out=out "  "; continue }
          if (c==q) { q=""; out=out " "; continue }
          out=out " "
        }
      }
      return out
    }
    function emit(t) { printf "%s\t%d\t%s\n", FILENAME, FNR, t }
    FNR==1 { inblock=0; isshell = (FILENAME ~ /\.(sh|bash)$/) }
    {
      raw=$0; line=blank(raw)
      if (isshell) {
        if (FNR==1 && raw ~ /^#!/) next
        p=index(line,"#"); if (p>0) emit(substr(raw,p))
        next
      }
      if (inblock) {
        e=index(line,"*/")
        if (e>0) { emit(substr(raw,1,e+1)); inblock=0 } else emit(raw)
        next
      }
      sl=index(line,"//"); bl=index(line,"/*")
      if (sl>0 && (bl==0 || sl<bl)) { emit(substr(raw,sl)); next }
      if (bl>0) {
        rest=substr(line,bl); e=index(rest,"*/")
        if (e>0) { emit(substr(raw,bl,e+1)) } else { emit(substr(raw,bl)); inblock=1 }
        next
      }
    }
  ' "$@"
}

# --- C1 (FAIL): no dates in comments -----------------------------------------
# A date in a comment cannot be a WHAT. It is always the opening of a war story --
# what was tried, when it broke, what it said before it was corrected.
DATE_RE='(19|20)[0-9][0-9]-[0-9][0-9]-[0-9][0-9]'
check_dates() { # $@=files -> prints hits, rc 1 if any
  extract "$@" | grep -E "$DATE_RE" | sed 's/^/  /' | { grep . && return 1 || return 0; }
}

# --- C2 (FAIL): no attributions in comments ----------------------------------
# Who decided a thing is a fact about the project's history, not about the code.
ATTRIB_RE='the designer|per the user|the user said|as [A-Z][a-z]+ (said|noted|pointed out)'
check_attrib() { # $@=files -> prints hits, rc 1 if any
  extract "$@" | grep -E "$ATTRIB_RE" | sed 's/^/  /' | { grep . && return 1 || return 0; }
}

# --- C3 (WARN): oversized blocks ---------------------------------------------
# A contract states itself in a few lines. A run longer than BLOCK_MAX is where
# WHY hides once the dates and attributions are gone; the closedown eye judges it.
check_blocks() { # $@=files -> prints runs over BLOCK_MAX (never fails hard)
  extract "$@" | awk -F'\t' -v max="$BLOCK_MAX" -v cap="$CAP" '
    function flush() {
      if (run > max) { n++; if (n<=cap) printf "  %s:%d  %d lines\n", f, start, run }
      run=0
    }
    { if ($1==f && $2==prev+1) run++; else { flush(); f=$1; start=$2; run=1 }
      prev=$2 }
    END { flush(); if (n>cap) printf "  ...and %d more\n", n-cap; if (n>0) exit 1 }'
}

# --- C4 (WARN): narrative connectives ----------------------------------------
# Lower precision than C1 and C2 on purpose: these phrases can open a legitimate
# HOW. They shortlist, they do not convict.
NARRATIVE_RE='considered and rejected|used to |turned out|shipped for days|was wrong|had been|no longer|previously|we tried|it ran the|for days|at the time'
check_narrative() { # $@=files -> prints hits (never fails hard)
  extract "$@" | grep -Ei "$NARRATIVE_RE" | sed 's/^/  /' |
    awk -v cap="$CAP" '{n++; if (n<=cap) print} END {if (n>cap) printf "  ...and %d more\n", n-cap; if (n>0) exit 1}'
}

# --- C5 (WARN): per-file ratio backstop --------------------------------------
# Backstop, never a gate: the criterion is the rule, not the number. Catches a file that
# grows uniformly, which C3 misses because no single block is large.
#
# **Two thresholds, because a header and a .cpp are different shapes.** A declaration
# header's comments *are* its interface -- every property owes a contract, and the code
# beside them is two lines of UPROPERTY -- so headers legitimately run several times the
# ratio an implementation does. One number would either never fire on a header or fire on
# every .cpp.
#
# Both set from what the debloat pass landed at, plus headroom: headers topped out at 309
# per 100 and implementations at 66. Files under RATIO_FLOOR comment lines are exempt --
# a ratio over a handful of lines measures nothing.
RATIO_MAX_HEADER=330
RATIO_MAX_IMPL=100
RATIO_FLOOR=10 # files under this many comment lines have no volume to judge
check_ratio() { # $@=files -> prints files over their threshold (never fails hard)
  local f c k RATIO_MAX
  for f in "$@"; do
    case "$f" in *.h) RATIO_MAX=$RATIO_MAX_HEADER ;; *) RATIO_MAX=$RATIO_MAX_IMPL ;; esac
    c=$(extract "$f" | wc -l)
    [ "$c" -lt "$RATIO_FLOOR" ] && continue
    k=$(awk -v F="$f" '
      { line=$0; sub(/^[ \t]+/,"",line); if (line=="") next
        if (F ~ /\.(sh|bash)$/) { if (line ~ /^#/) next } 
        else { if (line ~ /^(\/\/|\/\*|\*)/) next }
        k++ } END{print k+0}' "$f")
    [ "$k" -eq 0 ] && continue
    if [ $((c * 100 / k)) -gt "$RATIO_MAX" ]; then
      printf "  %s  %d:%d  (%d per 100)\n" "$f" "$c" "$k" $((c * 100 / k))
    fi
  done | { grep . && return 1 || return 0; }
}

# --- C6 (FAIL): orphaned doc blocks ------------------------------------------
# Catches a doc block that documents another doc block, which happens when an insertion
# lands between a comment and the thing it described: the stranded block now sits above
# the wrong declaration and its own declaration is left undocumented.
#
# It cannot catch the same failure in a // run, nor a single block sitting on the wrong
# declaration -- both are well-formed to a grep. Only reading finds those.
check_orphans() { # $@=files -> prints offending sites, rc 1 if any
  awk 'prev ~ /^[[:space:]]*\*\/[[:space:]]*$/ && /^[[:space:]]*\/\*\*/ {
         printf "  %s:%d  doc block follows a doc block\n", FILENAME, FNR
       }
       { prev = $0 }' "$@" | { grep . && return 1 || return 0; }
}

# --- volume accounting (C7, C8) ----------------------------------------------
# C1-C6 judge a comment by its shape. Neither shape nor ratio can see a file that is
# simply getting wordier: C3 is saturated at over a hundred deliberate blocks, so a
# regrown one is indistinguishable from them, and C5 is a fixed ceiling with room to
# spare. What is missing is a memory of where the tree already was.
#
# The baseline is that memory -- comment lines and words per file, checked in. Growth
# past it is the failure, so a deliberate addition is a baseline edit that shows in the
# diff and gets its reason in the commit message, and drift is the thing that has to be
# justified rather than the thing that happens quietly.
#
# **Two tolerances, because they catch different failures.** A loose per-file one names
# a single file that rotted; a tight corpus one catches uniform creep spread thin enough
# that no single file trips. Growth under TOL_FLOOR lines never fails on its own, so a
# small file gaining a line is not an incident -- the corpus check is what stops repeated
# sub-floor additions from accumulating.
#
# **Only baselined files count toward the corpus total**, so adding a new file cannot
# trip the corpus tolerance. A new file is judged by C5 until it has a baseline.
BASELINE="Tools/CommentCheck/baseline.txt"
TOL_FILE=10    # per-file growth allowed, percent
TOL_TREE=2     # baselined-corpus growth allowed, percent
TOL_FLOOR=5    # per-file growth under this many lines or words never fails alone

volumes() { # $@=files -> "file<TAB>comment lines<TAB>comment words"
  extract "$@" | awk '
    { f=$1
      t=$0; sub(/^[^\t]*\t[0-9]*\t/, "", t)
      sub(/^[ \t]*(\/\/+|\/\*+|\*+|#+)[ \t]*/, "", t)
      gsub(/[^A-Za-z0-9_.:*()-]+/, " ", t)
      n[f]++; w[f] += split(t, a, " ") }
    END { for (f in n) printf "%s\t%d\t%d\n", f, n[f], w[f] }' | sort
}

# $1=baseline file $2=column (2 lines, 3 words) $3=per-file tol $4=corpus tol; $5..=files
# Prints offenders and any unbaselined or stale entries; rc 1 only on real growth.
compare_volume() {
  local bl=$1 col=$2 tf=$3 tt=$4; shift 4
  [ -f "$bl" ] || { echo "  no baseline at $bl -- run --baseline"; return 0; }
  volumes "$@" | awk -v BL="$bl" -v COL="$col" -v TF="$tf" -v TT="$tt" -v FL="$TOL_FLOOR" '
    BEGIN { while ((getline line < BL) > 0) {
              if (line ~ /^#/ || line == "") continue
              split(line, b, "\t"); base[b[1]] = b[COL] + 0; seen[b[1]] = 1
            } }
    { cur[$1] = $COL + 0 }
    END {
      bad = 0; ct = 0; bt = 0
      for (f in cur) {
        if (!(f in seen)) { printf "  %s  unbaselined (%d)\n", f, cur[f]; continue }
        ct += cur[f]; bt += base[f]
        g = cur[f] - base[f]
        if (g >= FL && base[f] > 0 && g * 100 > base[f] * TF) {
          printf "  %s  %d -> %d  (+%d%%, tol %d%%)\n", f, base[f], cur[f], g * 100 / base[f], TF
          bad = 1
        }
      }
      for (f in seen) if (!(f in cur)) printf "  %s  stale baseline entry\n", f
      if (bt > 0 && (ct - bt) * 100 > bt * TT) {
        printf "  corpus  %d -> %d  (+%d%%, tol %d%%)\n", bt, ct, (ct - bt) * 100 / bt, TT
        bad = 1
      }
      exit bad
    }'
}

check_volume()  { compare_volume "$BASELINE" 2 "$TOL_FILE" "$TOL_TREE" "$@"; }
check_density() { compare_volume "$BASELINE" 3 "$TOL_FILE" "$TOL_TREE" "$@"; }

write_baseline() {
  { cat <<'HDR'
# comment-check baseline -- comment lines and words per file, as of the last deliberate
# reset. C7 fails when a file grows more than 10 percent past its line count, or the
# baselined corpus more than 2 percent; C8 warns on the same shape for words.
#
# Raising one number here is the sanctioned way to add comment volume: the diff names
# what grew and the commit message carries the reason. Regenerate wholesale only after a
# pass that deliberately re-set the standard, never to make a run green.
#
#   ./Tools/CommentCheck/comment-check.sh --baseline
#
# file<TAB>comment lines<TAB>comment words
HDR
    volumes $(sources)
  } > "$BASELINE"
  printf 'wrote %s (%d files)\n' "$BASELINE" "$(volumes $(sources) | grep -c .)"
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

  printf '// Raised 2026-08-18 after the trace disagreed.\nint a;\n'        > "$t/date_line.cpp"
  printf '/*\n *  Contract.\n *  Superseded 2026-08-18.\n */\nint b;\n'     > "$t/date_block.cpp"
  printf 'const char* s = "built 2026-08-18";\nint c;\n'                    > "$t/date_code.cpp"
  printf 'const char* u = "http://x.test/2026-08-18";\nint d;\n'            > "$t/url_code.cpp"
  printf '/** Refuses every ability while set. */\nint e;\n'                > "$t/clean.cpp"
  printf '// Chosen by the designer over the alternative.\nint f;\n'        > "$t/attrib.cpp"
  printf '#!/usr/bin/env bash\n# Added 2026-08-18 for the audit.\ntrue\n'   > "$t/date.sh"
  printf '#!/usr/bin/env bash\ntrue\n'                                      > "$t/bare.sh"
  { printf '/**\n'; for i in 1 2 3 4 5 6 7 8 9; do printf ' *  line %d\n' "$i"; done; printf ' */\nint g;\n'; } > "$t/long.cpp"
  printf '/**\n *  Two lines only.\n */\nint h;\n'                          > "$t/short.cpp"
  printf '// This turned out to be wrong.\nint i;\n'                        > "$t/narr.cpp"
  printf '// Refuses offense only.\nint j;\n'                               > "$t/nonarr.cpp"
  printf '/**\n *  one\n */\n/**\n *  two\n */\nint k;\n'                   > "$t/orphan.cpp"
  printf '/**\n *  one\n */\nint m;\n\n/**\n *  two\n */\nint n;\n'         > "$t/noorphan.cpp"
  { printf '/**
 *  a
 *  b
 *  c
 *  d
 *  e
 *  f
 *  g
 *  h
 */
'; seq 1 40 | sed 's/.*/int v&;/'; } > "$t/lean.cpp"

  expect "C1: date in line comment fails"        1 check_dates     "$t/date_line.cpp"
  expect "C1: date in block comment fails"       1 check_dates     "$t/date_block.cpp"
  expect "C1: date in a string literal passes"   0 check_dates     "$t/date_code.cpp"
  expect "C1: date in a URL literal passes"      0 check_dates     "$t/url_code.cpp"
  expect "C1: clean comment passes"              0 check_dates     "$t/clean.cpp"
  expect "C1: date in shell comment fails"       1 check_dates     "$t/date.sh"
  expect "C1: shebang alone passes"              0 check_dates     "$t/bare.sh"
  expect "C2: attribution fails"                 1 check_attrib    "$t/attrib.cpp"
  expect "C2: clean comment passes"              0 check_attrib    "$t/clean.cpp"
  expect "C3: block over max warns"              1 check_blocks    "$t/long.cpp"
  expect "C3: block under max passes"            0 check_blocks    "$t/short.cpp"
  expect "C4: narrative connective warns"        1 check_narrative "$t/narr.cpp"
  expect "C4: plain contract passes"             0 check_narrative "$t/nonarr.cpp"
  { printf '/**
 *  a
 *  b
 *  c
 *  d
 *  e
 *  f
 *  g
 *  h
 */
'; seq 1 40 | sed 's/.*/int v&;/'; } > "$t/lean.cpp"
  expect "C5: file under the floor is exempt"    0 check_ratio     "$t/clean.cpp"
  expect "C5: comment-heavy file warns"          1 check_ratio     "$t/long.cpp"
  expect "C5: lean file over the floor passes"   0 check_ratio     "$t/lean.cpp"
  expect "C6: doc block after doc block fails"    1 check_orphans   "$t/orphan.cpp"
  expect "C6: doc blocks with code between pass"  0 check_orphans   "$t/noorphan.cpp"

  # C7/C8 drive compare_volume with explicit tolerances so each assertion isolates one
  # guard; 999 disables the other. The constants themselves are TOL_FILE and TOL_TREE.
  mkcomments() { # $1=file $2=comment lines $3=words per line
    local i j line; : > "$1"
    for i in $(seq 1 "$2"); do
      line="//"; for j in $(seq 1 "$3"); do line="$line word"; done
      printf '%s\n' "$line" >> "$1"
    done
    printf 'int z;\n' >> "$1"
  }
  mkcomments "$t/vol_a.cpp" 20 4
  mkcomments "$t/vol_b.cpp" 20 4
  volumes "$t/vol_a.cpp" "$t/vol_b.cpp" > "$t/bl.txt"

  expect "C7: unchanged corpus passes"            0 compare_volume "$t/bl.txt" 2 10 2   "$t/vol_a.cpp" "$t/vol_b.cpp"
  mkcomments "$t/vol_a.cpp" 30 4
  expect "C7: file grown past tolerance fails"    1 compare_volume "$t/bl.txt" 2 10 999 "$t/vol_a.cpp" "$t/vol_b.cpp"
  mkcomments "$t/vol_a.cpp" 22 4
  expect "C7: growth under the floor passes"      0 compare_volume "$t/bl.txt" 2 10 999 "$t/vol_a.cpp" "$t/vol_b.cpp"
  mkcomments "$t/vol_a.cpp" 26 4
  mkcomments "$t/vol_b.cpp" 26 4
  expect "C7: creep under per-file tol still fails" 1 compare_volume "$t/bl.txt" 2 999 2 "$t/vol_a.cpp" "$t/vol_b.cpp"
  mkcomments "$t/vol_a.cpp" 20 4
  mkcomments "$t/vol_b.cpp" 20 4
  expect "C7: an unbaselined file does not fail"  0 compare_volume "$t/bl.txt" 2 10 2   "$t/vol_a.cpp" "$t/vol_b.cpp" "$t/clean.cpp"
  mkcomments "$t/vol_a.cpp" 20 9
  expect "C8: same lines, more words, fails"      1 compare_volume "$t/bl.txt" 3 10 999 "$t/vol_a.cpp" "$t/vol_b.cpp"

  rm -rf "$t"
  if [ "$bad" -eq 0 ]; then echo "SELF-TEST PASSED (24 assertions)"; exit 0; fi
  exit 1
}

# ==== main ====================================================================
case "${1:-}" in
  --self-test) self_test ;;
  --baseline) write_baseline; exit 0 ;;
  --list) shift; CAP=100000; case "${1:-}" in
      C1) check_dates     $(sources) ;;
      C2) check_attrib    $(sources) ;;
      C3) check_blocks    $(sources) ;;
      C4) check_narrative $(sources) ;;
      C5) check_ratio     $(sources) ;;
      C6) check_orphans   $(sources) ;;
      C7) check_volume    $(sources) ;;
      C8) check_density   $(sources) ;;
      *) echo "usage: comment-check.sh --list C1|C2|C3|C4|C5|C6|C7|C8"; exit 2 ;;
    esac; exit 0 ;;
  "") ;;
  *) echo "usage: comment-check.sh [--self-test|--baseline|--list Cn]"; exit 2 ;;
esac

echo "comment-check -- WHY stays out of code comments ($(date +%F))"
echo

FILES=$(sources)
NF=$(printf '%s\n' "$FILES" | grep -c .)
NC=$(extract $FILES | wc -l)
NK=$(printf '%s\n' "$FILES" | while read -r f; do
       awk -v F="$f" '{ line=$0; sub(/^[ \t]+/,"",line); if (line=="") next
         if (F ~ /\.(sh|bash)$/) { if (line ~ /^#/) next } else { if (line ~ /^(\/\/|\/\*|\*)/) next }
         k++ } END{print k+0}' "$f"; done | awk '{s+=$1} END{print s}')

out=$(check_dates     $FILES) && ok "C1 dates"       "no dates in comments" \
  || { fail "C1 dates" "a comment carries a date:"; printf '%s\n' "$out" | head -12; }

out=$(check_attrib    $FILES) && ok "C2 attribution" "no attributions in comments" \
  || { fail "C2 attribution" "a comment names who decided:"; printf '%s\n' "$out" | head -12; }

out=$(check_blocks    $FILES) && ok "C3 block size"  "no block over $BLOCK_MAX lines" \
  || { warn "C3 block size" "review these blocks:"; printf '%s\n' "$out"; }

out=$(check_narrative $FILES) && ok "C4 narrative"   "no narrative connectives" \
  || { warn "C4 narrative" "review these lines:"; printf '%s\n' "$out"; }

out=$(check_ratio     $FILES) && ok "C5 ratio"       "every file inside its backstop ($RATIO_MAX_HEADER header / $RATIO_MAX_IMPL impl)" \
  || { warn "C5 ratio" "over backstop:"; printf '%s\n' "$out"; }

out=$(check_orphans   $FILES) && ok "C6 orphans"     "no doc block documents another doc block" \
  || { fail "C6 orphans" "a doc block is orphaned:"; printf '%s\n' "$out" | head -12; }

out=$(check_volume    $FILES); rc=$?
if [ "$rc" -eq 0 ]; then ok "C7 volume" "no file or corpus over baseline (+$TOL_FILE% file, +$TOL_TREE% corpus)"
else fail "C7 volume" "comment volume grew past baseline:"; fi
[ -n "$out" ] && printf '%s\n' "$out"

out=$(check_density   $FILES); rc=$?
if [ "$rc" -eq 0 ]; then ok "C8 density" "no file or corpus wordier than baseline"
else warn "C8 density" "comment words grew past baseline:"; fi
[ -n "$out" ] && printf '%s\n' "$out" | grep -v 'unbaselined\|stale baseline'

echo
printf 'corpus: %d files, %d comment lines, %d code lines (%d per 100)\n' \
  "$NF" "$NC" "$NK" $((NC * 100 / NK))
echo
if [ "$FAILS" -gt 0 ]; then echo "RESULT: $FAILS FAIL, $WARNS WARN"; exit 1; fi
echo "RESULT: all passed, $WARNS WARN"
exit 0
