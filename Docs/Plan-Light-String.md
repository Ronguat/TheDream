# Plan — Light String (work-in-flight)

**This is the working plan for the Light String slice, agreed at the 2026-08-16 plan session.
Delete this file when the slice ships** — its durable content lives in `Docs/Combat-Decisions.md`
(the 2026-08-16 plan-session entry), in header comments, and in `CLAUDE.md` once the rules are
build-true. The reasoning behind every design choice here is in that dated entry; this file is the
*how*, and it is disposable by design.

**No questions remain open** — the interrupt fork was answered at the session's close, the same
night's **knockback dispensation is folded in** (§ Knockback), and the blocked-hit reading was
settled by the designer: same centring as a clean hit, notably less ground conceded.

---

## Scope

**In:** the 2–4 hit light string, hold-to-convert preserved per hit; **hitstun** (the guarantee's
mechanism, blockstun-patterned); **knockback as a spacing reset** on every non-final light hit,
with the reduced deflection through a guard (§ Knockback); the long-recovery safety redesign;
chain-on-whiff gating; per-hit authored values; the chain-press buffer extension; `Jump()`'s
hitstun check; trace additions; regression scenarios (the loop-coverage choice is **scenarios**,
not a trap); and the **authored clip-trial step** — the final roster and string length are chosen
by the designer in PIE, not by this plan.

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
3. **Strings chain regardless of contact** — whiff included. The whiff-punish cost this named is
   **repaired by decision 6** below: the window is not shrunk, it is converted into the
   delay-and-bait game. Interplay still judges the result.
4. **The clip roster is an authored trial.** The build scaffolds on all-`Attack4` 4-hit
   (S1 → S2 → S3 → S4, all `_Complete_IP`, all already migrated); the trial step swaps and trims.
5. **Knockback is a spacing reset and ships here** (the same night's dispensation): every
   non-final light hit carries the target to one authored position relative to the attacker,
   identical every time. Knockdown in every grade stays at Knockdown & Oki — including its new
   **hard knockdown** grade for the charged.
6. **No light is truly safe — "psychological everywhere."** Recovery is authored long (superseding
   the felt 0.40, knowingly) and only chaining skips it, so safety on whiff and block alike is the
   defender's hesitation against the next hit. Supersedes the spec's "first hit safe on block".
7. **A blocked hit is the same reset at a shorter authored distance** (the designer's correction
   of a flagged reading): full lateral centring, notably reduced pushback — one mechanism, two
   spacings.

## § Decided at the session's close — hitstun cancels everything

**Hitstun cancels the victim's active abilities on entry, committed or not** (the user,
2026-08-16, choosing over refuse-activation-only and a cancel-windup-only middle ground). A hit
through your swing beats your swing: the DKO trading model, making the light the interrupt to a
coiling heavy — commitment governs what a victim may cancel *voluntarily*, not what being hit
does to them. Blockstun is untouched: it cancels nothing and never did. Mechanically this is
`EnterHitstun` cancelling abilities the way death's path does, minus everything else death does —
and it is one more thing the ordinary `EndAbility` funnel must survive, which it already does.
**The build's dated entry records this ruling**; the plan-session entry deliberately left it open.

---

## Mechanism

### Per-swing data, and a deliberate asymmetry

New struct `FTDStringSwing`: `{ Montage, ReleaseStartSeconds, CoilEndSeconds, Damage,
StaminaDamage, BlockstunSeconds, HitstunSeconds, RecoverySeconds }`, in a new
`StringSwings` array on `UTDChargedAttackAbility` covering **hits 2..N only**.

**Hit 1 stays structurally the current authored surface** — `AttackMontage`, the ability-level
`ReleaseStartSeconds`/`CoilEndSeconds`, and `Branches[0]`, untouched as *properties*. The
asymmetry is deliberate and load-bearing: moving those UPROPERTYs would orphan every play-verified
CDO override (the `ATDCombatCharacter`-split warning, same mechanism). **One value inside them
retunes by design**: `Branches[0].RecoverySeconds` 0.40 → 0.60 (the long-recovery redesign), which
moves the `s1-light`/`s2-light` elapsed bands in the same package — authored truth moving, not
bands patched green. Everything else in `s1-*`/`s2-*`/`s3` is unperturbed by construction.

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

### Knockback — the spacing reset (the 2026-08-16 dispensation)

**A clean non-final light hit carries the target to one authored position relative to the
attacker — the same spot every time, every hit.** Fixed destination, variable magnitude: at hit
resolution (authority, beside hitstun), compute `attacker location + attacker facing ×
HitSpacingCm`, and run a canned translation with a curve to exactly there — `StartLunge`'s
target-side twin, on the same root-motion-source channel, for the same netcode-shaped reasons.
The impulse alternative is rejected on the designer's own instinct, recorded: fixed-magnitude/
variable-destination is the opposite of the determinism wanted. **The lunge stopping on a hit
(2026-08-14) is what makes the reference frame planted** — the attacker is stationary at contact
by prior design.

- **The clean hit re-centres**: the destination sits on the attacker's facing axis, so lateral
  error zeroes — that is part of "exact same relative location", and it is what makes every chain
  hit's approach a constant problem.
- **A blocked hit is the same mechanism at a shorter distance** (the designer's ruling, settling a
  flagged reading): destination `attacker location + facing × BlockedSpacingCm` — **lateral
  centring at full strength, identical to a clean hit; only the backward component shrinks.** One
  mechanism, two authored spacings. One HOW guard: the destination **never pulls a defender
  inward** when contact happened beyond it — vacuum blocks are a known artifact class; the clamp
  is one `max()` to remove if the pull-in is ever wanted.
