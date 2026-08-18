#!/usr/bin/env bash
#
# regression-check.sh -- assert combat invariants against a PIE session's log.
#
# This is a *log evaluator*, not a test runner. Orchestration stays agent-side:
# set the fixture knobs, StartPIE, wait on a condition, StopPIE, then run this.
# See Docs/Debug-Instruments.md for the scenario matrix and the fixture each
# scenario expects.
#
#   ./regression-check.sh <scenario> [logfile]
#   ./regression-check.sh --self-test        # prove the instrument can fail
#
# Scenarios: s1-light s1-heavy s1-charged s2-light s2-heavy s2-charged s3
#            s4-string s4-guarantee s4-block s4-360   (all need StringTaps 3)
# Exit 0 = all assertions passed, 1 = at least one failed, 2 = usage/no data.

set -uo pipefail

# ---------------------------------------------------------------------------
# Bands. A retune is a one-line change here.
#
# Sources: authored values from GA_Attack's Branches and the character CDOs;
# tolerances from the spreads measured 2026-08-15 across this fixture.
# ---------------------------------------------------------------------------

# S1 -- press to RELEASE BEGIN, milliseconds. Authored hitbox-live times.
BAND_RELEASE_LIGHT=200;   BAND_RELEASE_HEAVY=500;   BAND_RELEASE_CHARGED=750
BAND_RELEASE_TOL=30

# S1 -- ABILITY END elapsed against the authored total, seconds.
# Frame quantisation only, and it does not accumulate: measured +15..+31 ms.
# Light 0.750 -> 0.950 on 2026-08-16 with Light String's long-recovery redesign: branch 0's
# RecoverySeconds went 0.40 -> 0.60, so the authored total is 0.20 + 0.15 + 0.60. Authored truth
# moved and the band followed it -- this is not a band patched to green. Heavy and charged are
# untouched; only the light chains, so only the light's recovery was retuned.
BAND_ELAPSED_LIGHT=0.950; BAND_ELAPSED_HEAVY=1.150; BAND_ELAPSED_CHARGED=1.500
# Floor 0.010 -> 0.005 on 2026-08-16: a completed heavy measured +9 ms (press->release in-band,
# every sibling +10..+35). The overhead is frame quantisation, and one frame landing tight is
# jitter at the sampler, not a combat change -- the floor now admits it.
BAND_ELAPSED_MIN=0.005;   BAND_ELAPSED_MAX=0.035

# S1 -- exact per-attack counts. The light never coils; charged escalates twice.
BAND_ESCALATE_LIGHT=0; BAND_ESCALATE_HEAVY=1; BAND_ESCALATE_CHARGED=2
BAND_COIL_LIGHT=0;     BAND_COIL_HEAVY=1;     BAND_COIL_CHARGED=1

# S2 -- authored stamina damage per tier.
BAND_STAMDMG_LIGHT=5; BAND_STAMDMG_HEAVY=50; BAND_STAMDMG_CHARGED=100

# S2 -- authored health damage per tier, for hits landing while the guard is down.
# Source: GA_Attack's Branches CDO, read 2026-08-15.
BAND_HEALTHDMG_LIGHT=15; BAND_HEALTHDMG_HEAVY=25; BAND_HEALTHDMG_CHARGED=40

# S2 -- blockstun span equals the tier's authored RecoverySeconds.
# The charged has none reachable: its stamina damage empties any bar, so it
# always breaks instead. That is a filed trap, asserted here as a standing fact.
# Light 0.400 -> 0.350 on 2026-08-16, derived rather than felt: after blocking, the defender must
# be able to *start* an attack before the next chained hit lands, but never land first. At a 500 ms
# chain cadence the hit arrives at T+200 and the next at T+700, so blockstun B must satisfy
# 400 + B > 700 (never first) -- B > 300. 350 is that floor plus the 50 ms margin used elsewhere.
BAND_BLOCKSTUN_LIGHT=0.350; BAND_BLOCKSTUN_HEAVY=0.500
BAND_BLOCKSTUN_TOL=0.020

# S2 -- GuardBreakStunSeconds, break to GUARD END. Measured 1.004..1.007.
BAND_GUARDSTUN=1.000; BAND_GUARDSTUN_TOL=0.025

# S3 -- DodgeTargetDistanceCm and the spread measured on clean samples.
BAND_DODGE_MIN=400; BAND_DODGE_MAX=420
# A travel sample must be a *finished* dodge: DodgeSeconds 0.4 minus one frame. The final dodge
# before StopPIE ends mid-travel with zero drift and is a session artifact, not a short dodge.
BAND_DODGE_MIN_DURATION=0.38
# A dodge is contaminated when anything touched the mover. A stationary dodge
# is purely backward, so lateral drift is the tell -- filter on this, never on
# the distance you were hoping for.
BAND_DODGE_LATERAL_MAX=1.0
# Dodge cost from a full bar.
BAND_DODGE_REMAINING_FROM_FULL=50.0

