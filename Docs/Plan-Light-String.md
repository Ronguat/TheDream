# Plan — Light String (work-in-flight)

**This is the working plan for the Light String slice, agreed at the 2026-08-16 plan session.
Delete this file when the slice ships** — its durable content lives in `Docs/Combat-Decisions.md`
(the 2026-08-16 plan-session entry), in header comments, and in `CLAUDE.md` once the rules are
build-true. The reasoning behind every design choice here is in that dated entry; this file is the
*how*, and it is disposable by design.

**One question is still open at greenlight and is marked § Open below.** Everything else is agreed.

---

## Scope

**In:** the 2–4 hit light string, hold-to-convert preserved per hit; **hitstun** (the guarantee's
mechanism, blockstun-patterned); chain-on-whiff gating; per-hit authored values; the chain-press
buffer extension; `Jump()`'s hitstun check; trace additions; regression scenarios (the
loop-coverage choice is **scenarios**, not a trap); and the **authored clip-trial step** — the
final roster and string length are chosen by the designer in PIE, not by this plan.

**Out, with owners:** the knockdown terminator (Knockdown & Oki — the last hit ships dealing
damage with long recovery but not knocking down); hit-reaction animation (Death-full); movement
lock during hitstun (Knockdown & Oki, deferred in parallel with the guard break's, same shape);
heavy→heavy chains (spec says "some heavies"; the runway is silent for this weapon, so none —
chain eligibility is authored per branch, so enabling one later is data).

## Decided at the plan session — see the dated entry for reasoning

1. **DKO model stands, as a bet rather than a ruling.** The build keeps New World reachable as a
   retune: chain eligibility per branch, hitstun per hit, the heavy→light ban expressed as data.
2. **Hitstun ships in this slice**, mechanics only. It is what makes "any hit guarantees the rest"
   true against a dodge; cadence alone only beats walking.
3. **Strings chain regardless of contact** — whiff included. The cost to the light's whiff-punish
   window (~0.75 s → ~0.55 s effective) is named and accepted; Interplay judges it.
4. **The clip roster is an authored trial.** The build scaffolds on all-`Attack4` 4-hit
   (S1 → S2 → S3 → S4, all `_Complete_IP`, all already migrated); the trial step swaps and trims.

## § Open at greenlight — hitstun's interrupt semantics

Does hitstun **cancel the victim's active abilities** on entry (recommended: yes — a hit through
your swing beats your swing, which is the DKO trading model and what makes the light an interrupt
to a coiling heavy), or refuse activation only (armor-like: a mid-swing victim finishes their
swing, and simultaneous lights trade to completion)? The recommendation is *cancel all, including
committed attacks* — commitment governs what the victim may cancel voluntarily, not what being hit
does to them. Blockstun is untouched either way: it cancels nothing and never did.

---

## Mechanism

### Per-swing data, and a deliberate asymmetry

New struct `FTDStringSwing`: `{ Montage, ReleaseStartSeconds, CoilEndSeconds, Damage,
StaminaDamage, BlockstunSeconds, HitstunSeconds, RecoverySeconds }`, in a new
`StringSwings` array on `UTDChargedAttackAbility` covering **hits 2..N only**.

**Hit 1 stays exactly the current authored surface** — `AttackMontage`, the ability-level
`ReleaseStartSeconds`/`CoilEndSeconds`, and `Branches[0]`'s values, untouched. The asymmetry is
deliberate and load-bearing: moving those UPROPERTYs would orphan every play-verified CDO override
(the `ATDCombatCharacter`-split warning, same mechanism), and leaving them intact means
`s1-*`/`s2-*`/`s3` cannot be perturbed by construction. The header documents the asymmetry.

Accessors `GetSwingMontage(i)` / `GetSwingReleaseStart(i)` / `GetSwingCoilEnd(i)` resolve index 0
to the legacy fields. The rate derivations (`ComputeWindupPlayRate`, coil, recovery) and
`IsWindowForThisAttack` switch to these — they already take the montage through one field, so this
is a parameterisation, not a rewrite.

**Escalation mid-string is unchanged ladder machinery**: hit k held past 150 ms coils *hit k's
clip* and commits `Branches[1]`/`[2]`'s wall-clock timings and values against that clip's authored
`ReleaseStartSeconds`. The shared-windup tell survives per swing by construction. A heavy or
charged commit **ends the string** (window killed at commit) — heavy never chains into light, and
per the runway's silence this weapon's heavies do not chain at all.

Hitboxes, lunges and aim wedges stay the shared `Branches` values across all swings this slice —
consistent with the uniform-wedge state pending bespoke clips (the reach/travel/spacing trap).

