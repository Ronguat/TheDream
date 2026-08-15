# Verification infrastructure — implementation contract

**Read this when implementing any of the three packages, and not otherwise.** Approved 2026-08-15
after a planning pass with live measurements; sequence **V1 → V3 → V2**, with V2 free to
interleave (it has no coupling to the other two). **This file is work-in-flight, not standing
knowledge — delete it when all three packages have shipped.** By then its durable consequences
live in `Docs/Debug-Instruments.md` and `Docs/Working-In-Unreal.md`, and keeping a shipped plan
around would be a second copy nobody reviews.

The goal, in one line: offense has an unattended driver and defense does not, so every Block,
Dodge and stamina check is human-gated, and the client path has never once run. These three
packages remove the gates without touching combat design. **None of this is a combat-design
pass** — any behaviour question that surfaces gets filed, not fixed inline.

## Evidence this plan stands on (measured 2026-08-15)

Re-verify these rows before building if the project has moved since.

- `L_CombatTest` has exactly **one** PlayerStart (`PlayerStart_0`).
- `BP_TrainingDummy`'s placed instance serializes `DefaultAbilities = [GA_Attack]`; the CDO reads
  the same. `DefaultAbilities` is `EditDefaultsOnly`, so the instance cannot be written and CDO
  additions may not propagate to it — **the defender must be a fresh placement made after the CDO
  gains the defensive abilities.** The existing attacker keeping only `GA_Attack` is desirable.
- `GA_Block` CDO: owns `Ability.Defend.Block`, `State.Blocking`, `State.StaminaRegenPaused`;
  blocked by `State.Attacking.Committed` / `State.Blocking` / `State.Dodging` / `State.Exhausted`;
  `bResumeWhileInputHeld = true`; `InputTag.Block`.
- `GA_Dodge` CDO: `InputTag.Dodge`, `bBlockedWhileAirborne = true`, `DodgeSeconds` 0.4,
  `DodgeTargetDistanceCm` 405.
- The debug attacker's press path (`OnAbilityInputPressed`) sets `Spec.InputPressed` and the
  resume path reads it — **a debug press that never releases gets resume semantics for free**
  (read from source the same day). This is what makes HoldBlock a one-press design.
- Attacker fixture today: `bDebugAutoAttack` on, hold 0.1 (lights only; 0.3 buys a heavy, 0.8 a
  charged), interval 3.0, facing `WhileAttacking`, reset delay 0.35.

## Traps that bind execution

Grep the known-traps section at slice start as usual; these are the ones already known to apply.

- `bSimulate: true` stalls the dummy's timers — **normal PIE only**.
- CDO writes are not live until an editor restart, and **saving a level before that restart bakes
  stale values into placed actors**. Order is fixed: batch CDO writes → restart → place → save →
  read the **runtime** instance back (staleness is per property).
- Array writes through the toolset: **empty the container, then write it whole**, two calls.
- A periodic dodger against the 3.0 s attack cycle aliases — the dodge interval must not divide
  or multiply it. Sweeping phase is the feature, not a bug.
- Travel measurements need nothing touching the mover (capsules contact at 84 cm centre-to-centre)
  and must never be taken against an assumed position.
- An exhausted holder's `REFUSED` stream is **expected output** (the resume retries per tick,
  deduped to ~2/s per ability) — pass bands must whitelist it, not fail on it.
- The attacker's rule generalises: any defender tap/reset timing must fit inside its own interval.
- Adding pawns to the level changes nearest-target selection and every prior travel baseline —
  **file the fixture change in `Docs/Debug-Instruments.md`**, as the 2026-08-14 facing-mode
  change was filed. Measurements do not span it.

## V1 — Defense-capable dummy — **SHIPPED 2026-08-15**

Built and play-verified as described below. Both modes run unattended; the fixture, its placement
constraints and the two new trace lines are documented in `Docs/Debug-Instruments.md`, which is now
the authority for all of it.

**The parity question it raised was settled by the user, and it is the durable result.** The dummy
had drifted from the player on three combat values, and the call was that **a fixture mirrors the
conditions the systems exist to test — breaking parity is the design decision, not keeping it.**
`BlockInitialStaminaCost` 0 → 10, `StaminaRegenPauseSeconds` 1.0 → 0.5, `InputBufferSeconds`
0.10 → 0.20. Two of this plan's own assertions were wrong before that and correct after: S2's cost
line, and S3's "action end + 0.5 s", which was the player's number all along.