# Exhaustion: enters at 0, leaves at Max. Not a timer -- these two numbers are
# the whole assertion.
BAND_EXHAUST_ENTER=0.0; BAND_EXHAUST_EXIT=100.0
BAND_STAMINA_TOL=0.5

# S4 -- the light string. Fixture is DebugAutoAttackStringTaps 3, so each cycle is a burst of
# three swings rather than one. Every band here is derived from GA_Attack's CDO read 2026-08-18,
# NOT from the plan session's proposals -- three of those went stale when the cadence was
# measured off the designer and hitstun and blockstun were re-derived against it.
BAND_STRING_SWINGS=3

# ACTIVATE to the next ACTIVATE. 0.200 release-at + 0.150 release + ChainOpenAfterRecoverySeconds
# 0.133 + the buffer tick that notices recovery opened = ~0.500, which is the cadence the designer
# tapped on 2026-08-16. The tolerance is the tick landing either side of a frame.
BAND_CHAIN_GAP=0.500; BAND_CHAIN_GAP_TOL=0.045

# HitstunSeconds on branch 0. It must outlast the chain gap or "any hit guarantees the rest"
# silently stops being true -- which is what s4-guarantee observes directly.
BAND_HITSTUN_LIGHT=0.550; BAND_HITSTUN_TOL=0.020

# RELEASE OFF (recovery opens) to the chained ACTIVATE: ChainOpenAfterRecoverySeconds plus one
# buffer tick. The plan said "<= 2 frames", written when ChainOpen was still 0.
BAND_CHAIN_LATENCY_MIN_MS=125; BAND_CHAIN_LATENCY_MAX_MS=175

# Knockback determinism deliberately has NO band. The KNOCKBACK line prints its own authored
# target -- "spacing=150 (authored 150)" -- so the assertion compares the two fields against each
# other and cannot go stale when HitSpacingCm or BlockedSpacingCm is retuned. The clamp is
# one-sided by design (FinalSpacingCm = max(authored, currentAlong)), so the invariant is
# spacing >= authored, never equality: a hit landing beyond the reset keeps its distance.
BAND_SPACING_SLACK=0.5

# ---------------------------------------------------------------------------

PASSES=0; FAILS=0; ROWS=""

row() { # row PASS|FAIL <label> <detail>
	ROWS="${ROWS}$1\t$2\t$3\n"
	if [ "$1" = "PASS" ]; then PASSES=$((PASSES+1)); else FAILS=$((FAILS+1)); fi
}

check() { # check <label> <condition-result 0/1> <detail>
	if [ "$2" -eq 0 ]; then row PASS "$1" "$3"; else row FAIL "$1" "$3"; fi
}

usage() {
	sed -n '3,15p' "$0" | sed 's/^# \?//'
	exit 2
}

# --- slice the log from the last PIE start ---------------------------------
slice_log() {
	local log="$1" start
	start=$(grep -n "LogWorld: Bringing World .* up for play" "$log" 2>/dev/null | tail -1 | cut -d: -f1)
	if [ -z "$start" ]; then
		echo "regression-check: no PIE start marker in $log" >&2
		exit 2
	fi
	# Strip the timestamp/frame prefix and the category, leaving "[t] TAG ...".
	awk -v s="$start" 'NR>=s' "$log" \
		| grep "LogTDCombatTiming: \[" \
		| sed 's/^.*LogTDCombatTiming: //'
}

# --- shared extractors ------------------------------------------------------
# Each emits one number per event, in log order.

press_to_release() { # ms from each attack press to the following RELEASE BEGIN
	awk '
		/^\[[0-9.]+\] INPUT      InputTag\.Attack pressed/ {
			t=$1; gsub(/[\[\]]/,"",t); p=t+0; have=1; next
		}
		have && /^\[[0-9.]+\] RELEASE BEGIN/ {
			t=$1; gsub(/[\[\]]/,"",t); printf "%.0f\n", (t-p)*1000; have=0
		}' "$SLICE"
}

# Cancelled ends are excluded: a swing StopPIE (or anything else) tears down mid-flight logs
# "(cancelled)" and its elapsed is not an attack total. The dodge sampler has carried the same
# end-of-run guard since 2026-08-15; this one got phase-lucky until 2026-08-16, when a session
# stopped mid-swing and a charged read 0.776. The trace marks cancellation for exactly this.
elapsed_values() { grep "elapsed=" "$SLICE" | grep -v "(cancelled)" | grep -o "elapsed=[0-9.]*" | cut -d= -f2; }