`FTDAttackBranch` gains `HitstunSeconds` (light 0.40 / heavy 0.50 / charged 0.60 proposed) and
`bChainsIntoString` (true on branch 0 only) — the DKO↔New World flip lives in that flag.

### String state, on the character

Following the blockstun pattern: `StringIndex` (uint8, `Replicated` — the network rule; server
authoritative under Netcode, plain mirror today) and `StringWindowEndsAt` (float, server-side like
`BlockstunEndsAt`, not replicated).

- **Advance:** on attack activation, `if (Now <= StringWindowEndsAt && StringIndex + 1 <= LastSwing)
  ++StringIndex; else StringIndex = 0;` — the ability reads it to select swing data.
- **Open:** when a *chain-eligible* (branch 0, has a successor) attack ends,
  `StringWindowEndsAt = Now + StringLinkWindowSeconds` (0.40 first probe) — so a press *after* the
  string's recovery still links, fighting-game style.
- **Kill (reset to hit 1):** heavy/charged commit; the attack being cancelled (defensive cancel of
  a windup, death, and — pending § Open — hitstun); window lapse.

### Chain-out

Chain opens at **recovery start** (`RELEASE OFF`) plus `ChainOpenAfterRecoverySeconds` (authored,
default 0 — the cadence knob beyond hitstun). From then: the character's buffer tick, holding an
attack press against an active attack whose `IsChainOpen()` is true, **ends that attack** and lets
the existing first-legal-frame retry fire the next swing — one to two frames of latency, and no
new input path. The ability's early end runs the ordinary `EndAbility` funnel, so facing, tags,
homing and the lunge all clean up exactly as today.

Cadence arithmetic at defaults: contacts at **0.20 / 0.55 / 0.90 s** — 350 ms gaps, which
hitstun's 0.40 covers with the same 50 ms margin blockstun carries.

### The buffer extension (flagged: it touches a felt system)