The defender's placement is the other finding worth keeping: a stationary dodge resolves backward,
so on the +Y side it backs into the ramp and travels 107 cm rather than 405.

## V3 — Autonomous PIE regression loop — **SHIPPED 2026-08-15**

`Tools/RegressionCheck/regression-check.sh` ships with all seven scenarios of the matrix below,
33 assertions, validated against seven real PIE sessions. Bands sit in one config block; the
scenario matrix and how-to are in `Docs/Debug-Instruments.md`, and `Working-In-Unreal.md`'s
verification list now points at it.

**The instrument was proved before its passes were trusted, twice.** `--self-test` asserts a
known-good band passes and a deliberately wrong one fails; and asserting `s1-light`'s bands against
a real `s1-heavy` session fails all four checks with the true numbers printed. Both are documented
so the next person repeats them rather than trusting a green table.

**One assertion changed shape from the plan.** S3's travel check needs the `|right| ≤ 1.0` lateral
filter — 5 of 22 samples in the reference run were the attacker colliding with a displaced dodger,
reading ~297 cm. Widening the distance band to admit them would have been fitting the band to
contamination.

### V1 as planned, kept for the reasoning

**Design: mirror the auto-attacker exactly**, in the same `Combat|Debug` region of
`ATDCombatCharacter`, driving `OnAbilityInputPressed/Released` like it does.

- `ETDDebugDefendMode { Off, HoldBlock, PeriodicDodge }` — Parry gets a mode when it exists.
- `DebugAutoDefendMode` (EditAnywhere, default Off), `DebugDefendBlockInputTag`,
  `DebugDefendDodgeInputTag`, `DebugDodgeIntervalSeconds` (default co-prime with 3.0 — e.g. 1.9).
- **HoldBlock**: one press of the block tag after seeding, never released. Resume semantics then
  re-raise the guard through every break and exhaustion, indefinitely, with no further code.
- **PeriodicDodge**: press + ~50 ms release on the interval timer.
- **Two instrument gaps close here because defense verification needs them**: `EXHAUSTED` /
  `EXHAUSTION END` trace lines in Enter/ExitExhaustion (the documented "nothing logs exhaustion"
  gap), and `remaining=` on the `DODGE` line (parity with `BLOCK cost`).

**Content and fixture:** add `GA_Block` + `GA_Dodge` to the dummy CDO → restart → fresh-place one
defender paired with the existing attacker, **away from the player's axis** so nearest-pawn
targeting binds the pair to each other → save → verify instance and live PIE values.

**What one unattended session yields:** a repeating ~25–30 s cycle — raise (cost 10) → drain to 0
in ~9 s → `BLOCKED` at 5/hit → hit at 0 → `GUARD BREAK` → 1.0 s stun → suppressed then exhausted
regen → resume at Max → repeat. Fixture variants, zero code: defender yawed 180° exercises the
`IsGuardFacing` rejection; attacker hold 0.3 / 0.8 makes the heavy's 50 and the charged's
always-breaks arithmetic observable.

**Doc consequences, same commit:** `Docs/Working-In-Unreal.md`'s "nothing in the build can drain
stamina without a human" becomes false — update it; `Docs/Debug-Instruments.md` gains the defender
fixture rows, a note that `INPUT` lines include debug presses, the new trace lines, and the
fixture-changed marker.

**Estimate:** ~100–140 C++ lines plus two trace lines, one rebuild cycle, content pass, docs.

## V3 — Autonomous PIE regression loop (depends on V1)

**Form: no new framework.** Orchestration is agent-side, the pattern proven 2026-08-15: StartPIE →
poll `Saved/Logs/TheDream.log` on a condition → inspector snapshots → StopPIE → evaluate. UE's
Automation framework was considered and declined (heavier surface; the in-editor script
environment cannot sleep on the game thread). Headless `-game` runs are a future note only.