count_per_attack() { # count_per_attack <TAG-regex>; count of TAG per COMPLETED attack
	# Only attacks whose ABILITY END is not "(cancelled)" count. A swing StopPIE truncates
	# mid-windup has an ACTIVATE and legitimately zero escalations or coils -- it was cut before
	# its first checkpoint, not thrown wrong. Same end-of-run class as the elapsed guard above,
	# caught 2026-08-16 when a heavy session stopped 0.1s into its final windup.
	local tag="$1"
	awk -v tag="$tag" '
		/^\[[0-9.]+\] ACTIVATE/ { started=1; n=0; next }
		started && /ABILITY END/ {
			if ($0 !~ /\(cancelled\)/) print n
			started=0; next
		}
		started && $0 ~ tag { n++ }' "$SLICE"
}

stamina_damage_values() { grep -o "staminaDamage=[0-9.]*" "$SLICE" | cut -d= -f2; }

damaged_values() { grep "^\[[0-9.]*\] DAMAGED" "$SLICE" | grep -o " damage=[0-9.]*" | cut -d= -f2; }

damaged_ledger_violations() { # consecutive health= must step by exactly damage=; REVIVE resets
	# Per TARGET ($3 on both lines), not one global chain: the attacker re-focuses on the nearest
	# LIVING pawn, so during a dead defender's revive window a swing can land on the player and a
	# global ledger reads two characters' health as one broken sequence. Found 2026-08-16 when the
	# 3.0s attack interval aliased against the 3.0s auto-revive and a heavy hit the player 4 ms
	# before the defender's REVIVE -- a phase race, so it passes most runs and means nothing.
	awk '
		/^\[[0-9.]+\] REVIVE/ { prev[$3]=""; next }
		/^\[[0-9.]+\] DAMAGED/ {
			t=$3; d=""; h=""
			for (i=1;i<=NF;i++) {
				if ($i ~ /^damage=/) { split($i,a,"="); d=a[2] }
				if ($i ~ /^health=/) { split($i,b,"="); h=b[2] }
			}
			if (prev[t] != "" && d != "" && h != "") {
				# The attribute set clamps at zero, so an overkill hit steps by what was left,
				# not by its damage -- the clamped result is the only truthful ledger entry,
				# per the DAMAGED log itself. 20.0 hit for 40 lands at exactly 0.0, and the
				# charged (damage 40 into lives of 100) does it every third guard-down hit.
				expected = prev[t] - d; if (expected < 0) expected = 0
				diff = expected - h; if (diff < 0) diff = -diff
				if (diff > 0.01) print t ": " prev[t] "->" h "(damage " d ")"
			}
			if (h != "") prev[t] = h
		}' "$SLICE"
}

blockstun_spans() { # until= minus the timestamp it was printed at
	awk '
		/^\[[0-9.]+\] BLOCKSTUN  / {
			t=$1; gsub(/[\[\]]/,"",t)
			for (i=1;i<=NF;i++) if ($i ~ /^until=/) { split($i,a,"="); printf "%.3f\n", a[2]-t }
		}' "$SLICE"
}

hitstun_spans() { # until= minus the timestamp it was printed at; same shape as blockstun_spans
	awk '
		/^\[[0-9.]+\] HITSTUN    / {
			t=$1; gsub(/[\[\]]/,"",t)
			for (i=1;i<=NF;i++) if ($i ~ /^until=/) { split($i,a,"="); printf "%.3f\n", a[2]-t }
		}' "$SLICE"
}

swing_index_counts() { # "<count> <swing index>" per index, so a burst shows equal counts
	grep -o "ACTIVATE   swing=[0-9]*" "$SLICE" | cut -d= -f2 | sort | uniq -c
}

chain_gaps() { # seconds between an ACTIVATE and the chained ACTIVATE after it
	# Emitted only when the *arriving* swing index is non-zero: index 0 opens a new burst, and the
	# gap from the previous burst's ender is the fixture's 3 s interval, not a cadence sample.
	awk '
		/^\[[0-9.]+\] ACTIVATE/ {
			t=$1; gsub(/[\[\]]/,"",t)
			s=0; for (i=1;i<=NF;i++) if ($i ~ /^swing=/) { split($i,a,"="); s=a[2] }
			if (s+0 > 0 && prev != "") printf "%.3f\n", t-prev
			prev=t
		}' "$SLICE"
}

chain_latency_ms() { # RELEASE OFF to the chained ACTIVATE that follows it
	awk '
		/^\[[0-9.]+\] RELEASE OFF/ { t=$1; gsub(/[\[\]]/,"",t); r=t+0; have=1; next }
		have && /^\[[0-9.]+\] ACTIVATE/ {
			s=0; for (i=1;i<=NF;i++) if ($i ~ /^swing=/) { split($i,a,"="); s=a[2] }
			if (s+0 > 0) { t=$1; gsub(/[\[\]]/,"",t); printf "%.0f\n", (t-r)*1000 }
			have=0
		}' "$SLICE"
}