**An attack press stored while the presser's own attack is active does not expire until that
attack's link window closes.** Today a chain tap inside hit N's first 150 ms dies at +200 ms,
before the 350 ms chain boundary — mash cadence drops exactly the input the string invites
(`InputBufferSeconds`' watch names this slice as its trigger). Bounded ≤ ~1.2 s worst case, so the
4-second exhaustion argument that killed global widening does not apply. Last-press-wins, the
release edge, and every other buffer rule unchanged — held chain presses therefore escalate to
heavy through the buffer for free.

### Hitstun

Mirror blockstun exactly: `bInHitstun` (`ReplicatedUsing=OnRep_Hitstun`) + `HitstunEndsAt`
(server) + `EnterHitstun(Duration)` extending by max + `Apply/ClearHitstunState` applying
`State.Hitstun` locally + tick-driven expiry. Entered from `HandleTraceHit`'s **unblocked** branch
(authority), beside the damage effect — blocked hits take blockstun, dodged hits take nothing, so
the three stuns stay exclusive by construction.

`State.Hitstun` joins the shared base's blocked tags (the `State.GuardBroken` mechanism) —
refusing **all** abilities, defense included; that refusal *is* the guarantee. `Jump()` gets the
fifth hand-restated check (and the comment counting them gains one more argument for
jump-as-ability). Movement stays free this slice: the chase lunge covers a walking victim
(175 cm of walk against a 300 cm chase ceiling), and the full lockout ships with Knockdown & Oki
beside the guard break's.

---

## Numbers — all proposed, all destined for the felt table as unfelt

| Value | Proposal | Why this shape |
|---|---|---|
| Swings 2–3: Damage / StaminaDamage | 15 / 5 each | Flat, matching hit 1; the trial may taper |
| Swings 2–3: RecoverySeconds | 0.40 | Matches hit 1 |
| Swing 4 (ender): RecoverySeconds | 0.60 | The spec's "heavy endlag" on the last hit |
| Swing 1 BlockstunSeconds | 0.40 (unchanged) | = recovery, 50 ms safe — spec: first hit safe on block |
| Swings 2–3 BlockstunSeconds | 0.25 | recovery − 0.15: a 100 ms punish head start — spec: unsafe |
| Swing 4 BlockstunSeconds | 0.30 | Against 0.60 recovery: very punishable ender |
| HitstunSeconds L/H/C | 0.40 / 0.50 / 0.60 | Covers the 350 ms gap +50 ms; ladder mirrors blockstun |
| `StringLinkWindowSeconds` | 0.40 | Deliberately generous first probe, `MinimumBlockSeconds`-style |
| `ChainOpenAfterRecoverySeconds` | 0.0 | Chain at recovery start; the knob exists for the trial |

"Unsafe on block" bites only when the string *ends* on a blocked hit — chain-regardless means the
attacker may keep going, which is the guess game DKO wants; recorded in the entry.

## Assets, and the authoring loop the trial runs on

Three new montages — `AM_Attack_S2/S3/S4` from `Attack4_Stage2/3/4_Complete_IP` (1.933 / 1.000 /
2.300 s; `_IP` is mandatory — root motion suppresses the lunge). Per montage:

1. `AssetTools.duplicate` from `AM_Attack`, write `slotAnimTracks` whole to repoint the segment
   (live write, ~90% scriptable).
2. **Human:** open it, drag the inherited Release Window onto the new clip's impact frames,
   save (recomputes `sequenceLength`). Notify placement is the one unscriptable step.
3. One PIE swing: the `MONTAGE` trace prints the notify's real `trigger=`; author that montage's
   `ReleaseStartSeconds` from it and `CoilEndSeconds` just below it. The drift warning guards it
   thereafter.

**Swapping a trial candidate is the same loop** — repoint the segment, re-place the notify, re-read
two numbers. Budget one editor sitting for the designer's roster trial; string length is the array
size, so 2-, 3- and 4-hit variants are details-panel edits.

## Coverage — the scenarios branch, chosen at plan time

Fixture: `DebugAutoAttackStringTaps` (int, default 1 = today's single tap, so every existing
scenario is untouched); N > 1 re-presses at chain-open. Then:

- **`s4-string`** (taps 3, defender `Off`): N `COMMIT branch 0`; contacts at 200/550/900 ms ± 30;
  `DAMAGED` ledger stepping exactly 15; `HITSTUN` spans 0.400 ± 20 ms; chain latency
  `RELEASE OFF → ACTIVATE` ≤ 2 frames; final swing's `elapsed` matches its authored total.
- **`s4-guarantee`** (taps 3, defender `PeriodicDodge` timed into the string): `REFUSED` naming
  `State.Hitstun` between contacts, and **zero** `DODGE` lines inside the string span — the
  guarantee, observable.
- **`s4-block`** (taps 3, defender `HoldBlock`): `BLOCKED` staminaDamage=5 per hit; `BLOCKSTUN`
  spans matching the per-swing values (0.400 then 0.250); no `GUARD BREAK`.
- `s1-*`/`s2-*`/`s3` re-run green (single-tap path untouched by construction), `--self-test`
  extended, and **each new band made to fail once on purpose** before it is trusted.

## Netcode notes — stated, not solved (the lunge-stop precedent)

Hitstun is born network-correct (server decides, bool replicates, OnRep applies). `StringIndex`
replicates; the *chain-out cancel* is client-initiated input-adjacent state like the buffer, and
the swing a remote proxy sees rides GAS's montage replication — both named for Netcode's pass
rather than solved here. No new loose tags, no new `SetTimer` (the window deadline is a
tick-compared timestamp, blockstun-style).

## Verification pass (play, after the build)

Cadence and mash reliability (the buffer extension's proof); hold-to-heavy mid-string and its
tell; dodge-cancel of a string windup (pre-commit cancel unchanged per hit); the blocked-string
exchange at the proposed safety numbers; **the buffered-aim 1vX test the trap prescribes** —
target A, target B 180°, back to A — which decides aim-at-activation vs aim-latched-at-press with
felt evidence; and the designer's clip-roster trial.

## Docs owed by the package

`CLAUDE.md` (string rules become build-true; hitstun's line; Knockdown & Oki's entry loses the
hitstun fork and keeps movement-lock + terminator); `Docs/Combat-Decisions.md` (entries are
written; discharge/annotate the buffered-aim trap with the 1vX result; felt-table rows for every
number above; tuning-map rows: cadence → `HitstunSeconds`/`ChainOpenAfterRecoverySeconds`, string
drops → `StringLinkWindowSeconds`, safety → per-swing `BlockstunSeconds`); `Docs/Debug-Instruments.md`
(`HITSTUN`/`STRING` trace tags, the fixture mode and its aliasing note, three matrix rows); header
comments per convention.

## Sequencing

1. **C++ core** (editor closed, one rebuild): per-swing data + accessors, string state, chain-out,
   buffer extension, hitstun + `Jump()` check, traces. Verify `s1/s2/s3` still green single-tap.
2. **Assets**: three montages via the loop above (human notify step), values onto the CDO,
   restart, instance-vs-CDO checks per the staleness rules.
3. **Checker**: fixture mode, three `s4-*` scenarios, self-test, deliberate failures.
4. **Play**: the verification pass; the clip trial; retune from the tuning-map rows.
5. **Docs + closedown**: the owed updates above; delete this file.