**Ships:** `Tools/RegressionCheck/regression-check.sh` — bash + awk (no Python on this machine) —
slicing the log from the last PIE start, asserting bands, printing the pass/fail table
`Working-In-Unreal.md`'s verification rule asks for, non-zero exit on failure. Bands live in a
config block at the top of the script so a retune is a one-line update. Plus the scenario matrix
and a short how-to in `Docs/Debug-Instruments.md`, and a one-line pointer from
`Working-In-Unreal.md`'s "Verifying combat changes" (the automatable half of that list).

**Scenario matrix v1** (attacker knobs are EditAnywhere instance writes pre-PIE; read the runtime
instance back each run):

- **S1 tiers-vs-air** — hold 0.1 / 0.3 / 0.8: `RELEASE BEGIN` at 200 / 500 / 750 ±30 ms; elapsed
  0.750 / 1.150 / 1.500 +10–35 ms (the measured frame-quantisation band); escalation and coil
  counts exact.
- **S2 attacker-vs-blocker** — exact stamina-damage steps per tier (5 / 50 / 100); `remaining=`
  ledger monotone by exact steps; `GUARD BREAK` fires exactly when remaining reaches 0 and only
  then; `BLOCKSTUN until − now` equals the tier's authored `BlockstunSeconds` ±1 frame; break
  stun spans 1.0 s ±1 frame; cost lines equal guard raises; **the charged never blockstuns and
  always breaks** — the filed trap becomes a standing assertion.
- **S3 attacker-vs-dodger** — paired `DODGE`/`DODGE END`; travel ≈405 with clear placement;
  exact-50 spends via `remaining=`; `EXHAUSTED`/`EXHAUSTION END` pairing; regen-resume timing
  (action end + 0.5 s) inferred from the remaining sequence.

**Prove the instrument:** the first run includes one deliberately wrong band — the checker must be
seen to fail before its passes are trusted.

**Estimate:** script + docs, no rebuild; the first full run doubles as V1's deep verification.

## V2 — Two-player PIE recon — **SHIPPED 2026-08-15**

Both sessions run (one process, then a separate client process), findings filed as a dated entry and
as updated rows in `Docs/Combat-Decisions.md`'s multiplayer section, the recipe recorded in
`Docs/Debug-Instruments.md`, play settings restored to single player. **Nothing was fixed**, per
scope. **V2 owes the regression loop no new scenarios** under the 2026-08-15 coupling rule — it
shipped no combat capability — but it changed the level, so `s2-light` was re-run afterwards and
passes.

**Two checklist items remain, both blocked only on input**: client attack → server damage in exact
multiples, and `Net PktLag 100`. Driving them needs a human alternating windows, which this plan
already listed as a non-goal.

**With all three packages shipped, this file has discharged its purpose and can be deleted** — its
header says so. Its durable content now lives in `Docs/Debug-Instruments.md` (fixtures, scenario
matrix, two-player recipe), `Docs/Working-In-Unreal.md` (the verification pointer) and
`Docs/Combat-Decisions.md` (the V1/V2 dated entries and the multiplayer traps).

### V2 as planned, kept for the reasoning

**Ships:** a second PlayerStart; play settings to 2 players + Listen Server (cleanest: edit the
editor's per-project user settings ini while the editor is closed; ConfigSettingsToolset or two
human clicks are fallbacks); one recon session against the checklist below; findings filed as
measured rows in the decision log's multiplayer section; the recipe recorded compactly where the
budget allows. **No gameplay code, and nothing found gets fixed inline.**

**First-ever-possible checks, all observational:** `OnRep_PlayerState` fires and the client
resolves onto its PlayerState ASC (the seeding race manifests as a client that cannot act);
client attack → server damage in exact multiples; the four replicated bools watched from the
*other* window (die in one, see both); blockstun/guard-break tags on a remote pawn; the
LocalPredicted warning inventory on the client (first real data); whether the toolset can see the
second PIE world at all; optionally `Net PktLag 100` for a first taste of the 200 ms question.
Run once under one process and once with a separate client process; note the deltas.

**Non-goals:** latency realism, fixing findings, human-vs-human input (one keyboard alternates
windows — real two-human play needs a gamepad or second machine and is the step after this one).

## Still human after all three

Every feel verdict; the jump/airborne family; parry until it exists; montage and visual reads
beyond notify firing; true network latency; and two-human play — which is what the verified-good
trigger ultimately needs, and which V2 is the first step toward.