knockback_inward_violations() { # spacing must never fall below the authored value it prints
	awk -v slack="$BAND_SPACING_SLACK" '
		/^\[[0-9.]+\] KNOCKBACK/ {
			sp=""; au=""
			for (i=1;i<=NF;i++) {
				if ($i ~ /^spacing=/) { split($i,a,"="); sp=a[2] }
				if ($i == "(authored") { au=$(i+1); gsub(/\)/,"",au) }
			}
			if (sp != "" && au != "" && sp+0 < au+0 - slack) print sp "<authored " au
		}' "$SLICE"
}

knockback_count() { grep -c "^\[[0-9.]*\] KNOCKBACK" "$SLICE" || true; }

dodges_inside_hitstun() { # DODGE lines falling between a HITSTUN and its HITSTUN END
	awk '
		/^\[[0-9.]+\] HITSTUN    / { inside=1; next }
		/^\[[0-9.]+\] HITSTUN END/ { inside=0; next }
		inside && /^\[[0-9.]+\] DODGE      dir=/ { n++ }
		END { print n+0 }' "$SLICE"
}


first_burst_targets_per_window() { # "<attack index> <distinct targets damaged in that window>"
	# **First burst only, and that is the assertion's whole shape.** The finisher's 360 hits both
	# bodies and then knocks them to the attacker's facing axis, so from the second burst onward
	# they sit inside the 60-degree wedge and the earlier attacks hit them too -- the discrimination
	# is gone. The ender's displacement is Knockdown & Oki's to replace; when it does, widen this.
	# Do not "fix" a failure here by taking more bursts: that samples contaminated geometry.
	awk '
		/^\[[0-9.]+\] ACTIVATE/ {
			for (i=1;i<=NF;i++) if ($i ~ /^swing=/) { split($i,a,"="); sw=a[2] }
			if (sw+0 == 0) { bursts++ }
			if (bursts > 1) { exit }
			next
		}
		/^\[[0-9.]+\] RELEASE BEGIN/ { inwin=1; delete seen; n=0; next }
		/^\[[0-9.]+\] RELEASE END/   { if (inwin) print sw " " n; inwin=0; next }
		inwin && /^\[[0-9.]+\] DAMAGED/ { if (!($3 in seen)) { seen[$3]=1; n++ } }' "$SLICE"
}

guardstun_spans() { # GUARD BREAK to the next GUARD END
	awk '
		/^\[[0-9.]+\] GUARD BREAK/ { t=$1; gsub(/[\[\]]/,"",t); b=t+0; have=1; next }
		have && /^\[[0-9.]+\] GUARD END/ {
			t=$1; gsub(/[\[\]]/,"",t); printf "%.3f\n", t-b; have=0
		}' "$SLICE"
}

# --- assertion helpers ------------------------------------------------------

assert_all_in_band() { # label values_cmd lo hi unit
	local label="$1" lo="$3" hi="$4" unit="${5:-}" vals n bad
	vals=$(eval "$2")
	n=$(printf '%s\n' "$vals" | grep -c '[0-9]')
	if [ "$n" -eq 0 ]; then check "$label" 1 "no samples"; return; fi
	bad=$(printf '%s\n' "$vals" | awk -v lo="$lo" -v hi="$hi" '$1<lo || $1>hi' | tr '\n' ' ')
	if [ -n "${bad// /}" ]; then
		check "$label" 1 "n=$n outside [$lo,$hi]$unit: $bad"
	else
		check "$label" 0 "n=$n all within [$lo,$hi]$unit"
	fi
}

assert_all_equal() { # label values_cmd expected
	local label="$1" want="$3" vals n bad
	vals=$(eval "$2")
	n=$(printf '%s\n' "$vals" | grep -c '[0-9]')
	if [ "$n" -eq 0 ]; then check "$label" 1 "no samples"; return; fi
	bad=$(printf '%s\n' "$vals" | awk -v w="$want" '$1+0 != w+0' | tr '\n' ' ')
	if [ -n "${bad// /}" ]; then
		check "$label" 1 "n=$n expected $want, saw: $bad"
	else
		check "$label" 0 "n=$n all exactly $want"
	fi
}

assert_count() { # label actual expected
	if [ "$2" -eq "$3" ]; then check "$1" 0 "$2"; else check "$1" 1 "expected $3, got $2"; fi
}

# --- scenarios --------------------------------------------------------------

run_s1() { # run_s1 <release_ms> <elapsed_authored> <escalations> <coils>
	local rel="$1" ela="$2" esc="$3" coil="$4"
	assert_all_in_band "press->RELEASE BEGIN" press_to_release \
		$((rel-BAND_RELEASE_TOL)) $((rel+BAND_RELEASE_TOL)) "ms"
	assert_all_in_band "ABILITY END elapsed" elapsed_values \
		"$(awk -v a="$ela" -v d="$BAND_ELAPSED_MIN" 'BEGIN{printf "%.3f", a+d}')" \
		"$(awk -v a="$ela" -v d="$BAND_ELAPSED_MAX" 'BEGIN{printf "%.3f", a+d}')" "s"
	assert_all_equal "ESCALATE per attack" "count_per_attack '] ESCALATE'" "$esc"
	assert_all_equal "COIL START per attack" "count_per_attack '] COIL START'" "$coil"
}

