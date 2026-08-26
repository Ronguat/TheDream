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
#            s5-parry s5-parry-reward s5-parry-whiff s5-cancel s5-waiver
#            s6-knockdown s6-hard s6-stand s6-getup
#            s6-dodge s6-kipup s6-block s6-hard-stand      (sub-slice D's options)
#            s6-exhausted s6-exhausted-kipup s6-exhausted-block s6-exhausted-attack
#            s6-airborne s6-exhaust-regen
# Exit 0 = all assertions passed, 1 = at least one failed, 2 = usage/no data.

set -uo pipefail

# ---------------------------------------------------------------------------
# Bands. A retune is a one-line change here.
#
# Sources: authored values from GA_Attack's Branches and the character CDOs; tolerances from the
# spreads measured across this fixture.
# ---------------------------------------------------------------------------

# S1 -- press to RELEASE BEGIN, milliseconds. Authored hitbox-live times.
# The ladder poles as a fast layer -- light 200, heavy 400, read "they pressed" -- against a slow
# layer, charged 800, read "they're charging".
BAND_RELEASE_LIGHT=200;   BAND_RELEASE_HEAVY=400;   BAND_RELEASE_CHARGED=800
BAND_RELEASE_TOL=30

# S1 -- ABILITY END elapsed against the authored total, seconds. Frame quantisation only, and it
# does not accumulate: measured +15..+31 ms. Each band is ReleaseAt + Release + Recovery for its
# tier: light 0.20 + 0.15 + 0.60, heavy 0.40 + 0.15 + 0.50. **Re-derive from the CDO rather than
# nudging** -- a band moved to make a run green no longer asserts anything.
BAND_ELAPSED_LIGHT=0.950; BAND_ELAPSED_HEAVY=1.050; BAND_ELAPSED_CHARGED=1.550
# The floor is zero because zero is reachable: since the release window closes on an elapsed
# deadline rather than on its notify, the only overhead left is the distance from that deadline to
# the next tick, which is nil whenever the authored span divides evenly into the frame time.
# Measured across three frame rates: +0 at 60 fps (150 ms is exactly 9 frames), +8..+22 uncapped,
# +17 at 30 fps. The ceiling keeps its headroom for a slower machine than this one.
BAND_ELAPSED_MIN=0.000;   BAND_ELAPSED_MAX=0.035

# S1 -- exact per-attack counts. The light never coils; charged escalates twice.
BAND_ESCALATE_LIGHT=0; BAND_ESCALATE_HEAVY=1; BAND_ESCALATE_CHARGED=2
BAND_COIL_LIGHT=0;     BAND_COIL_HEAVY=1;     BAND_COIL_CHARGED=1

# S2 -- authored stamina damage per tier.
BAND_STAMDMG_LIGHT=5; BAND_STAMDMG_HEAVY=50; BAND_STAMDMG_CHARGED=100

# S2 -- authored health damage per tier, for hits landing while the guard is down.
# Source: GA_Attack's Branches CDO.
BAND_HEALTHDMG_LIGHT=15; BAND_HEALTHDMG_HEAVY=25; BAND_HEALTHDMG_CHARGED=40

# S2 -- blockstun span. Each tier's basis is its own; neither asserted tier is its RecoverySeconds
# any more, so do not re-derive one from the other. The charged has none reachable: its stamina
# damage empties any bar, so it always breaks instead -- a filed trap, asserted here as a standing
# fact.
# **The light's is derived, not felt**: after blocking, the defender must be able to *start* an
# attack before the next chained hit lands, but never land first. At a 500 ms chain cadence the hit
# arrives at T+200 and the next at T+700, so blockstun B must satisfy 400 + B > 700 -- B > 300, and
# 0.350 is that floor plus the 50 ms margin used elsewhere.
# **The heavy's basis is different**: a heavy landing on a guard is the intended paid transaction
# -- 50 stamina bitten, initiative retained -- so it is plus on block by design. Basis is recovery
# (0.50) + 0.10 of advantage, the advantage being the only felt number in it.
BAND_BLOCKSTUN_LIGHT=0.350; BAND_BLOCKSTUN_HEAVY=0.600
BAND_BLOCKSTUN_TOL=0.020

# S2 -- GuardBreakStunSeconds, break to GUARD END. Measured 1.004..1.007.
BAND_GUARDSTUN=1.000; BAND_GUARDSTUN_TOL=0.025

# S5 -- the parry window and the recovery a whiff leaves behind.
# Source: GA_Parry's CDO (ParryWindowSeconds, ParryWhiffRecoverySeconds).
# Neither is a free number. The window is fenced above by the anti-option-select ceiling -- one
# press must not cover two read-classes, so it must stay under the fast-to-charged gap of 400 ms,
# which DodgeSeconds welds there. There is no lower fence: window >= the longest authored
# ReleaseSeconds retired with Knockdown's parry rework. The recovery is floored by the constraint
# that a whiff timed against the fast layer stays locked through the charged's 800 ms arrival.
BAND_PARRY_WINDOW=0.300
BAND_PARRY_RECOVERY=0.600
BAND_PARRY_SPAN_TOL=0.025

# S5 -- Parry Grace, the tail a *successful* parry leaves behind. Source: ParryGraceSeconds on the
# character CDO. **Derived, not chosen**: 150 ms is roughly the interval humans cannot beat, about
# seven inputs a second, which makes two attacks inside it unanswerable by a second press.
# Re-derive against that ceiling, never against feel. It exists so a successful parry lasts longer
# than 0 ms -- a catch closes the window, so without it one press answers only one attack.
BAND_PARRY_GRACE=0.150
# The parry lockout is **authored** per branch and per swing rather than derived, so this is the
# value off the CDO and not an arithmetic result.
# s5-parry's attacker throws the string, whose first swing is the light branch.
BAND_PARRY_LOCKOUT_LIGHT=0.750

# S5 -- the credited stamina reward, as seen by s5-parry, whose parrier never spends. A parry costs
# nothing, so that fixture's bar never leaves 100 and the attribute set's clamp trims the whole
# reward: every sample there is legitimately 0.0. This band asserts the *clamp*, not the magnitude.
BAND_PARRY_GAINED_MIN=0
BAND_PARRY_GAINED_MAX=25

# S5 -- the reward's actual magnitude, asserted by s5-parry-reward, whose parrier holds a guard
# before each attempt and so has room for the credit. Source: ParryStaminaReward on the character
# CDO. **The pre-block is what makes this assertable at all** -- see DebugParryPreBlockSeconds.
BAND_PARRY_GAINED_EXACT=25

# S5 -- how quickly the on-hit waiver lets the attacker's own dodge out, in ms from its DAMAGED.
# The waiver frees defensive activations *instantly*, so this measures the fixture's press latency
# rather than a designed delay; anything beyond a couple of frames means the commitment tag is
# still refusing, which is the rule silently not working.
BAND_WAIVER_DODGE_MAX_MS=100

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

# S4 -- the light string. Fixture is DebugAutoAttackStringTaps 3, so each cycle throws a string of
# three swings rather than one. **Every band here is derived from GA_Attack's CDO, never from a
# plan's proposals** -- proposals go stale as soon as anything downstream is re-derived.
BAND_STRING_SWINGS=3

# ACTIVATE to the next ACTIVATE. 0.200 release-at + 0.150 release + ChainOpenAfterRecoverySeconds
# 0.133 + the buffer tick that notices recovery opened = ~0.500, the tapped cadence. The tolerance
# is the tick landing either side of a frame.
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

# --- S6: knockdown ----------------------------------------------------------
# Both types total 2.5s and begin their forced rise at 2.0 -- the split between
# lockout and input window is what the type changes, not the total. Sources: the
# Knockdown* properties on ATDCombatCharacter, read off the CDO at build time.
BAND_KD_ENTRY_TO_RISE=2.000     # lockout + input window, type-invariant by design
BAND_KD_RISE=0.500              # KnockdownRiseSeconds, shared
# The dodge get-up's rise is the dodge, so this tracks DodgeSeconds rather than the shared rise
# above. Move it whenever DodgeSeconds moves -- nothing enforces the link.
BAND_KD_RISE_DODGE=0.400
BAND_KD_SPAN_TOL=0.025
# The lockout is only observable through a press: refusals name their phase, and a
# stand fires the frame the input window opens. Normal 1.0, hard 1.5.
BAND_KD_LOCKOUT_NORMAL=1.000
BAND_KD_LOCKOUT_HARD=1.500

# S6 -- the get-up attack's authored phases: press to RELEASE BEGIN in WindupSeconds, and the
# ability's own ABILITY END at WindupSeconds + ReleaseSeconds + RecoverySeconds. Source:
# GA_GetUpAttack's CDO, defaults in UTDGetUpAttackAbility.
BAND_RELEASE_GETUP=300
BAND_ELAPSED_GETUP=1.250

# S6 -- the get-up options. The cost is GA_Dodge's, shared by the roll and the kip-up. The kip-up
# ceiling is slack for capsule settle rather than a travel budget: the authored displacement is
# zero, so this exists to make "stationary" falsifiable rather than assumed. The guard gap is one
# frame at 60 plus slack -- the block get-up's whole claim is that the guard is live from
# activation, not from the top of the rise.
# S6 -- the airborne knockdown. The floor is measured in-run from grounded stands, so a level edit
# cannot silently invalidate the comparison. The height minimum is what separates "airborne by the
# flag" from "airborne with room for a pinned Z to show" -- a body 2cm up proves only the former.
# S6 -- the exhaustion exception, asserted as time that fails to appear. An exhausted player's
# recovery is StaminaRegenPauseSeconds then MaxStamina/ExhaustedStaminaRegenPerSecond, plus the
# guard-break stun when a break caused it, since regen is suppressed across that. Sources are
# BP_TrainingDummy's CDO: 0.5 pause, 25/s exhausted regen, 100 max, 1.0 break stun.
BAND_EXHAUST_PAUSE=0.5
BAND_EXHAUST_REGEN_SECONDS=4.0
BAND_EXHAUST_BREAK_STUN=1.0
BAND_EXHAUST_SPAN_TOL=0.100

BAND_AIRBORNE_STAND_TOL=1.0
BAND_AIRBORNE_MIN_HEIGHT=20.0

BAND_GETUP_DODGE_COST=50.0
BAND_KIPUP_TRAVEL_MAX=25.0
# s7, death. The revive delay is a debug affordance rather than a design value --
# DebugAutoReviveSeconds on the placed dummy -- so this band tracks the fixture, not the game.
BAND_REVIVE_DELAY=3.000
BAND_REVIVE_TOL=0.060
# The settle distance is physics rather than an authored number, so the band is wide on purpose:
# measured 396-449 across six deaths at DeathImpulseStrength 30000. It exists to catch a *change*,
# not to pin a value -- a halved impulse lands near 200 and bVelChange sends the corpse past 18000.
BAND_DEATH_SETTLE_LO=300
BAND_DEATH_SETTLE_HI=560
BAND_BLOCK_GUARD_GAP=0.100

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
	sed -n '3,17p' "$0" | sed 's/^# \?//'
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
# end-of-run guard. The trace marks cancellation for exactly this.
elapsed_values() { grep "elapsed=" "$SLICE" | grep -v "(cancelled)" | grep -o "elapsed=[0-9.]*" | cut -d= -f2; }

