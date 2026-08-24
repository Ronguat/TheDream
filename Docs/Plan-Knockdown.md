# Plan — Knockdown

**Work-in-flight, deleted on delivery.** The reasoning behind every choice here is the
plan-session entry in `Docs/Combat-Decisions.md`; this file carries only the *how*. Design
direction is ruled; **execution awaits a greenlight at the session that picks this up.** The
animation migration is the one piece already shipped (`97f4acb`, verified on disk). Numbers are
first attempts unless marked derived.

**Re-scoped 2026-08-24**: the closing package, and what moved to Polish and Tuning-Rig, is
`Plan-Animation.md`'s re-scope section; the verification bar is that day's decision entry. Read
this file's F, H and D through it — F ships on the rough, H is a legibility check, D's mechanical
scenarios stay in-package.

---

## What exists today, measured

- The ender, heavy and charged deal damage plus hitstun and displace nothing —
  `GetKnockbackSpacingCm` returns 0 for anything but a non-final string light. This slice
  replaces what those hits do to their victim wholesale: a graded swing knocks down and never
  hitstuns, leaving each swing's `HitstunSeconds` with exactly one job — keying the attacker's
  movement return through the on-hit waiver.
- `s4-360` asserts its first burst only, because the ender currently parks bodies on the facing
  axis. The exclusion lifts here.
- Knockback baseline: `HitSpacingCm` 150 / `BlockedSpacingCm` 100 / 0.20 s. **The knockback
  re-author folds into this slice**: a reset at a third of the light's ~450 unobstructed
  coverage does not read as moving your target. Knockdown spacing sits farther still.
- In hand from the migration: `Dagger_Rise1_V2` (1.000 s), `GreatSword_Resurrection2` (2.000 s),
  `UnarmedV4_Rise1` kip-up (1.267 s); pack skeletons verified identical; compat links live.

## The down state

```
clean hit with a grade
  → forced facing (rate-limited turn to attacker; applies to ALL hitstun)
  → carry: fixed destination, RADIAL — attacker + (attacker→victim bearing) × KnockdownSpacingCm,
    Z natural; never-inward clamp; zero-radial degenerate falls back to attacker facing
  → JAIL    everything refused, movement+jump locked, invincible, presses buffer
  → CHOICE  options legal — dodge, block, attack, or a free neutral STAND (jump);
    a buffered press fires the frame the jail ends; still invincible until any rise begins
  → AUTO-RISE [last 0.5] committed, vulnerable, locked; a press buffers and fires at stand

  normal: jail 1.0 → choice 1.0 → rise 0.5      hard: jail 1.5 → choice 0.5 → rise 0.5
  (both total 2.5; auto-rise begins at 2.0 in each)
```

- **The carry axis follows the volume's purpose.** The string's forward knockback centres on the
  facing axis (the next hit needs its target in front); 360° hits — the ender's knockdown, the
  get-up attack's knockback — radiate along the attacker→victim bearing, so a side target flies
  to its own side and a crowd scatters outward. Two axes by design, never to be unified.
- **Invincibility covers the floor and ends the moment any rise begins**, auto or chosen. Each
  option prices its own rise: dodge i-frames, guard up, attack naked-but-threatening, do-nothing
  plainly hittable.
- **A neutral rise is fully committed once started — the auto-rise and the chosen stand alike**:
  no options, no movement, hittable; the stand chooses *when*, never *whether* the rise commits.
  A meaty timed onto one is a guaranteed hit; the loop is escapable every cycle by any
  choice-window press, and even at zero stamina the get-up attack remains. Accepted; Interplay
  judges.