run_s2() { # run_s2 <stamina_damage> <blockstun_span|none> <health_damage>
	local dmg="$1" bs="$2" hdmg="$3" raises costs breaks zeros viol dcount

	assert_all_equal "BLOCKED staminaDamage" stamina_damage_values "$dmg"

	# Guard-down hits: every stun and every exhaustion window lets swings through onto health,
	# so a HoldBlock run structurally contains DAMAGED lines -- none at all means the fixture
	# stopped trading, not that damage is clean.
	assert_all_equal "DAMAGED health damage" damaged_values "$hdmg"

	viol=$(damaged_ledger_violations | tr '\n' ';')
	dcount=$(damaged_values | grep -c '[0-9]')
	if [ -n "${viol//;/}" ]; then
		check "health ledger steps by damage" 1 "$viol"
	else
		check "health ledger steps by damage" 0 "n=$dcount, all consecutive steps exact"
	fi

	# Parity: every guard raised charges its initial cost. This is only true
	# because the dummy mirrors the player's BlockInitialStaminaCost.
	raises=$(grep -c "^\[[0-9.]*\] BLOCK      up" "$SLICE")
	costs=$(grep -c "^\[[0-9.]*\] BLOCK      cost" "$SLICE")
	assert_count "BLOCK cost per BLOCK up" "$costs" "$raises"

	# A break happens exactly when a blocked hit leaves the bar at 0, and only
	# then. Compare the count of breaks to the count of remaining=0.0 blocks.
	breaks=$(grep -c "^\[[0-9.]*\] GUARD BREAK" "$SLICE")
	zeros=$(grep "^\[[0-9.]*\] BLOCKED" "$SLICE" | grep -c "remaining=0.0")
	assert_count "GUARD BREAK == blocks at 0" "$breaks" "$zeros"

	if [ "$breaks" -gt 0 ]; then
		assert_all_in_band "guard break stun" guardstun_spans \
			"$(awk -v a="$BAND_GUARDSTUN" -v t="$BAND_GUARDSTUN_TOL" 'BEGIN{printf "%.3f", a-t}')" \
			"$(awk -v a="$BAND_GUARDSTUN" -v t="$BAND_GUARDSTUN_TOL" 'BEGIN{printf "%.3f", a+t}')" "s"
	fi

	if [ "$bs" = "none" ]; then
		# The charged can never blockstun -- it always breaks instead.
		assert_count "BLOCKSTUN never fires" "$(grep -c '^\[[0-9.]*\] BLOCKSTUN  ' "$SLICE")" 0
	else
		assert_all_in_band "BLOCKSTUN span" blockstun_spans \
			"$(awk -v a="$bs" -v t="$BAND_BLOCKSTUN_TOL" 'BEGIN{printf "%.3f", a-t}')" \
			"$(awk -v a="$bs" -v t="$BAND_BLOCKSTUN_TOL" 'BEGIN{printf "%.3f", a+t}')" "s"
	fi
}

run_s3() {
	local starts ends clean_n

	starts=$(grep -c "^\[[0-9.]*\] DODGE      dir=" "$SLICE")
	ends=$(grep -c "^\[[0-9.]*\] DODGE END" "$SLICE")
	assert_count "DODGE/DODGE END paired" "$ends" "$starts"

	# Travel, clean samples only -- one filter, defined in clean_dodge_distances.
	clean_n=$(clean_dodge_distances | grep -c '[0-9]')
	if [ "$clean_n" -eq 0 ]; then
		check "dodge travel (clean)" 1 "no uncontaminated samples"
	else
		assert_all_in_band "dodge travel (clean)" "clean_dodge_distances" \
			"$BAND_DODGE_MIN" "$BAND_DODGE_MAX" "cm"
	fi

	# A dodge from a full bar costs exactly 50.
	assert_all_equal "dodge from full costs 50" "dodge_from_full_remaining" \
		"$BAND_DODGE_REMAINING_FROM_FULL"

	# Exhaustion brackets, and the two values that say it is not a timer.
	#
	# A session that stops mid-exhaustion leaves exactly one trailing EXHAUSTED with no END --
	# recovery takes ~5s and StopPIE does not wait for it. That is truncation, not a leak, and is
	# tolerated only when the unclosed one is the LAST line of the pair-stream; an unclosed
	# exhaustion anywhere earlier is a genuine stuck state and still fails. Same end-of-run
	# family as the elapsed and per-attack guards, caught 2026-08-16.
	local exh_starts exh_ends exh_expected
	exh_starts=$(grep -c '^\[[0-9.]*\] EXHAUSTED ' "$SLICE")
	exh_ends=$(grep -c '^\[[0-9.]*\] EXHAUSTION END' "$SLICE")
	exh_expected=$exh_starts
	if [ "$exh_starts" -eq $((exh_ends + 1)) ]; then
		last_pair_line=$(grep '^\[[0-9.]*\] EXHAUST' "$SLICE" | tail -1)
		case "$last_pair_line" in
			*"EXHAUSTION END"*) : ;; # last event is an END: the missing one is mid-run, fail
			*) exh_expected=$((exh_starts - 1)) ;; # trailing open exhaustion: truncation
		esac
	fi
	assert_count "EXHAUSTED/END paired" "$exh_ends" "$exh_expected"
	assert_all_in_band "exhaustion enters at 0" "exhaust_enter_stamina" \
		"$(awk -v v="$BAND_EXHAUST_ENTER" -v t="$BAND_STAMINA_TOL" 'BEGIN{printf "%.2f", v-t}')" \
		"$(awk -v v="$BAND_EXHAUST_ENTER" -v t="$BAND_STAMINA_TOL" 'BEGIN{printf "%.2f", v+t}')"
	assert_all_in_band "exhaustion clears at Max" "exhaust_exit_stamina" \
		"$(awk -v v="$BAND_EXHAUST_EXIT" -v t="$BAND_STAMINA_TOL" 'BEGIN{printf "%.2f", v-t}')" \
		"$(awk -v v="$BAND_EXHAUST_EXIT" -v t="$BAND_STAMINA_TOL" 'BEGIN{printf "%.2f", v+t}')"
}