- **A dodged hit touches nothing** — no contact, no knockback, unchanged.
- **The ender, heavies and charged displace nothing this slice** — their knockdowns (the charged's
  now *hard*) are Knockdown & Oki's, the same deferral the terminator already carries.
- **Duration fits inside hitstun** (0.20 vs 0.40), so the target never acts mid-slide; the curve
  must average 1.0 like every strength curve in the project.
- **Walls compress the reset** — determinism holds in open space; corner-carry at the arena edge
  is accepted as emergent until play objects.

**The knockback-budget trap discharges by design when this lands**: one authored spacing replaces
the two coupled numbers, and the connect condition is the single inequality
`HitSpacingCm ≤ next hit's covered range` (today 150 against 100 + 200 + 150 − standoff 40).

---

## Numbers — all proposed, all destined for the felt table as unfelt

| Value | Proposal | Why this shape |
|---|---|---|
| Swings 2–3: Damage / StaminaDamage | 15 / 5 each | Flat, matching hit 1; the trial may taper |
| Swings 1–3: RecoverySeconds | **0.60** (hit 1 retuned from the felt 0.40) | The mindgame's substrate: a lone light is genuinely punishable, chaining skips it. Moves the `s1/s2-light` bands in-package |
| Swing 4 (ender): RecoverySeconds | 0.75 | The spec's "heavy endlag" on the last hit, above the new baseline |
| BlockstunSeconds, all swings | 0.40 | Value unchanged, meaning changed: 0.40 < recovery − 0.05, so a stopped attacker is technically punishable on block — the chain threat is the cover ("psychological everywhere") |
| HitstunSeconds L/H/C | 0.40 / 0.50 / 0.60 | Covers the 350 ms gap +50 ms; ladder mirrors blockstun |
| `HitSpacingCm` | 150 | = `MaxReachCm`: the reset parks the target at the sword's edge; the trial tunes it |
| `KnockbackDurationSeconds` | 0.20 | Inside hitstun's 0.40 — the target never acts mid-slide |
| `BlockedSpacingCm` | 100 | "Notably less" than the 150 reset — a guard concedes a third of the ground a hit does; clamped never-inward |
| `StringLinkWindowSeconds` | 0.40 | Deliberately generous first probe, `MinimumBlockSeconds`-style |
| `ChainOpenAfterRecoverySeconds` | 0.0 | Chain at recovery start; the knob exists for the trial |

Safety is the same bet everywhere: punishing a light — whiffed or blocked — means correctly
reading that no follow-up is coming, and the delay dial (chain any time from recovery start
through the link window) is continuous, so waiting *is* the read. Recorded in the entry.

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
  `DAMAGED` ledger stepping exactly 15; `HITSTUN` spans 0.400 ± 20 ms; **`KNOCKBACK` then a
  post-slide `TARGET`-style spacing read of `HitSpacingCm` ± tolerance on every non-final hit —
  the determinism guarantee living in a band**; chain latency `RELEASE OFF → ACTIVATE` ≤ 2 frames;
  final swing's `elapsed` matches its authored total.
- **`s4-guarantee`** (taps 3, defender `PeriodicDodge` timed into the string): `REFUSED` naming
  `State.Hitstun` between contacts, and **zero** `DODGE` lines inside the string span — the
  guarantee, observable.
- **`s4-block`** (taps 3, defender `HoldBlock`): `BLOCKED` staminaDamage=5 per hit; `BLOCKSTUN`
  spans 0.400 flat; post-block spacing reads **`BlockedSpacingCm` ± tolerance, never inward** —
  the same determinism band as the clean reset, at the guard's shorter distance; no `GUARD BREAK`.
- `s1-*`/`s2-*`/`s3` re-run green (single-tap path untouched by construction), `--self-test`
  extended, and **each new band made to fail once on purpose** before it is trusted.

## Netcode notes — stated, not solved (the lunge-stop precedent)

Hitstun is born network-correct (server decides, bool replicates, OnRep applies). `StringIndex`
replicates; the *chain-out cancel* is client-initiated input-adjacent state like the buffer, and
the swing a remote proxy sees rides GAS's montage replication — both named for Netcode's pass
rather than solved here. **Knockback rides the root-motion-source channel on the target,
server-decided at hit resolution** — how a victim's client experiences an unpredicted forced
translation is squarely Netcode's prediction-window problem and is named for it, exactly as the
lunge stop was. No new loose tags, no new `SetTimer` (the window deadline is a tick-compared
timestamp, blockstun-style).

## Verification pass (play, after the build)

Cadence and mash reliability (the buffer extension's proof); hold-to-heavy mid-string and its
tell; dodge-cancel of a string windup (pre-commit cancel unchanged per hit); the blocked-string
exchange at the proposed safety numbers; **the delay-and-bait game itself** — delayed chains
catching a premature punish, the spacing reset holding through it; a look at the reset compressing
against a wall (corner-carry, accepted until it reads wrong); **the buffered-aim 1vX test the trap
prescribes** — target A, target B 180°, back to A — which decides aim-at-activation vs
aim-latched-at-press with felt evidence; and the designer's clip-roster trial.

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
   buffer extension, hitstun + `Jump()` check, **the knockback translation (both variants)**, the
   string-taps fixture (inert at 1), traces. **Every new mechanism defaults inert** (0 hitstun, 0
   spacing, no swings, chainable false), so `s1/s2/s3` re-run against today's values and bands —
   green here proves the refactor invisible. The recovery retune and its band moves are sitting
   2's, where the values land.
2. **Assets**: three montages via the loop above (human notify step), values onto the CDO,
   restart, instance-vs-CDO checks per the staleness rules.
3. **Checker**: fixture mode, three `s4-*` scenarios, self-test, deliberate failures.
4. **Play**: the verification pass; the clip trial; retune from the tuning-map rows.
5. **Docs + closedown**: the owed updates above; delete this file.
