# Plan — Parry (and the ladder re-pole it forced)

**Work-in-flight, deleted on delivery, as this file's own header requires.** The *why* behind every
choice here is the three dated 2026-08-18 entries in `Docs/Combat-Decisions.md`; this file carries
only the *how*. Design direction is ruled; **execution awaits a greenlight at the session that
picks this up.** Implementation deliberately did not occur in the planning session.

**The first-attempt doctrine applies** (the designer, at greenlight of this plan's discussion): the
prototype exists to be wrong at times — hypothesize, observe, adjust. Numbers below are first
attempts unless marked derived.

---

## Numbers

| Value | Current | Plan | Basis |
|---|---|---|---|
| Heavy `ReleaseAtSeconds` | 0.50 | **0.35** | rapid heavy — kills the reaction-dodge answer (entry) |
| Charged decision boundary (heavy `HoldUntilSeconds`) | 0.45 | **0.30** | must sit below the heavy's arrival |
| Heavy `BlockstunSeconds` | 0.50 | **0.60** | **basis change**: recovery (0.50) + plus-on-block advantage (0.10, feel) |
| Heavy authored total (elapsed band) | 1.150 | **1.000** | 0.35 + 0.15 + 0.50 |
| Light, charged: all values | — | unchanged | 200/750 arrivals, boundaries 150/—, recoveries 0.60/0.60 |
| `ParryWindowSeconds` | (spec: 0.40) | **0.30** | ruled; ceiling < fast↔charged gap 0.40; floor ≥ longest `ReleaseSeconds` 0.15 — **both are tuning-map invariants** |
| `ParryWhiffLockoutSeconds` | (spec: 1.00) | **0.60** | floor constraint: a fast-timed whiff (press ≥ ~150) must stay locked through the charged's 750 |
| `PostDodgeParryLockoutSeconds` | — | **0.15** | derived: dodge-end + gap + window must overshoot 750 for the worst predictive dodge |
| Parry success stamina reward | — | **+25**, and the regen pause clears instantly | ruled |
| Parry activation cost | — | **0** | ruled — the pricing symmetry: dodge stamina-priced, block both, parry time-priced |
| Parry success offensive lock | (spec: 0.50) | **deleted** | subsumed: the attacker's own recovery is the lock, per-tier |
| Attacker movement return on clean hit | end of ability | **contact + that swing's `HitstunSeconds`** | derived — earlier erodes the authored spacing; later is dead freedom |
| `TurnRateDegrees` | 1200 | unchanged | derived from the light's `HoldUntilSeconds`, which does not move |

Untouched by design: the light tier entirely, the string cadence and its derivations, dodge, block.

---

## Sub-slice A — the ladder re-pole

CDO edits on `GA_Attack`'s `Branches` (details panel, per the staleness table): heavy `ReleaseAt`
0.35, heavy `HoldUntil` 0.30, heavy `BlockstunSeconds` 0.60. **Verify:** `s1-heavy` bands move to
350 ±30 / elapsed 1.000; `s2-heavy` blockstun span 0.600; full matrix re-run — **bands move in the
same package as the values, authored truth moving, never a checker patched green.** Watch the
`LogTDCombatTiming` warning family for the coil squeeze on the shortened heavy (escalation at 0.30
against `ReleaseStartSeconds`); trust the warnings over arithmetic here.

## Sub-slice B — GA_Parry core

- `UTDParryAbility` (C++, `Source/TheDream/Combat/Abilities/`) + `GA_Parry` Blueprint; granted via
  `DefaultAbilities`, bound via a new `IA_Parry` row in `AbilityInputActions` (MB4;
  **the binding trap applies — read the IA asset and the C++ binding together**).
- **The window is mechanical** — 300 ms from activation, timestamp checked in Tick (never
  `SetTimer`, per the netcode rule). No notify; the montage is purely visual. 360°, no facing test.
- **Resolution hooks the existing hit path** (`HandleTraceHit`): if the struck target's parry
  window is open — parried. Negate damage, stamina damage, knockback, hitstun. Add the *target* to
  `ActorsHitThisWindow` (remaining release inert by existing dedup — confirmed in `ResolveHits`).
  The lunge stop fires as on a hit (the attacker is planted). Set `bParried` on the attack —
  `IsChainOutOpen()` returns false for that swing (**a parried attack cannot chain**). Attacker
  rides full recovery.
- Parrier on success: +25 stamina (server, clamped by the existing three-layer clamp),
  `RegenSuppressedUntil` cleared, free instantly (no success recovery — "retrigger without
  impeding" survives).
- Whiff: window closes, `State.ParryLockout` for `ParryWhiffLockoutSeconds` — refuses defensive
  activations via the shared base, same tag pattern as the other ability-applied states.
- Gates: `ShouldBufferFailedInput = false` (**a replayed parry is a mistimed parry**);
  `ActivationBlockedTags` includes `State.Blocking` (can't parry while blocking — the guard's
  property), the base's dead/exhausted/guard-break/hitstun refusals; `bBlockedWhileAirborne =
  true`; **not** `State.Blockstun` (blockstun and parry never know about each other). Post-dodge
  gap: timestamp on the character (dodge end + 0.15), checked in `CanActivateAbility`.
- Tags: `State.Parrying`, `State.ParryLockout` — native, beside the existing family.

## Sub-slice C — the on-hit waiver

On a clean hit (server, in the hit path after immunity and parry checks): clear
`State.Attacking.Committed` — defensive activations open instantly; facing lock and movement lock
are separate flags and unaffected. Movement unlock at contact + the swing's `HitstunSeconds`
(timestamp in Tick). Offense untouched (chain rules govern). **Netcode trap to file on landing:**
the waiver's trigger is server-only knowledge (a hit), so a client cannot locally predict the
freed defensive input — same loose-tag family as the aim-assist asymmetry; single-player correct
now, filed against Netcode.

## Sub-slice D — fixture, scenarios, trace

**Loop coverage is satisfied in-package; no deferral trap owed.**

- Fixture: `ETDDebugDefendMode` gains `PeriodicParry` (`DebugParryIntervalSeconds` default **1.7**,
  non-aliasing against the attacker's 3.0 — the phase sweep produces both successes and whiffs, the
  dodge-fixture precedent). Attacker gains `bDebugDodgeAfterHit` (presses dodge on its first
  `DAMAGED` of each attack) for the waiver scenario.
- Trace: `PARRY WINDOW` open/close, `PARRY SUCCESS` (attacker, swing, stamina after), `PARRY
  WHIFF`/`LOCKOUT`, `WAIVER` (committed-tag cleared on hit), `MOVE UNLOCK`.
- Scenarios (bands from CDOs at build time, never from this plan — the s4 lesson):
  - `s5-parry` — every `PARRY SUCCESS` pairs with +25 on the stamina ledger, zero `DAMAGED` for
    that swing, **zero `STRING` continuation after a parried swing**, attacker `ABILITY END`
    elapsed at full authored total.
  - `s5-parry-whiff` — `REFUSED` naming `State.ParryLockout` inside the lockout span; span band
    0.60 ±25 ms.
  - `s5-cancel` — attack press → block inside 150 ms: no `RELEASE BEGIN`, `BLOCK cost` exactly
    once, zero damage dealt.
  - `s5-waiver` — attacker's dodge fires between its `DAMAGED` and its recovery's end.
  - Make each new assertion fail once on purpose before trusting it; `--self-test` first.

## Sub-slice E — animation (human steps, interleaved)

- Human creates `AM_Parry` **from the clip** (`AS_SwordSwordAnimV3_Block1_Parry_RM`) — montage-
  from-clip so the skeleton is right by construction; **never by duplicating an attack montage**
  (inherited-notify trap). No notifies needed — the window is mechanical.
- The clip is `_RM`: set the pair — `bEnableRootMotion = false` **and** `bForceRootLock = true` —
  parry authors no displacement (setting only the first is worse than setting neither).
- Success and whiff read the same clip for now; the parried *attacker's* recoil tell is Polish's
  hit-reaction family. **Parry ships felt-not-seen, exactly as blockstun did.**

## Sub-slice F — docs routing at ship

CLAUDE.md: ladder table (heavy 350), feel-goals line amendment (only the charged holds the
reactable pole — the designer's ruling, quote the entry), vocabulary gains **initiative** and
**flinch**. Combat-Spec: parry section rewritten from the entries; heavy section (rapid, plus on
block); stamina section (+25 on parry success). Tuning map rows: window floor/ceiling invariants,
whiff-lockout floor, post-dodge gap, movement-return derivation, heavy blockstun's new basis,
charged ≥ coil + reaction + dodge. Netcode brief: the waiver visibility trap, the cancel-vs-commit
race at the 150 boundary joins the tempo measurement's PktLag probes, the parry window named beside
i-frames in the lag-compensation ledger.

---

## Order, and what is deferred

**A → B → C → D (green) → E → F.** Estimated two to three sittings; A is independently
verifiable and commits alone. **Raise at the greenlight, un-ruled at planning:** whether B–F
should gate on A being *felt*, not merely green — the retune touches shipped, played systems, and
the rest of the evening's design is otherwise unplayed. Human-pending: the montage, a `Block1_Parry` preview, and the
execution greenlight itself.

Deferred with owners: recoil tell and all parry presentation (Polish); ranged redirect (out of
scope with ranged); per-branch parry rewards (contingent — only if play demands read-difficulty
compensation); waiver client prediction (Netcode); chain-to-defense verdict (Interplay, watch
filed); 1vX exercise of the waiver (no fixture can produce a second attacker today — noted, not
trapped, since the rule's 1v1 behaviour is fully covered by `s5-waiver`).