# --- S4: the light string ---------------------------------------------------
# Fixture for all four: DebugAutoAttackStringTaps 3. The defender differs per scenario.

assert_burst_shape() { # every swing index in the string fires the same number of times
	local counts n_idx uneven
	counts=$(swing_index_counts)
	n_idx=$(printf '%s\n' "$counts" | grep -c '[0-9]')
	if [ "$n_idx" -ne "$BAND_STRING_SWINGS" ]; then
		check "string is $BAND_STRING_SWINGS swings" 1 "saw $n_idx distinct swing indices: $(echo $counts | tr '\n' ' ')"
		return
	fi
	# A burst cut off by StopPIE leaves the earlier indices one ahead; tolerate exactly that.
	uneven=$(printf '%s\n' "$counts" | awk '{print $1}' | sort -n | awk 'NR==1{lo=$1} {hi=$1} END{print hi-lo}')
	if [ "$uneven" -le 1 ]; then
		check "string is $BAND_STRING_SWINGS swings" 0 "$(echo $counts | tr '\n' ' ')"
	else
		check "string is $BAND_STRING_SWINGS swings" 1 "counts differ by $uneven: $(echo $counts | tr '\n' ' ')"
	fi
}

assert_never_inward() {
	local bad n
	bad=$(knockback_inward_violations)
	n=$(knockback_count)
	# n=0 must FAIL, not pass on an empty set. A fixture with knockback switched off, or a build
	# where it stopped firing, would otherwise report this green while asserting nothing -- the
	# same vacuous-pass class --self-test exists to rule out.
	if [ "$n" -eq 0 ]; then
		check "knockback never pulls inward" 1 "no KNOCKBACK lines at all -- nothing was asserted"
	elif [ -z "$bad" ]; then
		check "knockback never pulls inward" 0 "n=$n, every spacing >= its authored value"
	else
		check "knockback never pulls inward" 1 "$(echo "$bad" | head -3 | tr '\n' ' ')"
	fi
}

run_s4_string() {
	assert_burst_shape
	assert_all_in_band "chain gap (cadence)" "chain_gaps" \
		"$(awk -v v="$BAND_CHAIN_GAP" -v t="$BAND_CHAIN_GAP_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_CHAIN_GAP" -v t="$BAND_CHAIN_GAP_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"
	assert_all_in_band "chain latency" "chain_latency_ms" \
		"$BAND_CHAIN_LATENCY_MIN_MS" "$BAND_CHAIN_LATENCY_MAX_MS" "ms"
	assert_all_equal "DAMAGED health damage" damaged_values "$BAND_HEALTHDMG_LIGHT"

	local viol dcount
	viol=$(damaged_ledger_violations | tr '\n' ';')
	dcount=$(damaged_values | grep -c '[0-9]')
	if [ -n "${viol//;/}" ]; then
		check "health ledger steps by damage" 1 "$viol"
	else
		check "health ledger steps by damage" 0 "n=$dcount, all consecutive steps exact"
	fi
	assert_all_in_band "HITSTUN span" "hitstun_spans" \
		"$(awk -v v="$BAND_HITSTUN_LIGHT" -v t="$BAND_HITSTUN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_HITSTUN_LIGHT" -v t="$BAND_HITSTUN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"
	assert_never_inward
}