- **Hard grade (heavy, charged): same total, meaner on three axes** — the directional dodge is removed
  (a stationary, i-framed kip-up replaces it), the neutral stand is removed, and the split
  tightens (jail 1.5 / choice 0.5 against normal's 1.0 / 1.0), so hard's timing mixup is both
  priced and narrow. Hard's set: kip-up, block, attack, wait. Exhausted + hard is the game's
  maximum funnel — get-up attack or metronome — named and accepted. Each grade's split is its
  own dial if either misreads in play.
- **Exhausted carve-out**: regen suppression **the opponent can renew indefinitely** does not
  bind the exhausted — knockdown is its only member today. The guard break's suppression stands
  by derivation: every break exhausts, and an exhausted player cannot raise a guard, so no
  second break can renew it — bounded, therefore a cost. Broken-then-floored serves the break's
  clock, then regens. Self-inflicted pauses always bind. Knocked down while exhausted, the 25/s
  runs — one down-cycle returns ~62 stamina.
- Knockdown supersedes hitstun, cancels the victim's abilities through the death-path funnel,
  resets their string (`ResetString` — a mid-string attacker stands up to swing 0; a stale
  chain tap expires inside the jail; a *held* attack button legitimately fires the get-up attack
  at the boundary), and calls `OverrideParryRecovery`.
- Airborne victims are knocked down mid-air: the carry's XY applies, Z follows gravity, no
  ground snap.
- Regen resumes at the **stand boundary** — when `State.KnockedDown` clears, the rise being the
  lockout's time to the last frame. No tail; tails belong to self-inflicted action pauses. A
  priced exit swaps suppressors seamlessly (its own action-pause or the guard's drain), so no
  path regens mid-rise.
- Death wins outright over knockdown; the dying fall's look is Death-full's.

**Edge composition — all of it falls out of standing rules; none needs code of its own.** A
wrong-facing guard is a clean hit, the guard dying in the entry cancel — and the still-held
button resumes at the choice window, so held-guard *is* the block get-up, uniform with
held-attack. A correctly-facing guard is never knocked down (clean hits only; the heavy stays
plus-on-block, the charged breaks). Mid-blockstun and mid-guard-break compose as overlapping
refusals the jail outlasts. Mid-hitstun supersedes; mid-dodge negates; the parry window and
Grace are unreachable; parry recovery is overridden by the schema's own call. A knockdown
landing mid-knockback-slide replaces the slide (last hit wins). The dead no-op.

## Numbers

| Value | Current | Plan | Basis |
|---|---|---|---|
| Jail / choice, **normal** | — | **1.0 / 1.0** | the lockout held to its minimum, the agency doubled — the fast layer's knockdowns are escape-rich, and boundary oki is light-only |
| Jail / choice, **hard** | — | **1.5 / 0.5** | the meaner split at the same total: it holds every exit back far enough that a committed follow-up's arrival window **overlaps the forced rise window** (a delayed heavy or a charged both reach it) — hard oki needs the setup time and the hit that bought it earned it. Authored per grade; each grade's split is its own dial |
| `KnockdownRiseSeconds` | — | **0.5**, shared | clips rate-fit to it; auto-rise begins at 2.0 and both grades stand at 2.5, keeping every total-keyed derivation (the exhausted ~62, the netcode line) grade-invariant |
| `HitSpacingCm` / `BlockedSpacingCm` | 150 / 100 | **350 / 225** | the knockback re-author, folded in: you are beating your target up and *moving* them — 350 pushes to the edge of the string's connect envelope (the inequality's ceiling is ~410; the margin narrows to 60 and the connect trap runs hot, re-annotated at ship). Blocked keeps the notably-less ratio |
| `KnockdownSpacingCm` | — | **450** | radial fixed destination, shared across grades; farther still than the re-authored knockback — a full light's coverage of separation, so re-engaging the riser costs real travel. Sanity-check at build against `Death_Bw`'s baked travel |
| `KnockdownCarrySeconds` | — | **0.35** | inside the fall segment; optional time-mapping curve, knockback's contract |
| Forced-facing turn rate | — | **720°/s** | **derived floor ≈ 655**: 180° must complete well inside the shortest hitstun (0.50) — re-derive if any `HitstunSeconds` drops |
| `ETDKnockdownGrade` per branch / ender swing | — | light **None**, heavy **Hard**, charged **Hard**, ender **Normal** | committed single hits knock down hard; the string's volume finisher knocks down normal — the string already extracted its 45, and generous escape is the volume trade. This is also what prices the rise-catch ladder (45+normal by reaction, 25+hard by read, 40+hard by callout) and the mid-string hold-conversion (55+hard but escapable, against the ender's guaranteed 45+normal) as choices instead of dominances. The geometry pairing — the kit's one AoE knockdown carrying the gentle grade, so a crowd can never be hard-floored — is authored per swing, not structural; a future weapon may pair them differently |
| `ParryLockoutFloorSeconds` | — | **0, reserved** | the lockout is derived (planned total − elapsed at catch), preserving per-tier punish for free; the floor is the authored half, spent only if play asks |
| Get-up attack: release at | — | **0.30 from press** | fast or it is unthreatening; no coil, no hold |
| Get-up attack: release live / blocked spacing | — | **0.35 / 100** | the long release deters **lunging hitboxes, never parries** — a parrier cannot walk an open window into the volume (movement lock), and dedup decides the whole parry question at release-open, where the rise's tell precedes it by a full window-width. The split's only trade is ward vs whiff-punish width under the fixed total |
| Get-up attack: recovery | — | **0.60** | release extension eats recovery, never the total: 0.30 + 0.35 + 0.60 = **1.25**, the move's commitment fixed. Tier-grade whiff punish; the parried price derives from the total (≈0.95 caught at first contact) |
| Get-up attack: damage / hitstun / spacing | — | **10 / 1.00 / 250** | hitstun **derived, both fences**, from riser-free at contact+0.95: floor 0.95 (victim frees after the riser, or there is no initiative), ceiling 1.10 (the fastest follow-up must meet a buffered guard, or it is a confirm); 1.00 is the 50 ms idiom |
| Get-up attack: blockstun / stamina damage | — | **0.65 / 10** | blockstun derived from *which punish is guaranteed*: `riserFree(0.95) − punishArrival` — at 0.65 the light punish is guaranteed (+100 ms) and the heavy is a 50 ms-margined read, never a frame race; B ≤ 0.55 is the dial if the heavy should confirm. 300 ms from plus. Stamina damage low by ruling: blocking it is the acceptable safeguard, initiative real but less than a parry grants |
| Get-up attack: waiver on clean hit | — | **instant, the base rule — and the waiver requests a resume pass** | protection is preparation-priced: block held through the read rises the frame it lands (`bResumePending`, one line, **global** — every attack's clean hit honors held intent; parry is structurally immune, it neither buffers nor resumes). Fallback if play demands automatic protection: Grace-shaped i-frames, recorded, not built |
| Dodge get-up / kip-up | — | **50 stamina + i-frames** | real `GA_Dodge` semantics |
| Block get-up | — | guard live from activation; full cost and commitment | all blocks are created equal |
| Heavy/charged/ender `HitstunSeconds` | 0.50 / 0.60 / 0.55 | **0.35 / 0.45 / 0.55, repurposed** | the victim never feels them (knockdown supersedes); they price only the attacker's walk-start — pure oki-tempo knobs. Retuned to the principle *harder knockdown, better oki*: both hard sources get a symmetric 0.30 of approach before they can act, and against the grade splits that is ~1.1 s of attacker freedom before a hard-downed victim's earliest exit, versus the ender's thin 0.45. Whiffs get no waiver at all, so the land/whiff asymmetry widens with the swing's size. All three sit below the inertness ceiling (the waiver only releases early); the light's 0.55 is untouched and still derived (string guarantee) |

Untouched by design: the ladder's timings, the string, dodge, block, and the parry's window,
Grace, whiff recovery and rewards.

---

## Sub-slice A — jump becomes an ability

`UTDJumpAbility` + `GA_Jump`, granted via `DefaultAbilities`, bound via an `IA_Jump` row in
`AbilityInputActions` (read the IA asset and the C++ binding together). Activation calls the
character's jump; `OnJumped`/`Landed` keep the regen pause keyed to the action. The five
hand-restated checks in `ATDCombatCharacter::Jump()` collapse into the shared base's refusals.
Refusal not buffered: a stale jump is a jump nobody asked for.
**Verify:** jump works in play; refused during block commitment, hitstun and movement locks —
today's observable behaviour, now for free; regen pause pair intact; full matrix green.
**Commits alone, first** — knockdown then refuses jump through the base like everything else.

## Sub-slice B — lockouts get their teeth

State-driven movement locking on `ATDCombatCharacter` (`DoMove`'s gate joins the ability-side
`bLocksMovement`), applied to **hitstun** and the **guard break** — full loss of control except
the camera. Blockstun deliberately untouched. **Discharges the "Before Stun" trap in the same
commit.**
**Verify:** zero movement during break stun (new `s2-*` assertion); `s4-guarantee` unchanged;
the flinch race unaffected.

## Sub-slice C — the knockdown state machine

- `bKnockedDown` + replicated grade: **ninth member of the state family**, same contract —
  server decides, bool replicates, `OnRep` applies the native `State.KnockedDown` tag,
  Tick-checked timestamps for the jail/choice/rise boundaries.
- `EnterKnockdown(Grade, Attacker)` from the hit path when the swing's grade ≠ None: replaces
  `EnterHitstun` for that hit, cancels through the death-path funnel, resets the victim's
  string, calls `OverrideParryRecovery`, starts the radial carry, begins forced facing. The
  waiver fires unchanged: `WAIVER` + `MOVE UNLOCK` at contact + that swing's `HitstunSeconds`.
- **Forced facing**, rate-limited at the derived rate, applies in `EnterHitstun` too — every
  clean hit turns the victim's body toward the attacker; the camera never moves. The buffered
  dodge's stored heading is re-anchored so a turn between press and fire cannot skew it.
- **Floor invincibility**: the hit path skips a knocked-down target until any rise begins —
  character-side state, the i-frame check's shape. **The check is inclusive of the rise-begin
  frame**: a hit arriving on the exact frame a rise starts resolves to the defender — ties at
  protective boundaries go to the protected, so the one place engine tick order could coin-flip
  an outcome is ruled instead.
- **Regen**: `State.KnockedDown` suppresses; `TickStaminaRegen` gains the carve-out — while
  exhausted, refreshable-class suppression is ignored; the break's bounded suppression and every
  action-tag pause still bind. Suppressors independent; the tick takes the max of those that
  bind. Resume at the stand boundary, with the tag.
- **The knockback re-author rides this sub-slice's CDO session**: `HitSpacingCm` 350,
  `BlockedSpacingCm` 225, and the graded swings' repurposed `HitstunSeconds` (heavy 0.35,
  charged 0.45; the ender's 0.55 and the light's derived 0.55 stay). The `s4-string`/`s4-block` knockback assertions self-adapt (they hold
  spacing against the authored value the trace prints); the chain cadence is untouched
  (chain-out is time-gated, not position-gated). The connect margin narrows to 60 cm — the
  inequality's trap annotation updates at ship, and any reach/travel retune from here on must
  check it first.