count_per_attack() { # count_per_attack <TAG-regex>; count of TAG per COMPLETED attack
	# Only attacks whose ABILITY END is not "(cancelled)" count. A swing StopPIE truncates
	# mid-windup has an ACTIVATE and legitimately zero escalations or coils -- it was cut before
	# its first checkpoint, not thrown wrong. Same end-of-run class as the elapsed guard above.
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
	# global ledger reads two characters' health as one broken sequence. Reachable whenever the attack
	# interval aliases against the auto-revive, so it is a phase race that passes most runs.
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

parry_window_spans() { # until= minus the timestamp it was printed at; blockstun_spans' shape
	awk '
		/^\[[0-9.]+\] PARRY WINDOW open/ {
			t=$1; gsub(/[\[\]]/,"",t)
			for (i=1;i<=NF;i++) if ($i ~ /^until=/) { split($i,a,"="); printf "%.3f\n", a[2]-t }
		}' "$SLICE"
}

parry_recovery_spans() { # same, for State.ParryRecovery
	# **One cause, and by construction rather than by fixture discipline.** The dodge's gap is
	# State.DodgeRecovery and prints DODGE RECOVERY, so this pattern cannot pick it up -- were they
	# to share a tag, a log containing dodges would yield two spans and a band expecting one would
	# fail on the other.
	awk '
		/^\[[0-9.]+\] PARRY RECOVERY [^E]/ {
			t=$1; gsub(/[\[\]]/,"",t)
			for (i=1;i<=NF;i++) if ($i ~ /^until=/) { split($i,a,"="); printf "%.3f\n", a[2]-t }
		}' "$SLICE"
}

acts_during_parry_window() { # anything that activated while a parry window was live
	# **The lockout's first half.** Throwing a parry commits you from activation, not from window close,
	# so an attack, dodge or block starting inside a live window is the failure. The window ends on
	# any of the three exits: a catch (PARRY SUCCESS), the whiff charging (PARRY WHIFF), or an
	# attacker's punishment cancelling it (HITSTUN / GUARD BREAK). Disarming on all of them matters
	# -- after a punishment the parrier is legitimately in someone else's state, and what they do
	# then belongs to that state's rules. **Scoped to the parrier by name**, which is why ACTIVATE
	# carries an avatar: without it the attacker's swings land inside the defender's windows on
	# every fixture where both act, and every one reads as a violation.
	awk '
		/^\[[0-9.]+\] PARRY WINDOW open/ {
			who=""
			for (i=1;i<=NF;i++) if ($i=="on") { who=$(i+1); break }
			in_win=1; next
		}
		/^\[[0-9.]+\] (PARRY SUCCESS|PARRY WHIFF|HITSTUN|GUARD BREAK)/ { in_win=0; next }
		in_win && who != "" && $0 ~ ("ACTIVATE +" who " +swing=") { print $0; next }
		in_win && who != "" && $0 ~ ("BLOCK +cost .* on " who) { print $0; next }
		# DODGE carries no avatar name, so it cannot be scoped. Left in deliberately: this
		# scenario has neither combatant dodging, so any DODGE at all is a regression worth
		# seeing, and a fixture that adds one must revisit this line.
		in_win && /^\[[0-9.]+\] DODGE +dir=/ { print $0 }' "$SLICE"
}

# --- what "an ability started" actually looks like in this trace ----------------
# Both helpers here name their markers explicitly rather than guessing, because the trace tags do
# not read the way their mechanics are named:
#
#   attack  ->  "ACTIVATE   swing=0 pos=... windupRate ..."   (there is NO "ATTACK" tag)
#   dodge   ->  "DODGE      dir=Bw section=..."               ("DODGE END" is not a start)
#   block   ->  "BLOCK      cost 10 on ...  remaining=..."    (multiple spaces, not one)
#   parry   ->  "PARRY WINDOW open ...  until=..."
#
# A pattern matching /ATTACK|DODGE |BLOCK cost/ matches **nothing a real log contains** -- while
# reporting PASS on 32 recovery spans, because the n=0 guard counts *spans* and not detectable
# events. **A hand-written fixture inherits the author's misconceptions; prove an extractor
# against a real log slice too.**

assert_nothing_acts_during_parry_window() {
	local bad n
	bad=$(acts_during_parry_window)
	n=$(grep -c "^\[[0-9.]*\] PARRY WINDOW open" "$SLICE" || true)
	# n=0 fails rather than passing vacuously, for the reason assert_never_inward gives.
	if [ "$n" -eq 0 ]; then
		check "nothing acts during a parry window" 1 "no PARRY WINDOW lines at all -- nothing was asserted"
	elif [ -z "$bad" ]; then
		check "nothing acts during a parry window" 0 "n=$n windows, no ability started inside any of them"
	else
		check "nothing acts during a parry window" 1 "$(echo "$bad" | head -3 | tr '\n' ' ')"
	fi
}

acts_during_parry_recovery() { # anything that activated while a recovery was running
	# **An absence, which is what the rule actually claims.** "You can't act during parry recovery"
	# is a claim about what did *not* happen, so counting refusals is not enough -- a refusal proves
	# one press was stopped, never that none got through. This walks the span instead and reports
	# any ability that started inside it, which is the thing that must be empty.
	awk '
		/^\[[0-9.]+\] PARRY RECOVERY [^E]/ {
			t=$1; gsub(/[\[\]]/,"",t)
			who=$4
			for (i=1;i<=NF;i++) if ($i ~ /^until=/) { split($i,a,"="); until_t=a[2] }
			in_rec=1; next
		}
		/^\[[0-9.]+\] PARRY RECOVERY END/ { in_rec=0; next }
		in_rec {
			t=$1; gsub(/[\[\]]/,"",t)
			if (t+0 >= until_t+0) next
			# Scoped to the recovering character, for the reason acts_during_parry_window gives.
			if (who != "" && $0 ~ ("ACTIVATE +" who " +swing=")) { print $0; next }
			if (who != "" && $0 ~ ("BLOCK +cost .* on " who)) { print $0; next }
			if ($0 ~ /^\[[0-9.]+\] DODGE +dir=/) { print $0; next }
			if ($0 ~ ("PARRY WINDOW open on " who)) { print $0; next }
		}' "$SLICE"
}

parry_success_gained() { grep "^\[[0-9.]*\] PARRY SUCCESS" "$SLICE" | grep -o "gained=[0-9.-]*" | cut -d= -f2; }

parry_success_gained_after_spend() { # gained= on successes whose pre-block actually spent
	# **Selected by position, never by the value under test.** A cycle whose guard was refused into
	# the knockdown lockout spends nothing, so its bar sits at full and the clamp eats the whole
	# reward -- correctly. Those cycles are identifiable before looking at gained= at all: the guard
	# either released or it did not. Filtering on the reward instead would leave an assertion that
	# can only fail via "no samples".
	awk '
		$2 == "BLOCK" && $3 == "down" && /\(released\)/ { spent=1; next }
		$2 == "REFUSED" && /knocked down/              { spent=0; next }
		$2 == "PARRY" && $3 == "SUCCESS" {
			if (spent) { for (i=1;i<=NF;i++) if ($i ~ /^gained=/) { split($i,a,"="); print a[2] } }
			spent=0
		}' "$SLICE"
}

parry_grace_spans() { # Grace's authored tail, read off until= like the recoveries
	awk '
		/^\[[0-9.]+\] PARRY GRACE [^E]/ {
			t=$1; gsub(/[\[\]]/,"",t)
			for (i=1;i<=NF;i++) if ($i ~ /^until=/) { split($i,a,"="); printf "%.3f\n", a[2]-t }
		}' "$SLICE"
}

grace_rearm_violations() { # a Grace tail that started from anything other than a window catch
	# **The no-re-arm rule, asserted as an identity rather than a timing.** Only a catch made by an
	# open *window* may start a tail; a catch made by Grace itself pays the full reward and starts
	# nothing, or "protected from everything" would quietly extend itself through the protection it
	# grants. So every PARRY GRACE must be immediately preceded by a by=window success, and a
	# by=grace success must never be followed by one. Checked pairwise in log order rather than by
	# counting: equal totals would also be produced by one window catch starting two tails while a
	# grace catch started none.
	awk '
		/^\[[0-9.]+\] PARRY SUCCESS/ {
			last_by = ($0 ~ /by=window/) ? "window" : "grace"
			seen_success = 1
			next
		}
		/^\[[0-9.]+\] PARRY GRACE [^E]/ {
			if (!seen_success)      { print "grace with no preceding success: " $0; next }
			if (last_by != "window") { print "grace started by a grace catch: " $0; next }
			# consumed -- a second tail before the next success would be a re-arm
			last_by = "consumed"
		}' "$SLICE"
}

grace_overlap_violations() { # a tail opening while one is already running
	awk '
		/^\[[0-9.]+\] PARRY GRACE [^E]/ { if (open) print "grace opened while one was running: " $0; open=1; next }
		/^\[[0-9.]+\] PARRY GRACE END/  { open=0 }' "$SLICE"
}

parried_string_violations() { # a parried swing must not open a link window
	# The next STRING line after each PARRY SUCCESS has to be a reset, never "link window open".
	# Looking forward rather than pairing by swing index because the parried attacker is named on
	# the WAIVER/STRING lines but not on the parry's own, and the fixture is 1v1 -- so the very next
	# STRING event is unambiguously this swing's. A second attacker would need the name.
	awk '
		/^\[[0-9.]+\] PARRY SUCCESS/ { armed=1; next }
		armed && /^\[[0-9.]+\] STRING/ {
			if ($0 ~ /link window open/) print $0
			armed=0
		}' "$SLICE"
}

waiver_dodge_latency_ms() { # ms from each DAMAGED to the next DODGE, the waiver's own latency
	awk '
		/^\[[0-9.]+\] DAMAGED/ { t=$1; gsub(/[\[\]]/,"",t); hit=t; armed=1; next }
		armed && /^\[[0-9.]+\] DODGE      dir=/ {
			t=$1; gsub(/[\[\]]/,"",t)
			printf "%.0f\n", (t-hit)*1000
			armed=0
		}' "$SLICE"
}

swing_index_counts() { # "<count> <swing index>" per index, so a string shows equal counts
	grep -oE "ACTIVATE +[A-Za-z_0-9]+ +swing=[0-9]+" "$SLICE" | cut -d= -f2 | sort | uniq -c
}

chains_after_first_parry() { # chain-outs occurring after the first PARRY SUCCESS in the run
	awk '
		/^\[[0-9.]+\] PARRY SUCCESS/ { seen=1; next }
		seen && /^\[[0-9.]+\] STRING     chain out of swing/ { n++ }
		END { print n+0 }' "$SLICE"
}

chain_gaps() { # seconds between an ACTIVATE and the chained ACTIVATE after it
	# Emitted only when the *arriving* swing index is non-zero: index 0 opens a new string, and the
	# gap from the previous cycle's ender is the fixture's 3 s interval, not a cadence sample.
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

blocked_knockback_count() { grep -c "^\[[0-9.]*\] KNOCKBACK.*(blocked)" "$SLICE" || true; }
blocked_hit_count() { grep -c "^\[[0-9.]*\] BLOCKED" "$SLICE" || true; }

dodges_inside_hitstun() { # DODGE lines falling between a HITSTUN and its HITSTUN END
	awk '
		/^\[[0-9.]+\] HITSTUN    / { inside=1; next }
		/^\[[0-9.]+\] HITSTUN END/ { inside=0; next }
		inside && /^\[[0-9.]+\] DODGE      dir=/ { n++ }
		END { print n+0 }' "$SLICE"
}


targets_per_window() { # "<string> <attack index> <distinct targets damaged in that window>"
	# **Every string.** The radial carry sends each victim out along its own bearing rather than onto
	# the attacker's facing axis, so nobody ends up inside the 60-degree wedge and the
	# discrimination survives every string rather than only the first.
	awk '
		/^\[[0-9.]+\] ACTIVATE/ {
			for (i=1;i<=NF;i++) if ($i ~ /^swing=/) { split($i,a,"="); sw=a[2] }
			if (sw+0 == 0) { strung++ }
			next
		}
		/^\[[0-9.]+\] RELEASE BEGIN/ { inwin=1; delete seen; n=0; next }
		/^\[[0-9.]+\] RELEASE END/   { if (inwin) print strung " " sw " " n; inwin=0; next }
		inwin && /^\[[0-9.]+\] DAMAGED/ { if (!($3 in seen)) { seen[$3]=1; n++ } }' "$SLICE"
}

guardstun_spans() { # GUARD BREAK to the next GUARD END
	awk '
		/^\[[0-9.]+\] GUARD BREAK/ { t=$1; gsub(/[\[\]]/,"",t); b=t+0; have=1; next }
		have && /^\[[0-9.]+\] GUARD END/ {
			t=$1; gsub(/[\[\]]/,"",t); printf "%.3f\n", t-b; have=0
		}' "$SLICE"
}


# --- knockdown extractors ---------------------------------------------------
# Keyed per victim, because two bodies can be down at once and pairing the wrong
# entry with the wrong rise yields a plausible number that means nothing -- the
# same trap two-player logs set for the whole checker.

kd_entry_to_rise_by() { # seconds from KNOCKDOWN to that victim's RISE; $1 filters on by=
	awk -v want="$1" '
		/^\[[0-9.]+\] KNOCKDOWN  / {
			t=$1; gsub(/[\[\]]/,"",t); down[$3]=t+0; next
		}
		/^\[[0-9.]+\] KNOCKDOWN RISE/ {
			t=$1; gsub(/[\[\]]/,"",t);
			reason=$5; sub(/^by=/,"",reason);
			if ($4 in down) {
				if (want=="" || reason==want) printf "%.3f\n", (t+0)-down[$4];
				delete down[$4]
			}
		}
	' "$SLICE"
}


kd_entry_to_rise() { kd_entry_to_rise_by ""; }

kd_rise_to_stand() { # seconds from each RISE to that victim's STAND
	awk '
		/^\[[0-9.]+\] KNOCKDOWN RISE/ {
			t=$1; gsub(/[\[\]]/,"",t); rise[$4]=t+0; next
		}
		/^\[[0-9.]+\] KNOCKDOWN STAND/ {
			t=$1; gsub(/[\[\]]/,"",t);
			if ($4 in rise) { printf "%.3f\n", (t+0)-rise[$4]; delete rise[$4] }
		}
	' "$SLICE"
}

kd_types() { # one type token per KNOCKDOWN entry
	grep '^\[[0-9.]*\] KNOCKDOWN  ' "$SLICE" | grep -oE 'type=[a-z]+' | sed 's/type=//'
}

kd_rise_span_by() { # seconds from each RISE to its own stands=, filtered on by=$1
	awk -v want="by=$1" '
		/^\[[0-9.]+\] KNOCKDOWN RISE/ {
			t=$1; gsub(/[\[\]]/,"",t)
			ok=0; span=""
			for (i=1;i<=NF;i++) {
				if ($i == want) ok=1
				if ($i ~ /^stands=/) { split($i,a,"="); span=a[2] }
			}
			if (ok && span != "") printf "%.3f\n", span-t
		}' "$SLICE"
}

kd_rise_reasons() { # one by= token per RISE
	grep '^\[[0-9.]*\] KNOCKDOWN RISE' "$SLICE" | grep -oE 'by=[a-z]+' | sed 's/by=//'
}

kd_damage_while_down() { # count of DAMAGED landing between a victim's KNOCKDOWN and its RISE
	awk '
		/^\[[0-9.]+\] KNOCKDOWN  / { down[$3]=1; next }
		/^\[[0-9.]+\] KNOCKDOWN RISE/ { delete down[$4]; next }
		/^\[[0-9.]+\] DAMAGED/ { if ($3 in down) n++ }
		END { print n+0 }
	' "$SLICE"
}

kd_fall_overruns_lockout() { # authored montage span vs the lockout it sits in, per knockdown
	# played/rate, not want=: once a portion is fitted the tail after it still plays, so the
	# montage outlasts the carry and is the binding number. Read off the authored fields rather
	# than measured as a span, so the check holds at any frame rate and rejects a raise past the
	# ceiling directly instead of waiting for a slow machine to expose it.
	awk '
		/^\[[0-9.]+\] KNOCKDOWN MONTAGE/ && / fall / {
			played=""; rate=""; from=0
			for (i=1;i<=NF;i++) {
				if ($i ~ /^played=/) { split($i,a,"="); played=a[2] }
				if ($i ~ /^rate=/)   { split($i,a,"="); rate=a[2] }
				if ($i ~ /^from=/)   { split($i,a,"="); from=a[2] }
			}
			span = (rate+0 > 0) ? (played - from)/rate : ""
			next
		}
		/^\[[0-9.]+\] KNOCKDOWN  / {
			lock=""
			for (i=1;i<=NF;i++) if ($i ~ /^lockout=/) { split($i,a,"="); lock=a[2] }
			if (span != "" && lock != "" && span+0 >= lock+0)
				print sprintf("%.3f", span) "s montage >= " lock "s lockout"
			span=""
		}' "$SLICE"
}


parry_lockout_spans() { # one span per PARRY LOCKOUT, from its start to its printed until=
	awk '
		/^\[[0-9.]+\] PARRY LOCKOUT  / {
			t=$1; gsub(/[\[\]]/,"",t);
			for (i=1;i<=NF;i++) if ($i ~ /^until=/) { u=substr($i,7); printf "%.3f\n", (u+0)-(t+0) }
		}
	' "$SLICE"
}

damage_during_parry_lockout() { # DAMAGED dealt BY an attacker while it is serving one
	awk '
		/^\[[0-9.]+\] PARRY LOCKOUT  / { locked[$4]=1; next }
		/^\[[0-9.]+\] PARRY LOCKOUT END/ { delete locked[$5]; next }
		/^\[[0-9.]+\] DAMAGED/ { for (a in locked) if (index($0, " by " a)) n++ }
		END { print n+0 }
	' "$SLICE"
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

	# DodgeRecoverySeconds is retired to 0, so the gap must never fire. A stale CDO or per-instance
	# override would silently reinstate it and nothing else in the loop would notice. Guarded
	# against the vacuous pass: an absence asserted over a log with no dodges proves nothing.
	if [ "$starts" -eq 0 ]; then
		check "no DODGE RECOVERY (gap retired)" 1 "no dodges in log; absence proves nothing"
	else
		assert_count "no DODGE RECOVERY (gap retired)" 			"$(grep -c 'DODGE RECOVERY' "$SLICE")" 0
	fi

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
	# recovery takes ~5s and StopPIE does not wait for it. That is truncation, not a leak, tolerated
	# only when the unclosed one is the LAST line of the pair-stream; an unclosed exhaustion earlier
	# is a genuine stuck state and still fails. Same end-of-run family as the elapsed guard.
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

run_s5_parry() {
	local resumed successes violations

	# The window itself. Mechanical, not a notify, so this is the one place its length is visible.
	assert_all_in_band "PARRY WINDOW span" "parry_window_spans" \
		"$(awk -v v="$BAND_PARRY_WINDOW" -v t="$BAND_PARRY_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_PARRY_WINDOW" -v t="$BAND_PARRY_SPAN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"

	# A run with no successes is not a pass -- it is a fixture that never met an attack, which is
	# exactly what an aliasing interval produces while looking healthy. n=0 fails, as it does for
	# knockback in s4-string and for the same reason.
	successes=$(grep -c "^\[[0-9.]*\] PARRY SUCCESS" "$SLICE" || true)
	if [ "$successes" -gt 0 ]; then
		check "PARRY SUCCESS observed" 0 "$successes"
	else
		check "PARRY SUCCESS observed" 1 "none -- the parry interval may be aliasing against the attacker's"
	fi

	# The credited reward, clamp included. See the band's comment for why this is not an equality.
	assert_all_in_band "parry reward within clamp" "parry_success_gained" \
		"$BAND_PARRY_GAINED_MIN" "$BAND_PARRY_GAINED_MAX"

	assert_gesture_inside_window
	assert_parry_grace

	# "No more games": a parried swing takes the string with it, so no link window may follow one.
	violations=$(parried_string_violations | grep -c '[0-9]' || true)
	assert_count "no STRING continuation after a parry" "$violations" 0

	# **The other half of the same rule, and the half that regressed.** A parried swing takes its
	# string with it -- asserted directly above -- but the *next* attack must chain normally. The
	# flag that forbids chaining is per-instance, and attack abilities are InstancedPerActor, so one
	# left standing disables chaining for the session rather than for one string. Nothing else in
	# the loop would see it: s4-string runs with defence Off and never faces a parried attacker.
	resumed=$(chains_after_first_parry)
	check "chaining resumes after a parry" "$([ "$resumed" -gt 0 ] && echo 0 || echo 1)" \
		"$resumed chain-outs after the first PARRY SUCCESS"
	# **The lockout is authored, so this asserts a CDO value rather than an arithmetic result.** A
	# derived one would vary with the elapsed time at the catch, so **a span that starts wobbling
	# again means something is computing it**.
	assert_all_in_band "PARRY LOCKOUT span" parry_lockout_spans \
		"$(awk -v v="$BAND_PARRY_LOCKOUT_LIGHT" -v t="$BAND_PARRY_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_PARRY_LOCKOUT_LIGHT" -v t="$BAND_PARRY_SPAN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"

	# A caught swing ends at the catch, so its hitbox is dead for everyone -- the half of sub-slice
	# E that only matters in a crowd. In 1v1 this can only show that the *parried attacker* deals no
	# damage while locked out, which is the observable corner of it.
	local lockdmg
	lockdmg=$(damage_during_parry_lockout)
	check "no damage from a locked-out attacker" "$([ "$lockdmg" -eq 0 ] && echo 0 || echo 1)" \
		"$lockdmg DAMAGED dealt during a PARRY LOCKOUT"
}

run_s5_parry_reward() {
	local successes spent

	# n=0 fails: a run with no successes proves nothing about a reward, and this fixture is the
	# fussier of the two -- the parry has to land in the ~1.1 s after the guard drops and before
	# regen has climbed back above the clamp threshold, so a mistuned pre-block reads as silence.
	successes=$(grep -c "^\[[0-9.]*\] PARRY SUCCESS" "$SLICE" || true)
	if [ "$successes" -gt 0 ]; then
		check "PARRY SUCCESS observed" 0 "$successes"
	else
		check "PARRY SUCCESS observed" 1 "none -- no parry landed, so the reward was never credited"
	fi

	# **The magnitude, not the clamp.** This is the assertion s5-parry cannot make, and the whole
	# reason DebugParryPreBlockSeconds exists. A sample reading 0.0 here means the parrier was at
	# full stamina when it parried -- the pre-block did not spend, or regen had already refilled it.
	#
	# **Scoped to cycles whose guard spent, because knockdown shares this fixture.** The ender floors
	# the parrier, a pre-block landing in the lockout is refused, and that cycle legitimately credits
	# 0. Those cycles are not failures and asserting across them measures the fixture rather than the
	# reward. The floored cycles are also what keeps this scenario runnable at all -- bDebugHomeAtStand
	# restores the placed spacing at each stand, and without it the defender ratchets out of reach
	# within a dozen attacks.
	spent=$(parry_success_gained_after_spend | grep -c . || true)
	if [ "$spent" -gt 0 ]; then
		check "a pre-block actually spent" 0 "$spent of $successes successes followed a released guard"
	else
		check "a pre-block actually spent" 1 \
			"0 of $successes successes -- every guard was refused, so nothing tests the magnitude"
	fi

	assert_all_equal "parry reward credits in full" "parry_success_gained_after_spend" \
		"$BAND_PARRY_GAINED_EXACT"
}

run_s5_parry_whiff() {
	local win_refusals rec_refusals

	assert_all_in_band "PARRY RECOVERY span" "parry_recovery_spans" \
		"$(awk -v v="$BAND_PARRY_RECOVERY" -v t="$BAND_PARRY_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_PARRY_RECOVERY" -v t="$BAND_PARRY_SPAN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"

	# The lockout has to actually refuse something, or it is a rule nobody reads. The fixture presses
	# on its own schedule, so presses land inside a running lockout by construction.
	#
	# **Matched on the reason string, not the tag name.** Both halves are refused in
	# UTDGameplayAbility::CanActivateAbility rather than through ActivationBlockedTags, so neither
	# reaches the tag-filter path that prints tag names -- they print their own reasons, as "dead"
	# and "guard broken" do. Grepping for a tag here returns zero forever and reads as the refusal
	# being broken.
	#
	# **Both halves are asserted separately**, the tag tracking the window rather than the ability,
	# so each phase refuses under its own name. **A single count passing tells you the lockout refused
	# something; two tell you which half.**
	win_refusals=$(grep "^\[[0-9.]*\] REFUSED" "$SLICE" | grep -c ": parrying" || true)
	rec_refusals=$(grep "^\[[0-9.]*\] REFUSED" "$SLICE" | grep -c "parry recovery" || true)

	if [ "$win_refusals" -gt 0 ]; then
		check "REFUSED names the parry window" 0 "$win_refusals"
	else
		check "REFUSED names the parry window" 1 "none -- the window refused nothing"
	fi

	if [ "$rec_refusals" -gt 0 ]; then
		check "REFUSED names parry recovery" 0 "$rec_refusals"
	else
		check "REFUSED names parry recovery" 1 "none -- the recovery refused nothing"
	fi

	# **The rule itself, asserted as an absence.** The refusal count above proves the recovery stopped
	# *a* press; this proves nothing got through. An attack, dodge, block or second parry starting
	# inside the span is the failure this scenario exists to catch.
	assert_nothing_acts_during_parry_recovery
	assert_nothing_acts_during_parry_window
}

gesture_outside_window() { # PARRY GESTURE lines that fall outside their own window's span
	# The clip's read moment must land while the parry is actually live. If it drifts past the
	# close, the character is seen catching a blow after the window has gone -- the fit silently
	# wrong in the one way play would blame on the mechanic. **Scoped to the parrier by name, and it
	# is not paranoia**: an animation editor left open on AM_Parry fires this notify from its own
	# preview actor, on a loop, into the same log, every preview gesture falling outside any window,
	# so an unscoped assertion reports a montage fault that does not exist. **Close the animation
	# editor before measuring**, and let this filter cover the times somebody forgets.
	awk -v TOL="$BAND_PARRY_SPAN_TOL" '
		/^\[[0-9.]+\] PARRY WINDOW open/ {
			t=$1; gsub(/[\[\]]/,"",t); open_t=t; open_seen=1
			who=""
			for (i=1;i<=NF;i++) if ($i=="on") { who=$(i+1); break }
			for (i=1;i<=NF;i++) if ($i ~ /^until=/) { split($i,a,"="); close_t=a[2] }
			next
		}
		/^\[[0-9.]+\] PARRY GESTURE/ {
			# **$4, not $3.** The actor is the fourth field -- "[t] PARRY GESTURE <Actor> pos=..."
			# -- and comparing $3 skips every line, which is exactly what it did on first writing:
			# the assertion passed on 30 samples while examining none of them.
			if (who == "" || $4 != who) next
			t=$1; gsub(/[\[\]]/,"",t)
			# **The late tolerance is structural, not slop.** The gesture fires when the montage
			# reaches the marker, and the window rate is derived so that happens at exactly
			# ParryWindowSeconds -- but Montage_Play is called during ActivateAbility and the first
			# montage advance lands a tick later, while until= was stamped immediately. So the
			# gesture reliably trails the close by about a frame: measured 1-14 ms across 13 samples,
			# never early. A hard boundary would fail forever on a correct clip; widening past the
			# shared span tolerance would not, and that is the point at which a marker really is
			# misplaced.
			#
			# NOTE: no apostrophes anywhere in this awk program. A single quote inside a
			# single-quoted shell string closes it, and the rest of the program silently stops
			# reaching awk. bash -n does not catch it: the result is still valid shell, it just
			# means something else.
			if (!open_seen || t+0 < open_t+0 || t+0 > close_t+0 + TOL) print $0
		}' "$SLICE"
}

assert_parry_grace() {
	local n wins bad

	# The tail's authored length. Vacuous-pass guarded by the count check below rather than here,
	# since assert_all_in_band already fails on an empty sample set.
	assert_all_in_band "PARRY GRACE span" "parry_grace_spans" \
		"$(awk -v v="$BAND_PARRY_GRACE" -v t="$BAND_PARRY_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_PARRY_GRACE" -v t="$BAND_PARRY_SPAN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"

	# **Every window catch starts exactly one tail.** n=0 on either side fails: a run with no
	# successes proves nothing about Grace, and a run with successes but no Grace is the mechanic
	# silently not firing -- which is what this scenario exists to catch.
	n=$(grep -c "^\[[0-9.]*\] PARRY GRACE [^E]" "$SLICE" || true)
	wins=$(grep "^\[[0-9.]*\] PARRY SUCCESS" "$SLICE" | grep -c "by=window" || true)
	if [ "$wins" -eq 0 ]; then
		check "every window catch starts Grace" 1 "no by=window successes -- nothing was asserted"
	elif [ "$n" -eq "$wins" ]; then
		check "every window catch starts Grace" 0 "n=$n tails for $wins window catches"
	else
		check "every window catch starts Grace" 1 "$n tails for $wins window catches"
	fi

	# **Grace does not re-arm**, the property that keeps it bounded. Two independent readings:
	# nothing but a window catch may start a tail, and no tail may open while one is running.
	bad=$(printf '%s\n%s\n' "$(grace_rearm_violations)" "$(grace_overlap_violations)" | grep -v '^$' || true)
	if [ "$n" -eq 0 ]; then
		check "Grace never re-arms" 1 "no PARRY GRACE lines at all -- nothing was asserted"
	elif [ -z "$bad" ]; then
		check "Grace never re-arms" 0 "n=$n tails, each from one window catch and none overlapping"
	else
		check "Grace never re-arms" 1 "$(echo "$bad" | head -3 | tr '\n' ' ')"
	fi
}

assert_gesture_inside_window() {
	local bad n
	bad=$(gesture_outside_window)
	n=$(grep -cE "^\[[0-9.]+\] PARRY GESTURE BP_" "$SLICE" || true)
	# **n=0 fails, and the message says which of two causes it is**, because both are real and they
	# need different fixes: the montage may not be assigned to GA_Parry, or it may be assigned with
	# no Parry Gesture marker placed on it. Notify placement cannot be read off the asset by any
	# tool, so this line and the ungated warning in UTDParryAbility::PlayParryMontage are between
	# them the only things that can report a missing marker at all.
	if [ "$n" -eq 0 ]; then
		check "parry gesture reads inside the window" 1 \
			"no PARRY GESTURE lines -- AM_Parry is unassigned, or carries no Parry Gesture marker"
	elif [ -z "$bad" ]; then
		check "parry gesture reads inside the window" 0 "n=$n, every gesture inside its own window"
	else
		check "parry gesture reads inside the window" 1 "$(echo "$bad" | head -3 | tr '\n' ' ')"
	fi
}

assert_nothing_acts_during_parry_recovery() {
	local bad n
	bad=$(acts_during_parry_recovery)
	n=$(parry_recovery_spans | grep -c . || true)
	# n=0 must FAIL rather than pass on an empty set, exactly as assert_never_inward does. A build
	# where the recovery stopped being charged at all would otherwise report "nothing acted during
	# it" as green while asserting nothing whatsoever -- and that build is precisely the regression
	# this scenario exists to catch, so the vacuous pass would hide its own target.
	if [ "$n" -eq 0 ]; then
		check "nothing acts during parry recovery" 1 "no PARRY RECOVERY spans at all -- nothing was asserted"
	elif [ -z "$bad" ]; then
		check "nothing acts during parry recovery" 0 "n=$n spans, no ability started inside any of them"
	else
		check "nothing acts during parry recovery" 1 "$(echo "$bad" | head -3 | tr '\n' ' ')"
	fi
}

run_s5_cancel() {
	local releases costs damaged

	# The pre-commit cancel: a defensive action inside the attack's startup erases the swing before
	# it can ever damage. No release window means the hitbox never went live at all.
	releases=$(grep -c "^\[[0-9.]*\] RELEASE BEGIN" "$SLICE" || true)
	assert_count "cancelled swings never release" "$releases" 0

	damaged=$(grep -c "^\[[0-9.]*\] DAMAGED" "$SLICE" || true)
	assert_count "cancelled swings deal no damage" "$damaged" 0

	# The guard the cancel bought is real and charged for, once per cancel rather than once ever --
	# which is what says the fixture is re-pressing rather than holding a stale guard up.
	costs=$(grep -c "^\[[0-9.]*\] BLOCK      cost" "$SLICE" || true)
	if [ "$costs" -gt 0 ]; then
		check "BLOCK cost per cancel" 0 "$costs"
	else
		check "BLOCK cost per cancel" 1 "none -- no guard was raised, so nothing was cancelled"
	fi
}

run_s5_waiver() {
	local dodges

	# The waiver's whole observable claim: the attacker's own dodge comes out of its own recovery.
	# Before the waiver this press was refused by State.Attacking.Committed and produced a REFUSED
	# line instead, so a zero here is the rule silently not working.
	dodges=$(waiver_dodge_latency_ms | grep -c '[0-9]' || true)
	if [ "$dodges" -gt 0 ]; then
		check "attacker dodges out of its own hit" 0 "$dodges"
	else
		check "attacker dodges out of its own hit" 1 "none -- the commitment tag is still refusing"
	fi

	assert_all_in_band "waiver dodge latency" "waiver_dodge_latency_ms" \
		0 "$BAND_WAIVER_DODGE_MAX_MS" "ms"

	# And the movement half, which is the *derived* one: contact plus that swing's hitstun.
	local unlocks
	unlocks=$(grep -c "^\[[0-9.]*\] MOVE UNLOCK" "$SLICE" || true)
	if [ "$unlocks" -gt 0 ]; then
		check "MOVE UNLOCK observed" 0 "$unlocks"
	else
		check "MOVE UNLOCK observed" 1 "none -- movement never came back early"
	fi
}

assert_string_shape() { # every swing index in the string fires the same number of times
	local counts n_idx uneven
	counts=$(swing_index_counts)
	n_idx=$(printf '%s\n' "$counts" | grep -c '[0-9]')
	if [ "$n_idx" -ne "$BAND_STRING_SWINGS" ]; then
		check "string is $BAND_STRING_SWINGS swings" 1 "saw $n_idx distinct swing indices: $(echo $counts | tr '\n' ' ')"
		return
	fi
	# A string cut off by StopPIE leaves the earlier indices one ahead; tolerate exactly that.
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
	assert_string_shape
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
	assert_string_shape
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
	#
	# **What string 1 and the rest assert differently is reach, not the wedge.** After the finisher
	# floors both, bDebugHomeAtStand returns the dummy to its placed spacing at every stand and
	# never moves a player pawn -- so the player stays out at the carry's 450 and only the dummy
	# comes back within reach. String 1 therefore reaches two and every later string exactly one,
	# while attacks 1-2 reach nobody throughout.
	local rows early first_late later_late strings kdns
	rows=$(targets_per_window)
	strings=$(printf '%s\n' "$rows" | awk 'NF {print $1}' | sort -un | tail -1)
	strings=${strings:-0}

	# The whole point of lifting the exclusion is sampling past string 1, so one fails here
	# rather than passing on the old shape.
	check "more than one string observed" "$([ "${strings:-0}" -gt 1 ] && echo 0 || echo 1)" \
		"$strings strings"

	early=$(printf '%s\n' "$rows" | awk '$2 < 2 {print $3}' | sort -u | tr '\n' ' ')
	first_late=$(printf '%s\n' "$rows" | awk '$1 == 1 && $2 == 2 {print $3}' | sort -u | tr '\n' ' ')
	later_late=$(printf '%s\n' "$rows" | awk '$1 > 1 && $2 == 2 {print $3}' | sort -u | tr '\n' ' ')

	check "60-degree attacks reach neither, in every string" \
		"$([ "$(echo $early)" = "0" ] && echo 0 || echo 1)" \
		"attacks 1-2 damaged: ${early:-none} distinct targets across $strings strings"
	check "the 360 finisher reaches both in string 1" \
		"$([ "$(echo $first_late)" = "2" ] && echo 0 || echo 1)" \
		"string 1 attack 3 damaged: ${first_late:-none} distinct targets"
	check "and exactly the re-homed body after that" \
		"$([ "$(echo $later_late)" = "1" ] && echo 0 || echo 1)" \
		"later strings' attack 3 damaged: ${later_late:-none} distinct targets"

	# The ender's displacement is a knockdown now, so string 1 floors both bodies rather than
	# knocking them back. Asserted here because it is what lifted the exclusion.
	kdns=$(kd_types | grep -c "^normal$" || true)
	check "the finisher floors both bodies" "$([ "$kdns" -ge 2 ] && echo 0 || echo 1)" \
		"$kdns normal-type KNOCKDOWN lines"
}

run_s4_block() {
	assert_string_shape
	assert_all_equal "BLOCKED staminaDamage" "stamina_damage_values" "$BAND_STAMDMG_LIGHT"
	assert_all_in_band "BLOCKSTUN span" "blockstun_spans" \
		"$(awk -v v="$BAND_BLOCKSTUN_LIGHT" -v t="$BAND_BLOCKSTUN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_BLOCKSTUN_LIGHT" -v t="$BAND_BLOCKSTUN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"

	# Every blocked contact resets spacing, the ender included -- its knockdown type governs the
	# clean hit only. assert_never_inward cannot see this: a knockback that never fires just
	# lowers the sample count it reads, so the shortfall has to be counted directly.
	assert_count "blocked KNOCKBACK per BLOCKED" "$(blocked_knockback_count)" "$(blocked_hit_count)"
	assert_never_inward
}

clean_dodge_distances() {
	# Full-duration, uncontaminated samples only. The lateral gate excludes collisions (the
	# attacker shoving a mid-dodge body reads as right= drift); the duration gate excludes
	# dodges truncated by the session itself -- the last dodge before StopPIE ends mid-travel
	# with zero drift, which reads as a travel failure.
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
	# *position*, never by the value under test. **Filtering samples to the expected value and then
	# asserting it can only ever fail via "no samples"** -- the assertion is circular.
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


# --- S6: knockdown ----------------------------------------------------------
# The spans are type-invariant on purpose: both types total 2.5s and rise at
# 2.0. What the type changes is the lockout/input-window split inside that, and the lockout
# is only observable through a press -- see run_s6_stand.

run_s6() { # run_s6 <type>
	local want="$1" types wrong dmg kdn overruns
	types=$(kd_types)
	kdn=$(printf '%s\n' "$types" | grep -c . || true)

	# **Fails on n=0 rather than passing vacuously.** A run where nothing was
	# floored would otherwise report a clean sheet on every assertion below while
	# exercising none of them -- the class --self-test exists to rule out.
	if [ "$kdn" -eq 0 ]; then
		check "KNOCKDOWN fires" 1 "no KNOCKDOWN lines -- the swing's type is None, or nothing connected"
	else
		wrong=$(printf '%s\n' "$types" | grep -vc "^${want}$" || true)
		if [ "$wrong" -eq 0 ]; then
			check "KNOCKDOWN type" 0 "n=$kdn all type=$want"
		else
			check "KNOCKDOWN type" 1 \
				"expected $want, saw:$(printf '%s\n' "$types" | sort | uniq -c | tr -s ' \n' ' ')"
		fi
	fi

	assert_all_in_band "entry -> auto-rise" kd_entry_to_rise \
		"$(awk -v v="$BAND_KD_ENTRY_TO_RISE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_KD_ENTRY_TO_RISE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"

	assert_all_in_band "rise -> stand" kd_rise_to_stand \
		"$(awk -v v="$BAND_KD_RISE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_KD_RISE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"

	# Floor invincibility. Gated on kdn for the reason above: zero damage across
	# zero knockdowns is not evidence of anything.
	# The fall must land before the lockout hands the input window over, or a get-up begins
	# while the body is still sliding. Gated on kdn for the reason above.
	overruns=$(kd_fall_overruns_lockout)
	if [ "$kdn" -eq 0 ]; then
		check "fall lands inside lockout" 1 "no knockdowns -- nothing to check"
	elif [ -z "$overruns" ]; then
		check "fall lands inside lockout" 0 "n=$kdn, every fall inside its own lockout"
	else
		check "fall lands inside lockout" 1 "$(printf '%s' "$overruns" | tr '
' ';')"
	fi

	dmg=$(kd_damage_while_down)
	if [ "$kdn" -eq 0 ]; then
		check "zero DAMAGED while down" 1 "no knockdowns -- nothing was ever invincible to test"
	else
		check "zero DAMAGED while down" "$([ "$dmg" -eq 0 ] && echo 0 || echo 1)" \
			"$dmg DAMAGED across $kdn knockdowns"
	fi
}

run_s6_knockdown() {
	local reasons n wrong
	run_s6 normal
	# Nothing presses, so every exit must be the forced one. A stray by=stand here
	# means the fixture is pressing something it was not configured to press.
	reasons=$(kd_rise_reasons)
	n=$(printf '%s\n' "$reasons" | grep -c . || true)
	if [ "$n" -eq 0 ]; then
		check "every rise is by=auto" 1 "no rises at all"
	else
		wrong=$(printf '%s\n' "$reasons" | grep -vc '^auto$' || true)
		check "every rise is by=auto" "$([ "$wrong" -eq 0 ] && echo 0 || echo 1)" \
			"n=$n rises, $wrong not auto"
	fi
}

run_s6_hard() {
	run_s6 hard
}

run_s6_stand() {
	# The lockout made observable: a jump press inside it is refused and names the
	# phase, and the first press *after* it fires the neutral stand. So the stand's
	# arrival is a lower bound on the lockout and an upper bound below the auto-rise.
	local lockout_refusals stands
	lockout_refusals=$(grep -c "knocked down (lockout)" "$SLICE" || true)
	check "REFUSED names the lockout" "$([ "$lockout_refusals" -gt 0 ] && echo 0 || echo 1)" \
		"$lockout_refusals presses refused inside the lockout"

	stands=$(kd_rise_reasons | grep -c '^stand$' || true)
	check "stand fires as a get-up" "$([ "$stands" -gt 0 ] && echo 0 || echo 1)" \
		"$stands rises by=stand"

	# A chosen stand must land inside the input window: at or after the lockout's end
	# and strictly before the auto-rise would have taken it.
	assert_all_in_band "stand inside input window" \
		"kd_entry_to_rise_by stand" "$BAND_KD_LOCKOUT_NORMAL" \
		"$(awk -v v="$BAND_KD_ENTRY_TO_RISE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" "s"
}

getup_press_to_release() { # ms from each fixture get-up attack press to the next RELEASE BEGIN
	awk '
		/^\[[0-9.]+\] DEBUG GETUP  .* mode=attack/ { t=$1; gsub(/[\[\]]/,"",t); p=t+0; have=1; next }
		have && /^\[[0-9.]+\] RELEASE BEGIN/ { t=$1; gsub(/[\[\]]/,"",t); printf "%.0f\n", (t-p)*1000; have=0 }
	' "$SLICE"
}

getup_elapsed() { # elapsed= of every completed get-up attack, read off its own ABILITY END line
	grep -E '^\[[0-9.]+\] ABILITY END  GA_GetUpAttack' "$SLICE" | grep -v "(cancelled)" | grep -o "elapsed=[0-9.]*" | cut -d= -f2
}

getup_damage() { # DAMAGED dealt by a pawn the fixture made press the get-up attack
	awk '
		/^\[[0-9.]+\] DEBUG GETUP  / { riser[$4]=1; next }
		/^\[[0-9.]+\] DAMAGED/ { for (r in riser) if (index($0, " by " r)) n++ }
		END { print n+0 }
	' "$SLICE"
}

getup_string_lines_after_rise() { # STRING lines naming a riser after its attack get-up
	awk '
		/^\[[0-9.]+\] KNOCKDOWN RISE  / && /by=attack/ { rose[$4]=1; next }
		/^\[[0-9.]+\] STRING/ { for (r in rose) if (index($0, r)) n++ }
		END { print n+0 }
	' "$SLICE"
}

run_s6_getup() {
	# The get-up attack as a get-up: the fixture presses it just inside the hard input
	# window, and it must rise the body, open its window on the authored clock, run to the
	# authored total, land on the attacker, and never join a string.
	local presses rises damaged strings
	presses=$(grep -c 'DEBUG GETUP  .* mode=attack' "$SLICE" || true)
	check "fixture pressed the get-up attack" "$([ "$presses" -gt 0 ] && echo 0 || echo 1)" \
		"$presses presses"

	rises=$(kd_rise_reasons | grep -c '^attack$' || true)
	check "get-up attack fires as a get-up" "$([ "$rises" -gt 0 ] && echo 0 || echo 1)" \
		"$rises rises by=attack"

	assert_all_in_band "rise inside the hard input window" \
		"kd_entry_to_rise_by attack" "$BAND_KD_LOCKOUT_HARD" \
		"$(awk -v v="$BAND_KD_ENTRY_TO_RISE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" "s"

	assert_all_in_band "press to RELEASE BEGIN" getup_press_to_release \
		$((BAND_RELEASE_GETUP - BAND_RELEASE_TOL)) $((BAND_RELEASE_GETUP + BAND_RELEASE_TOL)) "ms"

	assert_all_in_band "get-up attack total" getup_elapsed \
		"$(awk -v v="$BAND_ELAPSED_GETUP" -v t="$BAND_ELAPSED_MIN" 'BEGIN{printf "%.3f", v+t}')" \
		"$(awk -v v="$BAND_ELAPSED_GETUP" -v t="$BAND_ELAPSED_MAX" 'BEGIN{printf "%.3f", v+t}')" "s"

	damaged=$(getup_damage)
	check "riser's attack lands on the attacker" "$([ "$damaged" -gt 0 ] && echo 0 || echo 1)" \
		"$damaged DAMAGED by the riser"

	strings=$(getup_string_lines_after_rise)
	check "no STRING line for the riser after its attack get-up" "$([ "$strings" -eq 0 ] && echo 0 || echo 1)" \
		"$strings STRING lines"
}

# --- S6: the get-up options (sub-slice D) -----------------------------------
# Each option gets its own fixture and its own scenario. They are not folded together because a
# scenario whose assertions depend on which DebugGetUpMode ran can pass vacuously on the mode it
# was not given -- the same shape as the n=0 guards above.
#
# Every helper anchors on the RISE rather than on the DEBUG GETUP press: the press only proves the
# fixture fired, while the rise proves the option was actually taken. A refused press produces the
# first and not the second, which is exactly what the exhaustion scenarios assert.

getup_mode_presses() { # DEBUG GETUP lines the fixture emitted in mode $1
	grep -c "DEBUG GETUP  .* mode=$1" "$SLICE" || true
}

getup_rises_by() { # count of rises whose by= token matches $1
	kd_rise_reasons | grep -cE "^($1)$" || true
}

getup_dodge_remaining() { # remaining= on the DODGE each $1 get-up opened with
	awk -v want="$1" '
		$2 == "KNOCKDOWN" && $3 == "RISE" { split($5,b,"="); armed = (b[2] ~ "^(" want ")$"); next }
		armed && $2 == "DODGE" && $3 ~ /^dir=/ {
			for (i=1;i<=NF;i++) if ($i ~ /^remaining=/) { split($i,a,"="); print a[2] }
			armed=0
		}' "$SLICE"
}

getup_dodge_travel() { # dist= on the DODGE END that closed each $1 get-up
	awk -v want="$1" '
		$2 == "KNOCKDOWN" && $3 == "RISE" { split($5,b,"="); armed = (b[2] ~ "^(" want ")$"); next }
		armed && $2 == "DODGE" && $3 == "END" {
			for (i=1;i<=NF;i++) if ($i ~ /^dist=/) { split($i,a,"="); d=a[2]; sub(/uu$/,"",d); print d }
			armed=0
		}' "$SLICE"
}

damage_during_getup_exit() { # DAMAGED landing on the riser between its $1 rise and that exit ending
	awk -v want="$1" '
		$2 == "KNOCKDOWN" && $3 == "RISE" {
			split($5,b,"="); if (b[2] ~ "^(" want ")$") { armed=1; who=$4 } next
		}
		armed && $2 == "DAMAGED" && $3 == who { n++; next }
		armed && $2 == "DODGE" && $3 == "END" { armed=0 }
		END { print n+0 }' "$SLICE"
}

rise_to_block_up() { # seconds from each by=block rise to that pawn's next BLOCK up
	awk '
		$2 == "KNOCKDOWN" && $3 == "RISE" {
			split($5,b,"=")
			if (b[2] == "block") { t=$1; gsub(/[\[\]]/,"",t); start=t+0; who=$4; armed=1 }
			next
		}
		armed && $2 == "BLOCK" && $3 == "up" && $5 == who {
			t=$1; gsub(/[\[\]]/,"",t); printf "%.3f\n", (t+0)-start; armed=0
		}' "$SLICE"
}

getup_presses_while_exhausted() { # mode-$1 presses landing while that pawn carries State.Exhausted
	awk -v want="$1" '
		$2 == "EXHAUSTED"                  { ex[$3]=1; next }
		$2 == "EXHAUSTION" && $3 == "END"  { delete ex[$4]; next }
		$2 == "DEBUG" && $3 == "GETUP" { split($5,m,"="); if (m[2]==want && ($4 in ex)) n++ }
		END { print n+0 }' "$SLICE"
}

getup_rises_while_exhausted() { # rises by=$2 that followed a mode-$1 press made while exhausted
	awk -v want="$1" -v token="$2" '
		$2 == "EXHAUSTED"                  { ex[$3]=1; next }
		$2 == "EXHAUSTION" && $3 == "END"  { delete ex[$4]; next }
		$2 == "DEBUG" && $3 == "GETUP" {
			split($5,m,"="); armed=(m[2]==want && ($4 in ex)); who=$4; next
		}
		armed && $2 == "KNOCKDOWN" && $3 == "RISE" && $4 == who {
			split($5,b,"="); if (b[2]==token) n++
			armed=0
		}
		END { print n+0 }' "$SLICE"
}

configured_defender() { # the pawn this fixture drives, read off its own DEBUG GETUP lines
	grep -m1 "DEBUG GETUP  " "$SLICE" | awk '{print $4}'
}

run_s6_getup_exit() { # shared spine: $1 mode, $2 by= token(s), $3 lockout floor
	local presses rises
	presses=$(getup_mode_presses "$1")
	check "fixture pressed the $1 get-up" "$([ "$presses" -gt 0 ] && echo 0 || echo 1)" \
		"$presses presses"

	rises=$(getup_rises_by "$2")
	check "get-up fires as by=$2" "$([ "$rises" -gt 0 ] && echo 0 || echo 1)" "$rises rises"

	assert_all_in_band "rise inside the input window" \
		"kd_entry_to_rise_by $2" "$3" \
		"$(awk -v v="$BAND_KD_ENTRY_TO_RISE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" "s"
}

run_s6_dodge() {
	# The dodge get-up on a normal knockdown: a real roll, i-framed, and priced.
	local kipups damaged
	run_s6_getup_exit dodge dodge "$BAND_KD_LOCKOUT_NORMAL"

	# The conversion is type-gated, so a normal knockdown must never produce one. Asserted here
	# rather than only in s6-kipup: a kip-up appearing on normal is the same defect seen from the
	# other side, and only this fixture can see it.
	rises=$(getup_rises_by dodge)
	kipups=$(getup_rises_by kipup)
	if [ "$rises" -eq 0 ]; then
		check "no kip-up on a normal knockdown" 1 "no dodge get-ups -- the conversion had no chance to fire"
	else
		check "no kip-up on a normal knockdown" "$([ "$kipups" -eq 0 ] && echo 0 || echo 1)" \
			"$kipups rises by=kipup across $rises dodge get-ups"
	fi

	assert_all_equal "dodge get-up costs its 50" "getup_dodge_remaining dodge" \
		"$BAND_GETUP_DODGE_COST"


	# **The rise is the dodge.** Left on the shared KnockdownRiseSeconds, the difference is a
	# window with movement and facing both locked and the option's own protection already
	# expired -- and the i-frame assertion beside this one stops at DODGE END, which is exactly
	# where that window opens.
	assert_all_in_band "dodge get-up rise ends with the dodge" "kd_rise_span_by dodge" \
		"$(awk -v v="$BAND_KD_RISE_DODGE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_KD_RISE_DODGE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"
	assert_all_in_band "dodge get-up travels" "getup_dodge_travel dodge" \
		"$BAND_DODGE_MIN" "$BAND_DODGE_MAX" "cm"

	# The i-frames, observable: the attacker keeps swinging through the roll and none of it lands.
	damaged=$(damage_during_getup_exit dodge)
	if [ "$rises" -eq 0 ]; then
		check "i-frames hold across the dodge get-up" 1 "no dodge get-ups -- nothing was ever i-framed"
	else
		check "i-frames hold across the dodge get-up" "$([ "$damaged" -eq 0 ] && echo 0 || echo 1)" \
			"$damaged DAMAGED across $rises dodge get-ups"
	fi
}

run_s6_kipup() {
	# The same input on a hard knockdown: stationary, and the held direction ignored.
	local dodges
	run_s6_getup_exit dodge kipup "$BAND_KD_LOCKOUT_HARD"

	kipups=$(getup_rises_by kipup)
	dodges=$(getup_rises_by dodge)
	if [ "$kipups" -eq 0 ]; then
		check "hard never yields the directional dodge" 1 "no kip-ups -- the conversion never ran"
	else
		check "hard never yields the directional dodge" "$([ "$dodges" -eq 0 ] && echo 0 || echo 1)" \
			"$dodges rises by=dodge across $kipups kip-ups"
	fi

	# "Stationary" made falsifiable. The ceiling is slack for capsule settle, not a travel budget --
	# the authored displacement is zero and anything approaching the roll's 400 is the conversion
	# having silently not happened.
	assert_all_in_band "kip-up travels about zero" "getup_dodge_travel kipup" \
		0 "$BAND_KIPUP_TRAVEL_MAX" "cm"


	# **The rise is the dodge.** Left on the shared KnockdownRiseSeconds, the difference is a
	# window with movement and facing both locked and the option's own protection already
	# expired -- and the i-frame assertion beside this one stops at DODGE END, which is exactly
	# where that window opens.
	assert_all_in_band "kip-up rise ends with the kip-up" "kd_rise_span_by kipup" \
		"$(awk -v v="$BAND_KD_RISE_DODGE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_KD_RISE_DODGE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"
	assert_all_equal "kip-up costs its 50" "getup_dodge_remaining kipup" "$BAND_GETUP_DODGE_COST"
}

run_s6_block() {
	# The block get-up: the guard is live from activation rather than from the top of the rise.
	run_s6_getup_exit block block "$BAND_KD_LOCKOUT_NORMAL"

	assert_all_in_band "guard is up from activation" rise_to_block_up \
		0 "$BAND_BLOCK_GUARD_GAP" "s"
}

run_s6_hard_stand() {
	# Hard removes the free stand outright. The jump ability refuses by name, so the refusal and
	# the absent rise are two independent halves -- a silent no-op would pass one and fail the other.
	local presses refused stands
	presses=$(getup_mode_presses stand)
	check "fixture pressed the stand" "$([ "$presses" -gt 0 ] && echo 0 || echo 1)" "$presses presses"

	refused=$(grep -c "no stand from a hard knockdown" "$SLICE" || true)
	check "hard refuses the stand by name" "$([ "$refused" -gt 0 ] && echo 0 || echo 1)" \
		"$refused refusals"

	stands=$(getup_rises_by stand)
	if [ "$presses" -eq 0 ]; then
		check "no rise by=stand under hard" 1 "nothing pressed the stand -- the refusal had no chance to fire"
	else
		check "no rise by=stand under hard" "$([ "$stands" -eq 0 ] && echo 0 || echo 1)" \
			"$stands rises by=stand across $presses presses"
	fi

	# And the refusal costs the victim nothing but time: the auto-rise still arrives on the full
	# clock, which is what makes this a removed option rather than a broken one.
	assert_all_in_band "auto-rise still arrives on the full clock" "kd_entry_to_rise_by auto" \
		"$(awk -v v="$BAND_KD_ENTRY_TO_RISE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_KD_ENTRY_TO_RISE" -v t="$BAND_KD_SPAN_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"
}

run_s6_exhausted() { # run_s6_exhausted <mode> <by-token> <survives: yes|no>
	# The exhaustion exception, from both sides. A defensive option is refused naming the tag; the
	# get-up attack and the wait are not. The press always fires -- what differs is whether a rise
	# follows it, which is why every assertion here is anchored on the rise.
	local mode="$1" token="$2" survives="$3" who presses exhpresses rises
	presses=$(getup_mode_presses "$mode")
	check "fixture pressed the $mode get-up" "$([ "$presses" -gt 0 ] && echo 0 || echo 1)" \
		"$presses presses"

	who=$(configured_defender)
	check "defender identified from the trace" "$([ -n "$who" ] && echo 0 || echo 1)" "${who:-none}"

	# **The exhaustion exception's regen half is asserted by s6-exhaust-regen, not here.** Nothing
	# prints the stamina ledger while a character is down: EXHAUSTED and EXHAUSTION END bracket the
	# state from outside the down-span, and the regen pause and the guard-break stun both sit between
	# them, so from in here the endpoints cannot separate "regen ran while down" from "regen ran
	# after standing". That scenario measures the span itself, which suppression would lengthen.

	# The fixture drains the defender but cannot hold it exhausted indefinitely -- a successful
	# get-up refills, and the cycle can settle with the tag down at every press. So the presses
	# that carry the claim are the subset made while the tag was actually up, counted rather than
	# assumed: n=0 fails, because a run where nothing was pressed while exhausted proves nothing.
	exhpresses=$(getup_presses_while_exhausted "$mode")
	check "presses landed while exhausted" "$([ "$exhpresses" -gt 0 ] && echo 0 || echo 1)" \
		"$exhpresses of $presses presses made while State.Exhausted was up"

	rises=$(getup_rises_while_exhausted "$mode" "$token")
	if [ "$survives" = "yes" ]; then
		check "the $mode get-up survives exhaustion" \
			"$([ "$exhpresses" -gt 0 ] && [ "$rises" -eq "$exhpresses" ] && echo 0 || echo 1)" \
			"$rises of $exhpresses exhausted presses rose by=$token"
	else
		# **Asserted behaviourally, not by the REFUSED line.** That line dedups per reason for half
		# a second, so a defend mode pressing the same ability swallows the get-up press's own
		# refusal -- observed on HoldBlock plus BlockGetUp. A rise that does not happen is the same
		# fact and cannot be deduped.
		check "no $mode rise while exhausted" "$([ "$rises" -eq 0 ] && echo 0 || echo 1)" \
			"$rises rises by=$token across $exhpresses exhausted presses"
		# The wait survives by construction: refusing every option leaves the forced rise, and its
		# absence would mean the refusal had refused the victim rather than declined the option.
		check "the wait still rises them" \
			"$([ "$(getup_rises_by auto)" -gt 0 ] && echo 0 || echo 1)" \
			"$(getup_rises_by auto) rises by=auto"
	fi
}

# --- S6: the airborne knockdown ---------------------------------------------
# The ruling is that an airborne victim is floored mid-air with the Z axis left to gravity -- no
# ground snap. The structural fix is IgnoreZAccumulate on the shared root motion source: an Override
# source overrides *velocity*, gravity included, so any pinned Z hangs the body for the source's
# duration. What that predicts is a body which leaves the ground high and stands at ground level.

kd_airborne_pairs() { # "<entry z> <stand z>" for each knockdown that began airborne
	awk '
		$2 == "KNOCKDOWN" && $3 != "STAND" && $3 != "RISE" && $3 != "MONTAGE" {
			z=""; air=""
			for (i=1;i<=NF;i++) {
				if ($i ~ /^z=/)        { split($i,a,"="); z=a[2] }
				if ($i ~ /^airborne=/) { split($i,b,"="); air=b[2] }
			}
			if (air == "1") down[$3]=z
			next
		}
		$2 == "KNOCKDOWN" && $3 == "STAND" && ($4 in down) {
			for (i=1;i<=NF;i++) if ($i ~ /^z=/) { split($i,a,"="); print down[$4] " " a[2] }
			delete down[$4]
		}' "$SLICE"
}

kd_ground_stand_z() { # stand z of every knockdown that began grounded -- the floor, measured in-run
	awk '
		$2 == "KNOCKDOWN" && $3 != "STAND" && $3 != "RISE" && $3 != "MONTAGE" {
			air=""
			for (i=1;i<=NF;i++) if ($i ~ /^airborne=/) { split($i,b,"="); air=b[2] }
			if (air == "0") down[$3]=1
			next
		}
		$2 == "KNOCKDOWN" && $3 == "STAND" && ($4 in down) {
			for (i=1;i<=NF;i++) if ($i ~ /^z=/) { split($i,a,"="); print a[2] }
			delete down[$4]
		}' "$SLICE"
}

run_s6_airborne() {
	local pairs n floor hung high worst
	pairs=$(kd_airborne_pairs)
	n=$(printf '%s\n' "$pairs" | grep -c . || true)

	check "airborne knockdowns observed" "$([ "$n" -gt 0 ] && echo 0 || echo 1)" \
		"$n knockdowns entered with airborne=1"
	[ "$n" -gt 0 ] || return

	# **The floor is the *lowest* stand, not the highest.** Grounded stands cluster at ground level,
	# but the test level has raised geometry and a stand occasionally happens on it -- taking the
	# maximum picks that outlier and shifts the reference by 40cm.
	floor=$(kd_ground_stand_z | sort -n | head -1)
	if [ -z "$floor" ]; then
		check "floor reference available" 1 "no grounded knockdown in this run to measure it from"
		return
	fi
	check "floor reference available" 0 "z=$floor, the lowest of this run's grounded stands"

	# **Being airborne by the flag is not being airborne at height.** A body 2cm off the deck
	# exercises the code path and proves nothing about a pinned Z, which needs room to show. This
	# runs first because the hang test below is only meaningful on samples that had height to lose.
	high=$(printf '%s\n' "$pairs" | awk -v f="$floor" -v m="$BAND_AIRBORNE_MIN_HEIGHT" \
		'NF && ($1 - f >= m) { n++ } END { print n+0 }')
	worst=$(printf '%s\n' "$pairs" | awk -v f="$floor" 'NF { d=$1-f; if (d>m) m=d } END { printf "%.1f", m+0 }')
	check "at least one floored at height" "$([ "$high" -gt 0 ] && echo 0 || echo 1)" \
		"$high of $n cleared ${BAND_AIRBORNE_MIN_HEIGHT}cm; highest was ${worst}cm above the floor"

	# **The rule, as the trap states it: equal heights across a carry mean the body hung.** Compared
	# against each victim's *own* stand rather than a global floor, which is both what the rule says
	# and immune to where in the level the body happened to come down. Scoped to the samples that
	# cleared the height bar: a body floored 2cm up has nothing to fall, so it can neither hang nor
	# be seen not to.
	hung=$(printf '%s\n' "$pairs" | awk -v f="$floor" -v m="$BAND_AIRBORNE_MIN_HEIGHT" \
		-v t="$BAND_AIRBORNE_STAND_TOL" \
		'NF && ($1 - f >= m) && ($1 - $2 < m - t) { n++ } END { print n+0 }')
	if [ "$high" -eq 0 ]; then
		check "no airborne body hung" 1 "no sample cleared ${BAND_AIRBORNE_MIN_HEIGHT}cm -- nothing had height to lose"
	else
		check "no airborne body hung" "$([ "$hung" -eq 0 ] && echo 0 || echo 1)" \
			"$hung of $high high samples failed to fall back to their own stand"
	fi
}

# --- S6: the exhaustion exception -------------------------------------------
# Knockdown suppresses regen -- unless you are already exhausted. That exception is what stops
# repeated knockdowns locking a player out forever, regen being exhaustion's only exit.
#
# **Asserted as time that fails to appear, which is why it needs no new trace line.** Nothing prints
# the stamina ledger inside a down-span, and it does not have to: if a knockdown suppressed an
# exhausted player's regen, every EXHAUSTED -> EXHAUSTION END span containing one would run longer
# by that knockdown's duration. The endpoints both print, so the span is measurable and the
# prediction is arithmetic.

exhaust_spans_with_knockdown() { # "<span> <predicted>" per exhaustion that contained a knockdown
	awk -v pause="$BAND_EXHAUST_PAUSE" -v regen="$BAND_EXHAUST_REGEN_SECONDS" \
	    -v stun="$BAND_EXHAUST_BREAK_STUN" '
		$2 == "EXHAUSTED" { t=$1; gsub(/[\[\]]/,"",t); start=t+0; open=1; brk=0; kd=0; next }
		open && $2 == "GUARD" && $3 == "BREAK" {
			t=$1; gsub(/[\[\]]/,"",t); if ((t+0)-start < 0.05) brk=1; next
		}
		open && $2 == "KNOCKDOWN" && $3 != "RISE" && $3 != "STAND" && $3 != "MONTAGE" { kd++; next }
		open && $2 == "EXHAUSTION" && $3 == "END" {
			t=$1; gsub(/[\[\]]/,"",t)
			if (kd > 0) printf "%.3f %.3f\n", (t+0)-start, pause + regen + (brk ? stun : 0)
			open=0
		}' "$SLICE"
}

run_s6_exhaust_regen() {
	local rows n bad worst
	rows=$(exhaust_spans_with_knockdown)
	n=$(printf '%s\n' "$rows" | grep -c . || true)

	# n=0 fails: a run where nobody was floored while exhausted says nothing about the exception.
	check "exhaustions containing a knockdown" "$([ "$n" -gt 0 ] && echo 0 || echo 1)" \
		"$n spans with a knockdown inside"
	[ "$n" -gt 0 ] || return

	# **The whole assertion.** Predicted is the recovery an exhausted player owes with the knockdown
	# contributing nothing. Suppression would add the down-span, which is 2.5s -- an order above the
	# tolerance, so this cannot pass by luck.
	bad=$(printf '%s\n' "$rows" | awk -v t="$BAND_EXHAUST_SPAN_TOL" \
		'NF && ($1-$2 > t || $2-$1 > t) { n++ } END { print n+0 }')
	worst=$(printf '%s\n' "$rows" | awk 'NF { d=$1-$2; if (d<0) d=-d; if (d>m) m=d } END { printf "%.3f", m+0 }')
	check "a knockdown costs an exhausted player no recovery" \
		"$([ "$bad" -eq 0 ] && echo 0 || echo 1)" \
		"$bad of $n spans off prediction by more than ${BAND_EXHAUST_SPAN_TOL}s; worst ${worst}s"
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

# --- s7: death, its impulse, and its supersession of knockdown ----------------
# Every extractor here must exclude DEATH SETTLE, which shares the DEATH prefix and would
# otherwise be counted as a death by an actor named "SETTLE".


death_revive_spans() { # seconds from each DEATH to that actor's REVIVE
	awk '
		/^\[[0-9.]+\] DEATH / && $3 != "SETTLE" { t=$1; gsub(/[\[\]]/,"",t); dead[$3]=t+0; next }
		/^\[[0-9.]+\] REVIVE / { t=$1; gsub(/[\[\]]/,"",t)
			if ($3 in dead) { printf "%.3f\n", (t+0)-dead[$3]; delete dead[$3] } }
	' "$SLICE"
}

death_health() { # the health reading on the blow that killed, one per death
	awk '
		/^\[[0-9.]+\] DEATH / && $3 != "SETTLE" { t=$1; gsub(/[\[\]]/,"",t); dt[$3]=t+0; next }
		/^\[[0-9.]+\] DAMAGED / { t=$1; gsub(/[\[\]]/,"",t)
			if (($3 in dt) && (t+0)==dt[$3]) {
				for (i=1;i<=NF;i++) if ($i ~ /^health=/) { h=$i; sub(/^health=/,"",h); print h+0 }
				delete dt[$3] } }
	' "$SLICE"
}

death_settles() { # the horizontal distance each corpse ended from its capsule
	awk '/^\[[0-9.]+\] DEATH SETTLE/ {
		for (i=1;i<=NF;i++) if ($i ~ /^drift=/) { d=$i; sub(/^drift=/,"",d); print d+0 } }' "$SLICE"
}

damage_while_dead() { # DAMAGED landing strictly after a DEATH and before that actor's REVIVE
	awk '
		/^\[[0-9.]+\] DEATH / && $3 != "SETTLE" { t=$1; gsub(/[\[\]]/,"",t); dead[$3]=t+0; next }
		/^\[[0-9.]+\] REVIVE /  { delete dead[$3]; next }
		/^\[[0-9.]+\] DAMAGED / { t=$1; gsub(/[\[\]]/,"",t)
			if (($3 in dead) && (t+0) > dead[$3]) n++ }
		END { print n+0 }
	' "$SLICE"
}

deaths_that_also_floored() { # deaths whose own contact still produced a KNOCKDOWN
	awk '
		/^\[[0-9.]+\] DEATH / && $3 != "SETTLE" { t=$1; gsub(/[\[\]]/,"",t); dt[$3]=t+0; next }
		/^\[[0-9.]+\] REVIVE /     { delete dt[$3]; next }
		/^\[[0-9.]+\] KNOCKDOWN  / { t=$1; gsub(/[\[\]]/,"",t)
			if (($3 in dt) && (t+0)==dt[$3]) n++ }
		END { print n+0 }
	' "$SLICE"
}

count_deaths() { awk '/^\[[0-9.]+\] DEATH / && $3 != "SETTLE" { n++ } END { print n+0 }' "$SLICE"; }
count_tag() { grep -c "^\[[0-9.]*\] $1 " "$SLICE" 2>/dev/null || true; }

run_s7_death() {
	local deaths revives orphans dmg
	deaths=$(count_deaths)
	if [ "$deaths" -eq 0 ]; then
		check "DEATH fires" 1 "no DEATH lines -- nothing died, so every assertion below is vacuous"
		return
	fi
	check "DEATH fires" 0 "$deaths deaths"

	# Health is asserted rather than assumed: a death on a nonzero bar would mean the
	# threshold moved, which nothing else here would notice.
	assert_all_equal "death lands at exactly zero health" death_health 0.0

	revives=$(count_tag REVIVE)
	orphans=$((deaths - revives))
	check "every death revives" "$([ "$orphans" -le 0 ] && echo 0 || echo 1)" \
		"$deaths deaths, $revives revives"

	assert_all_in_band "death -> revive" death_revive_spans \
		"$(awk -v v="$BAND_REVIVE_DELAY" -v t="$BAND_REVIVE_TOL" 'BEGIN{printf "%.3f", v-t}')" \
		"$(awk -v v="$BAND_REVIVE_DELAY" -v t="$BAND_REVIVE_TOL" 'BEGIN{printf "%.3f", v+t}')" "s"

	# **The impulse, made assertable.** Its magnitude would not catch the failure that matters:
	# bVelChange does not change the number, it changes what the number means. The settle
	# distance is the observable, read at teardown while the resting place still exists.
	assert_all_in_band "death impulse carries the corpse" death_settles \
		"$BAND_DEATH_SETTLE_LO" "$BAND_DEATH_SETTLE_HI" "cm"

	# A corpse must not be hittable. The killing blow shares the death's timestamp and is
	# excluded by the strict comparison in the extractor.
	dmg=$(damage_while_dead)
	check "zero DAMAGED while dead" "$([ "$dmg" -eq 0 ] && echo 0 || echo 1)" \
		"$dmg DAMAGED across $deaths deaths"
}

run_s7_death_grade() {
	local deaths floored kdn
	deaths=$(count_deaths)
	kdn=$(count_tag KNOCKDOWN)

	if [ "$deaths" -eq 0 ]; then
		check "DEATH fires" 1 "no DEATH lines"
		return
	fi
	# **The fixture is what makes this rigorous, not the assertion.** On the heavy fixture every
	# swing is graded, so any death in the run is necessarily a graded kill. On a light string the
	# lethal blow is hit 7 and enders are hits 3 and 6, so it lands on a swing that would not have
	# floored anyway and the check below passes without exercising anything.
	# The knockdown count is the second half of that guard: it proves grading is live in this run.
	if [ "$kdn" -eq 0 ]; then
		check "graded swings floor when they do not kill" 1 \
			"no KNOCKDOWN lines at all -- the fixture is not throwing a graded swing"
		return
	fi
	check "graded swings floor when they do not kill" 0 "$kdn knockdowns beside $deaths deaths"

	floored=$(deaths_that_also_floored)
	check "death suppresses the knockdown on its own contact" \
		"$([ "$floored" -eq 0 ] && echo 0 || echo 1)" \
		"$floored of $deaths deaths still produced a KNOCKDOWN"
}

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
	s5-parry)        run_s5_parry ;;
	s5-parry-reward) run_s5_parry_reward ;;
	s5-parry-whiff) run_s5_parry_whiff ;;
	s5-cancel)      run_s5_cancel ;;
	s5-waiver)      run_s5_waiver ;;
	s6-knockdown)   run_s6_knockdown ;;
	s6-hard)        run_s6_hard ;;
	s6-stand)       run_s6_stand ;;
	s6-getup)       run_s6_getup ;;
	s6-dodge)       run_s6_dodge ;;
	s6-kipup)       run_s6_kipup ;;
	s6-block)       run_s6_block ;;
	s6-hard-stand)  run_s6_hard_stand ;;
	s6-exhausted)        run_s6_exhausted dodge  dodge  no ;;
	s6-exhausted-kipup)  run_s6_exhausted dodge  kipup  no ;;
	s6-exhausted-block)  run_s6_exhausted block  block  no ;;
	s6-exhausted-attack) run_s6_exhausted attack attack yes ;;
	s6-airborne)    run_s6_airborne ;;
	s6-exhaust-regen) run_s6_exhaust_regen ;;
	s7-death)       run_s7_death ;;
	s7-death-grade) run_s7_death_grade ;;
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