run_s4_guarantee() {
	local refused inside
	assert_burst_shape
	refused=$(grep -c ": hitstun" "$SLICE" || true)
	check "REFUSED names hitstun" "$([ "$refused" -gt 0 ] && echo 0 || echo 1)" \
		"$refused refusals attributed to State.Hitstun"
	inside=$(dodges_inside_hitstun)
	check "zero dodges inside hitstun" "$([ "$inside" -eq 0 ] && echo 0 || echo 1)" \
		"$inside DODGE lines between HITSTUN and HITSTUN END"
	assert_all_in_band "HITSTUN span" "hitstun_spans" \
		"$(awk -v v="$BAND_HITSTUN_LIGHT" -v t="$BAND_HITSTUN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_HITSTUN_LIGHT" -v t="$BAND_HITSTUN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"
}

run_s4_360() {
	# Light 3's 360-degree volume, asserted as a count rather than an angle. The attacker holds its
	# placed yaw (FacingMode Never) with a target 90 degrees to either side, so the shared
	# 60-degree wedge -- even widened by the ~24 degrees a capsule subtends at this range -- cannot
	# reach either. The finisher has no bearing test at all, so it reaches both.
	#
	# Requires bDebugAutoAttackHomeBetweenAttacks: an attacker whiffing into open space has an open
	# standoff gate and runs its full authored lunge, which would carry it away from both.
	local rows early late
	rows=$(first_burst_targets_per_window)
	if [ -z "$rows" ]; then
		check "first burst observed" 1 "no release windows in the first burst"
		return
	fi

	early=$(printf '%s\n' "$rows" | awk '$1 < 2 {print $2}' | sort -u | tr '\n' ' ')
	late=$(printf '%s\n' "$rows" | awk '$1 == 2 {print $2}' | sort -u | tr '\n' ' ')

	check "60-degree attacks reach neither" \
		"$([ "$(echo $early)" = "0" ] && echo 0 || echo 1)" \
		"attacks 1-2 damaged: ${early:-none} distinct targets"
	check "the 360 finisher reaches both" \
		"$([ "$(echo $late)" = "2" ] && echo 0 || echo 1)" \
		"attack 3 damaged: ${late:-none} distinct targets"
}

run_s4_block() {
	assert_burst_shape
	assert_all_equal "BLOCKED staminaDamage" "stamina_damage_values" "$BAND_STAMDMG_LIGHT"
	assert_all_in_band "BLOCKSTUN span" "blockstun_spans" \
		"$(awk -v v="$BAND_BLOCKSTUN_LIGHT" -v t="$BAND_BLOCKSTUN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_BLOCKSTUN_LIGHT" -v t="$BAND_BLOCKSTUN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"
	assert_never_inward
}

clean_dodge_distances() {
	# Full-duration, uncontaminated samples only. The lateral gate excludes collisions (the
	# attacker shoving a mid-dodge body reads as right= drift); the duration gate excludes
	# dodges truncated by the session itself -- the last dodge before StopPIE ends mid-travel
	# with zero drift and read as a 141 cm travel failure until this existed (2026-08-15).
	awk -v m="$BAND_DODGE_LATERAL_MAX" -v mind="$BAND_DODGE_MIN_DURATION" '
		/^\[[0-9.]+\] DODGE      dir=/ { t=$1; gsub(/[\[\]]/,"",t); start=t+0; have=1; next }
		have && /^\[[0-9.]+\] DODGE END/ {
			t=$1; gsub(/[\[\]]/,"",t); dur=(t+0)-start; have=0
			r=""; d=""
			for (i=1;i<=NF;i++) {
				if ($i ~ /^right=/) { split($i,a,"="); r=a[2]; if (r<0) r=-r }
				if ($i ~ /^dist=/)  { split($i,b,"="); d=b[2]; sub(/uu$/,"",d) }
			}
			if (dur >= mind && r != "" && r <= m) print d
		}' "$SLICE"
}

dodge_from_full_remaining() {
	# The first dodge of each full-bar stretch: session start, after every EXHAUSTION END
	# (which by rule fires at Max), and after every REVIVE (which refills). Selected by
	# *position*, never by the value under test -- the earlier version filtered samples to the
	# expected 50 and then asserted 50, so it could only fail via "no samples" (found in
	# review, 2026-08-15).
	awk '
		BEGIN { expect=1 }
		/^\[[0-9.]+\] EXHAUSTION END/ { expect=1; next }
		/^\[[0-9.]+\] REVIVE/ { expect=1; next }
		/^\[[0-9.]+\] DODGE      dir=/ {
			if (expect) {
				for (i=1;i<=NF;i++) if ($i ~ /^remaining=/) { split($i,a,"="); print a[2] }
				expect=0
			}
		}' "$SLICE"
}

exhaust_enter_stamina() {
	grep "^\[[0-9.]*\] EXHAUSTED " "$SLICE" | grep -o "stamina=[0-9.]*" | cut -d= -f2
}