- The jail refuses everything from the shared base; presses buffer; parry never buffers.
- **Trace**: `KNOCKDOWN` (grade, spacing, **bearing** — the radial axis's angle off the
  attacker's facing, so the 1v1 coincidence ≈0° and the s4-360 divergence ≈±90° are both
  assertable), `KNOCKDOWN RISE` (auto|dodge|block|attack|kipup|stand), `KNOCKDOWN STAND`,
  `FACING FORCED` (once per hit, with the turn's span for the derivation check).

## Sub-slice D — get-up options

Options unlock at jail end; a buffered press fires on the boundary exactly; **the action is the
exit** — no shared pre-rise.

- **Dodge get-up**: `GA_Dodge` from the down state — yaw-snap to the held direction, one
  `Roll_Fw` montage rate-fit to the dodge's duration, authored travel, i-frames, 50 stamina.
  Mid-roll the body faces its travel direction — a deliberate, logged exception to the
  strafe-always convention; facing re-converges to camera at `EndAbility`.
- **Block get-up**: `GA_Block` from the down state — guard mechanically live from activation,
  rise montage over it; cost, commitment and drain all standard. **Committed through the rise
  like every exit**: movement and acting-out begin at the stand boundary (a press mid-rise
  buffers and fires through the guard on standing); the minimum-block floor is subsumed by the
  rise outlasting it. The trade against the free stand, priced: 0.5 s guarded-committed instead
  of naked, for ~15 stamina (initial 10 + drain) plus the guard's regen-pause tail — front-arc
  only, and a meaty heavy on a sub-50 bar breaks the guard on the way up. **The guard's
  direction is a call, latched at the press**: the camera heading at activation (the free
  camera during down-time is the aiming instrument), the body turning to it at the
  forced-facing rate with the guard live through the turn, committed until stand — the dodge
  get-up's composite-input precedent applied to aim. Repositioning after the press can still
  flank it: the walk-around stays a read, not a dead mechanic. The recorded fallback, if the
  latched call proves too flankable in play, is a camera-tracked rise — steerable like the
  standing guard it becomes.
- **Kip-up** (hard only): the dodge input while hard-down — stationary, i-framed, 50 stamina,
  the kip-up clip playing **with its own root motion** (the one deliberate exception to the
  migrated flag pair). Directional input not honoured.
- **Neutral stand**: the jump input — the default rise fired anywhere in the choice window.
  Free, no protection, committed once started — pure timing variance against the meaty, and the
  autonomy to use a window a teammate buys in 1vX. Held movement input was rejected as the
  trigger: incidental WASD would manufacture rises nobody called; a jump press is deliberate.
- Hard refuses the directional dodge and the neutral stand; block, attack and kip-up survive.
  Exhaustion refuses block, dodge and kip-up as ordinary defensive actions — the exhausted
  normal-downed player holds get-up attack, stand, or wait; the exhausted hard-downed player
  holds get-up attack or wait alone.

## Sub-slice E — the parried attacker ends and locks out

- **Catch → the parried ability ends through the ordinary funnel** (facing, lunge, homing, tags
  restored; hitbox dead for *everyone* — a caught swing must not keep killing bystanders) **→
  `EnterParryLockout(RemainingSeconds)`**.
- **Duration derived at the catch**: `remaining = the swing's planned authored total − elapsed`
  — pure authored-value arithmetic, no montage reads — so a parried charged pays more than a
  parried light and every per-tier punish window is preserved. `ParryLockoutFloorSeconds` is the
  authored half, reserved at 0.
- `State.ParryLockout`: the **tenth** replicated family member — refuses everything, takes the
  full movement lock, composes with recoveries by the standing lockout-overrides-recovery
  schema. The registry's RESERVED comment retires; the retired-names bridge gains the row.
- **String loss stays explicit**: entry calls `ResetString("parried")`. `bParried`'s chain gate
  is subsumed by the ability no longer existing.
- Presentation: the swing cuts to locomotion mid-motion — abrupt and honest; the recoil clip
  this state is the natural home for stays **Polish's**.
- **The window≥release floor retires with this model.** It bought tell-timing sufficiency — a
  forgiveness guarantee, not correctness — at the price of capping every release in the game.
  Arrival-timed parries need no floor once a catch ends the attack, and for stationary volumes
  the parry question collapses to a single boundary test at release-open anyway (dedup consumes
  a hit target for the swing; the movement lock forbids walking an open window into a volume).
  The parry window keeps only its real fence, the anti-option-select ceiling. Ship routing
  updates the tuning row, including the fence's corrected guarantee: **first contact, no prior
  catch** — a catch collapses cover to Grace, deliberately.
- **`s5-parry` re-bands in-package**: the attacker's `ABILITY END` at full authored total
  inverts to ending at the catch; new assertions — `PARRY LOCKOUT` span equals planned total −
  catch elapsed ±25 ms, zero `DAMAGED` by a parried swing after its catch. The bystander half of
  the deadness is unwitnessable in 1v1 and joins the second-attacker trap's items.

## Sub-slice F — the get-up attack

`UTDGetUpAttackAbility` (a `UTDMeleeAttackAbility` subclass — trace, damage, hitstun, knockback
and waiver machinery inherited), answering `InputTag.Attack` with `State.KnockedDown` in its
`ActivationRequiredTags`; `GA_Attack` refuses under the same tag, so the input routes cleanly.
Single fixed swing: no hold conversion, no chain, no string membership. Knockback radial —
a 360° separation tool sends every victim away on their own bearing; the facing-axis clamp
would drag a target behind the riser through their body. Nothing about it may guarantee a
follow-up. Regen tax standard — thrown while exhausted, it suppresses your own recovery: the
priced gamble.

**Committed from activation; the standard waiver on contact, made reachable by held intent.**
A `CommittedTag` applies the moment it activates — no startup-cancel window; the exit was chosen
from the floor, and a cancellable startup would make it a free probe — and a real
`ReleaseCommitmentTag` override drops it on a clean hit, instantly, exactly as every attack
waives. The waiver sets `bResumePending`, so a guard held through the read rises the frame the
hit lands — block never buffers, and the resume tick otherwise only sees ability ends. Whiff or
blocked: committed for the full 0.60, naked. Offense untouched, keyed to recovery's end — the
hitstun derivation's anchor.

**Two things the subclass must add, or the design silently regresses.** The base melee class
carries no authored phase timings — those are the charged ladder's branch structure — so the
subclass authors its own `ReleaseAtSeconds` / `ReleaseSeconds` / `RecoverySeconds`
(0.30 / 0.35 / 0.60) with rates derived from the montage's measured position, the
three-durations-and-the-clip-conforms model. And the CDO must populate `TargetImmunityTags`
with the i-frame tag — the inherited default is empty, and empty silently makes the get-up
attack undodgeable. The blend-out condition is checked for the composed montage like any other.

## Sub-slice G — fixture, scenarios, trace

**Loop coverage is satisfied in-package. Two dated traps are owed at ship, named now**: the
**1vX half** of knockdown (meaty loops, a second attacker hitting a riser, the parried swing's
bystander deadness) is unproducible until a second attacker exists — it joins the Parry Grace
second-attacker trap for discharge in the same sitting; and the **airborne knockdown** path has
no fixture — filed as untested, human-verified once at build.

- Fixture: `DebugGetUpMode` on the defender — `Wait` / `DodgeGetUp` / `BlockGetUp` /
  `AttackGetUp` / `StandGetUp` — pressing its input at jail-end + ε when knocked down. The
  attacker needs nothing new: `…HoldSeconds` 0.22 throws knockdown heavies, 0.8 hard chargeds.
- Scenarios (bands from CDOs at build time, never from this plan):
  - `s6-knockdown` — the ender (taps 3) vs `Wait`: `KNOCKDOWN grade=normal`, jail/choice/rise
    spans at 1.0/1.0/0.5 ±25 ms, carry spacing ≥ authored, **zero `DAMAGED` on the victim
    between `KNOCKDOWN` and `RISE`** while the attacker keeps swinging (floor invincibility
    observable; n=0 of attacker swings fails), `REFUSED` naming knockdown inside the jail,
    `MOVE UNLOCK` at contact + the ender's 0.55.
  - `s6-getup` — heavy vs `DodgeGetUp`: `RISE by=dodge` within ε of jail end, i-frames hold, 50
    on the ledger. Vs `AttackGetUp`: release ≤ 0.30 + band from press, attacker `DAMAGED`,
    recovery span 0.60, no `STRING`/chain lines after it, ever. Vs `StandGetUp`: `RISE by=stand`
    inside the choice window, strictly earlier than the auto-rise's clock, no cost on the
    ledger.
  - `s6-hard` — heavy vs `DodgeGetUp`: `grade=hard`, jail/choice spans at 1.5/0.5 ±25 ms,
    directional dodge `REFUSED`, kip-up fires with travel ≈ 0; a `StandGetUp` pass asserts the
    stand `REFUSED` under hard and the auto-rise arriving on the full clock; one charged pass
    asserts `grade=hard` from the other source.
  - `s6-exhausted` — pre-drained defender (the `s5-parry-reward` pre-block trick) knocked down:
    **the stamina ledger rises during the down-span** — the carve-out observable in one
    assertion — block/dodge presses `REFUSED` naming exhaustion, get-up attack fires.
  - `s5-parry` re-banded per sub-slice E.
  - `s5-waiver` gains the held-intent assertion — the attacker holds block through its own swing
    (`HoldBlock` plus auto-attack on one pawn, the deliberate exception this rule needs): its
    `BLOCK up` lands within a frame of its own `DAMAGED`.
  - `s4-360` widened past its first burst. The radial carry rewrites its geometry: the finisher
    knocks both bodies outward to their own sides, parking them ~450 cm out — far beyond the
    stationary attacker's reach. Expected shape: burst 1 as today plus two `KNOCKDOWN` lines;
    every later burst damages zero targets.
  - `--self-test` first; every new assertion made to fail once on purpose.

## Sub-slice H — animation (human steps, interleaved)

All montages **montage-from-clip, never duplicated from an attack** (inherited-notify trap);
every clip conforms to the authored durations at derived rates; the blend absorbs pose seams at
both ends. The blend-out trap's general form is checked per montage: `length − BlendTime ×
phaseRate` must clear every phase boundary.

- `AM_Knockdown` from `V3_Death_Bw` — fall fitted to the carry/fall span,
  **`bEnableAutoBlendOut` false** so the last frame holds as the ground pose.
- `AM_Rise` from `Dagger_Rise1_V2`; `AM_RiseHard` from `Resurrection2` — fitted to
  `KnockdownRiseSeconds`; blend-in from the ground pose, blend-out to idle.
- `AM_KnockdownRoll` from `V1_Roll_Fw` — **flip that clip's flag pair first** (it still has root
  motion enabled from the original dodge build); displacement is the dodge's, mechanical.
- `AM_KipUp` — **revert `bForceRootLock` to false on that one clip**; its gentle root motion is
  the travel, by ruling.
- `AM_GetUpAttack` — rise front-half + a 360° swing chosen by the designer's eye
  (`Attack2_Stage2`'s spinning slash is the standing candidate), Release Window notify placed by
  a human, drift warning verifying placement at runtime.
- Delete the two dead notify entries off our `Dagger_Rise1_V2` copy (vendor `BP_AddWeapon` /
  `BP_RemoveWeapon` residue — one `LoadErrors` pair per fresh load until done).
- First preview of each migrated clip doubles as the compat-link play check; pick the
  SwordShield `SKM_Manny` as preview mesh.

## Sub-slice I — docs routing at ship

Spec: the Stun & Knockdown section rewritten from the entries (anatomy, grades, options,
invincibility boundary, forced facing, the exhausted carve-out, the airborne rule); the parry
section gains the lockout model and loses the floor; the hitstun/blockstun deferral clauses
resolve. Tuning map rows: the jail/choice/rise dials; the forced-facing rate (derived);
`KnockdownSpacingCm` vs `MaxReachCm` coupling; the `HitstunSeconds` repurposing on graded
swings; the kip-up's root-motion exception; the carry-axis rule (centres vs radiates, never
unified); the parry window's floor replaced by the ceiling-only rule with the first-contact
guarantee noted. Traps: "Before Stun" discharged (sub-slice B, in-commit); the two owed traps
filed; `s4-360`'s exclusion note deleted and the matrix updated with the `s6` rows. Retired
names: the ride-your-own-recovery reward model → the parry lockout; `State.ParryLockout`'s
reservation resolves. CLAUDE.md: strike Knockdown from the roster, route consequences, delete
this file. Interplay brief inherits: floor vulnerability, regen-paused-while-down,
parry-as-get-up, the hard-grade experiment and each grade's split dial, the meaty loop, the
carve-out's generosity, the block get-up's latched aim (camera-tracked rise as fallback) — and one for the DKO verdict itself: the clean-hit tier choice is now **damage versus grade**
(the light string's 45 + normal, the heavy's 25 + hard, the charged's 40 + hard, the
hold-conversion's escapable 55 + hard against the ender's guaranteed 45 + normal) — whether
those trades price correctly in human hands is the bet being judged. **Graduation candidate for the spec's laws**: *"lockouts and recoveries are there,
but they're all as minimal as they possibly can be, in every single instance."* Netcode brief:
every knockdown window is ≥ 500 ms and dwarfs the round trip; the ninth and tenth replicated
members follow the established pattern.

---

## Order, and what is deferred

**A → B → C (green) → D → E → F, with G's scenarios landing inside the sub-slice that makes each
assertable, H's human steps interleaved where named, I at ship.** A and B are independently
verifiable and commit alone; E is behavior-preserving by derivation and re-bands `s5-parry` in
its own commit. C is the heavyweight sitting — the state machine plus the whole CDO session.
Estimated four to five sittings.

Deferred with owners: floor vulnerability, down-regen, parry get-up, the hard-grade verdict, the
meaty loop, the carve-out's generosity (**Interplay**); all knockdown presentation beyond the
chosen clips — directional falls, impact weight, the airborne fall's look, the parried
attacker's recoil (**Polish**); knockdown 1vX and the airborne fixture (**the two owed traps**);
Human-pending: the six montage steps in H, the Dagger notify cleanup, and the
execution greenlight itself.