exhaust_exit_stamina() {
	grep "^\[[0-9.]*\] EXHAUSTION END" "$SLICE" | grep -o "stamina=[0-9.]*" | cut -d= -f2
}

# --- self-test: the checker must be seen to fail ----------------------------
self_test() {
	echo "Self-test: asserting the checker reports FAIL on a band it cannot meet."
	echo
	SLICE=$(mktemp)
	cat >"$SLICE" <<'EOF'
[1.000] ACTIVATE   pos=0.0000
[1.000] INPUT      InputTag.Attack pressed on Fixture
[1.200] RELEASE BEGIN  pos=0.3
[1.769] ABILITY END  pos=0.0000 elapsed=0.769
[2.000] DAMAGED    Defender by Fixture  damage=15  health=85.0
[2.500] DAMAGED    Defender by Fixture  damage=15  health=70.0
EOF
	# Correct band: 200 ms press->release. Should PASS.
	PASSES=0; FAILS=0; ROWS=""
	assert_all_in_band "control (correct band)" press_to_release 170 230 "ms"
	local good=$FAILS
	# Deliberately wrong band: the same 200 ms sample asserted at 500 ms.
	PASSES=0; FAILS=0; ROWS=""
	assert_all_in_band "deliberately wrong band" press_to_release 470 530 "ms"
	local bad=$FAILS
	# Same pair for the damage ledger: 15s must pass as 15 and fail as 25.
	PASSES=0; FAILS=0; ROWS=""
	assert_all_equal "control (damaged=15)" damaged_values 15
	local good2=$FAILS
	PASSES=0; FAILS=0; ROWS=""
	assert_all_equal "deliberately wrong (damaged=25)" damaged_values 25
	local bad2=$FAILS
	rm -f "$SLICE"

	if [ "$good" -eq 0 ] && [ "$bad" -eq 1 ] && [ "$good2" -eq 0 ] && [ "$bad2" -eq 1 ]; then
		echo "  PASS  both control bands passed and both wrong bands FAILED."
		echo "  The instrument can fail, so its passes mean something."
		exit 0
	fi
	echo "  BROKEN  timing $good/$bad, damage $good2/$bad2 (want 0/1 and 0/1)."
	echo "  Do not trust any result from this checker until fixed."
	exit 1
}

# --- main -------------------------------------------------------------------

[ $# -ge 1 ] || usage
[ "$1" = "--self-test" ] && self_test

SCENARIO="$1"
LOGFILE="${2:-Saved/Logs/TheDream.log}"
[ -f "$LOGFILE" ] || { echo "regression-check: no such log: $LOGFILE" >&2; exit 2; }

SLICE=$(mktemp)
trap 'rm -f "$SLICE"' EXIT
slice_log "$LOGFILE" >"$SLICE"
[ -s "$SLICE" ] || { echo "regression-check: no combat trace in the last PIE session" >&2; exit 2; }

case "$SCENARIO" in
	s1-light)   run_s1 $BAND_RELEASE_LIGHT   $BAND_ELAPSED_LIGHT   $BAND_ESCALATE_LIGHT   $BAND_COIL_LIGHT ;;
	s1-heavy)   run_s1 $BAND_RELEASE_HEAVY   $BAND_ELAPSED_HEAVY   $BAND_ESCALATE_HEAVY   $BAND_COIL_HEAVY ;;
	s1-charged) run_s1 $BAND_RELEASE_CHARGED $BAND_ELAPSED_CHARGED $BAND_ESCALATE_CHARGED $BAND_COIL_CHARGED ;;
	s2-light)   run_s2 $BAND_STAMDMG_LIGHT   $BAND_BLOCKSTUN_LIGHT $BAND_HEALTHDMG_LIGHT ;;
	s2-heavy)   run_s2 $BAND_STAMDMG_HEAVY   $BAND_BLOCKSTUN_HEAVY $BAND_HEALTHDMG_HEAVY ;;
	s2-charged) run_s2 $BAND_STAMDMG_CHARGED none $BAND_HEALTHDMG_CHARGED ;;
	s3)           run_s3 ;;
	s4-string)    run_s4_string ;;
	s4-guarantee) run_s4_guarantee ;;
	s4-block)     run_s4_block ;;
	s4-360)       run_s4_360 ;;
	*) echo "regression-check: unknown scenario '$SCENARIO'" >&2; usage ;;
esac

echo
echo "  scenario: $SCENARIO    log: $LOGFILE"
echo "  ------------------------------------------------------------------"
printf "$ROWS" | while IFS=$'\t' read -r status label detail; do
	printf "  %-6s %-28s %s\n" "$status" "$label" "$detail"
done
echo "  ------------------------------------------------------------------"
echo "  $PASSES passed, $FAILS failed"
echo

[ "$FAILS" -eq 0 ] || exit 1
exit 0
