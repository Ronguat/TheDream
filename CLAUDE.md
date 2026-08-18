# The Dream – Combat Prototype

## Project Intent
Build a high-precision **PvP** combat prototype in Unreal Engine that prioritizes spacing, reactability windows, stamina as a real resource, and clear punish opportunities.

**PvP is the destination, and it is a requirement rather than a later phase.** A combat prototype that cannot be played against another person does not answer the question it exists to ask — every feel goal below is about what two players do to each other. That does not mean networking each slice as it is built; it means no slice may be built in a way that has to be torn up to network it. See **Building for the network**, below.

Feel goals (in priority order):
- Precise spacing and whiff punish
- Unreactable-but-risky light offense vs reactable-but-rewarding heavy/charged options
- High-agency defense (block, dodge, parry) with meaningful costs
- Fair, readable knockdown / oki
- Strong melee identity first; ranged and hybrid come later

## Current Prototype Scope
**In scope right now**
- Single melee weapon archetype with the full 3-point offense (Light / Heavy / Charged Heavy)
- 3-point defense (Block, Dodge, Parry)
- Stamina (100 max, costs, regen rules, exhaustion)
- Basic hitstun, blockstun, and knockdown + get-up options
- One player character + one simple AI or dummy enemy for testing

**Explicitly out of scope for now**
- Multiple weapons / weapon swapping
- Ranged and Hybrid archetypes
- Armor classes
- Abilities / specials
- Full frame-data tuning pass (placeholder numbers are fine)
- **Multiplayer session UX** — lobbies, matchmaking, reconnect handling. *A real network test pass
  left this list on 2026-08-15: **Netcode** holds a roster position ahead of Interplay; see below.*

## Building for the network

**Networkability is a property of every slice, not a later phase** (2026-08-11, replacing an
earlier call that put netcode flatly out of scope). What stays out is session UX — lobbies,
matchmaking, reconnect handling. *Running* multiplayer stopped being deferred on 2026-08-15:
**Netcode** is a roster item, and the test pass against latency is its opening sub-slice.

The model is **server-authoritative with client prediction**, the one GAS is designed around.
Three rules bind all new work:

- **New state is a replicated property or an attribute, never a loose gameplay tag.** Loose tags
  do not replicate. Follow `bDead` / `bExhausted` on `ATDCombatCharacter`: the server decides,
  the bool replicates, `OnRep` applies the tag locally. **Decide on the server, apply everywhere.**
- **Authority-sensitive work is explicitly gated.** Damage, attribute writes and hit detection
  belong to the server. State that only the local machine can know — input, buffered presses,
  camera-relative facing — deliberately does not.
- **Latency comes out of the reactability budget, so it is a design input.** The tightest window
  in the game is the **light's 200 ms**, measured from the montage starting on the press — not
  the heavy's coil, which is 350 ms. A network does not get to spend that budget silently. When a
  timing is chosen, say what it looks like with a round trip in it.

**The ASC lives on `ATDPlayerState` for players** (2026-08-11). The training dummy has **no
PlayerState**, so `ATDCombatCharacter` *resolves* which ASC it uses rather than assuming one —
never reach past `AbilitySystem` to the owned fallback. *(This said "unpossessed placed pawn" until
2026-08-14 and the possession half was wrong: the dummy is `AutoPossessAI: PlacedInWorld` under a
stock `AAIController`. Having no PlayerState is what the resolution depends on, and that is intact.)*

**Still outstanding:** prediction windows, lag compensation for i-frames, client-side stamina
prediction, and **2** network-unaware `SetTimer` sites — the charged attack's checkpoint and the
dodge's duration. Those two are the same problem twice: the dodge timer *is* the i-frame lag
compensation. (Recounted 2026-08-11 from 14; the breakdown is in `Docs/Combat-Decisions.md`,
along with the audit and the reasoning behind the model.)

**Two machines have run once** (2026-08-15, V2 recon — observational, no client input). They connect,
replicate and stay up, and all four replicated bools reach the client. **Everything else above is
still structure rather than behaviour**: nothing has run under latency or with a client acting, and
**`OnRep_PlayerState` is still unverified** — observable since 2026-08-15's `ASC RESOLVE` line; the next two-machine run settles it. See the decision log.

**On commitment level — restated 2026-08-15, superseding the 2026-08-11 stretch-goal framing:**
*building* networkably is non-negotiable and binds every slice, as above. *Actually networking the
game* is now a **prerequisite for the feel verdict, not a stretch goal**: with one local human, the
second player is remote by definition, so **Netcode precedes Interplay** and verified-good cannot
exist without it — the feel pass measures the game that ships, latency included. What survives:
netcode difficulty must never be a reason to compromise combat feel, and readiness work must not
crowd out the feel work it protects. The failure case is front-loaded rather than fallen back on —
**Netcode opens with the kill-question** (see Remaining). Reasoning and the fallback's new meaning:
`Docs/Combat-Decisions.md`, 2026-08-15.

## Core Combat Rules (must respect)

### Combat Vocabulary
Attack phases, used consistently in code, comments and discussion:
- **Windup** — everything before the attack can deal damage.
- **Release** — the period during which the attack deals damage. Marked on a montage by the `Release Window` notify state (`UAnimNotifyState_MeleeWindow`).
- **Recovery** — from the end of the damaging phase to the end of the attack.
- **Coil** — *not* a fourth phase. It is a sub-state of windup: the portion slowed while waiting for the commit checkpoint, and it exists as visual feedback. Its tuning values are named `Coil*` rather than after a phase.

Note that "release" also names the button coming up, via GAS's `InputReleased`. Bare "release" always means the damaging phase; the button edge is always written as *input release*.

**Attack and swing mean the same thing** (the user, 2026-08-18). A chained light is **three attacks**, not one attack in three parts — `FTDStringSwing` and the trace's `swing=N` are that index. A **string** is the chain they form. A **burst** is the debug fixture's firing cycle, which produces a string when `DebugAutoAttackStringTaps` > 1 and a single attack at its default 1.

### Offense (Melee)
**An attack is defined by when it hits, not by how it plays.** Each tier authors the moment its hitbox goes live and the input boundary you must release before to get it. Every play rate is derived from those at runtime. Two numbers per tier:

| | Release before | Hitbox live |
|---|---|---|
| Light | 150 ms | **200 ms** |
| Heavy | 450 ms | **500 ms** |
| Charged Heavy | (held past 450 ms) | **750 ms** |

**Space is authored the same way, as of 2026-08-12.** Each tier also authors its damaging volume — reach, arc and a vertical band — as an `FTDAttackHitbox` on its branch, so range is a designed number rather than a property of the clip. `GA_Attack`'s `Branches` array is authoritative for both.

**Displacement too, as of 2026-08-12** — two authored distances in centimetres, not a scale on the clip. A shared **base lunge** from the press to the light's input boundary, then a **per-branch lunge** from the commit checkpoint to the end of the release window. The coil carries neither, which is what keeps the tiers indistinguishable for as long as they must be. `GA_Attack` is authoritative; the reasoning is in `Docs/Combat-Decisions.md`.

**A lunge is shortened two different ways, and they are not the same mechanism** (the stop added 2026-08-14):
- **The standoff gate *pauses* it** while a body sits ahead, per movement tick, and travel resumes if that body leaves. Geometric, so it never needs to know who the target is.
- **A hit against a viable target *stops* it outright**, permanently, for that activation. Needed because a pause cannot survive the target ceasing to exist: killing someone removes their capsule, the gate opens on the corpse, and the attacker slides through. Geometry does not stop a lunge, and neither does a target who i-framed it — **a dodged attack runs on**, which is what stops a successful evade from paying the attacker in spacing.

Both only ever subtract, so the authored distance stays a hard ceiling and neither is homing.

**Aim assist may correct where you are pointed, never whether you were in range** (graduated from Target Lock 2026-08-13). It cannot rescue a spacing miss, so whiff punish is untouched, and **it aims the *lunge*, not the swing** — the damage wedge already carries ±36–50° of tolerance while travel is a line, so a 25° error puts you 84 cm to the side. The user's framing: *a margin of error for aiming the lunge, not for aiming the mouse.*
- **The wedge's arc is the knob and means how wrong your aim may be; its reach is derived** — base lunge + branch lunge + damage reach + `AimAssistMarginCm`, the one authored number. **The wedge must reach past hit range, and that gap is the design rather than slack**, or lock-on becomes a rangefinder. `bEnabled` turns a branch off; an arc of 0 does not. **The arc is a learnable constant** and stays static across the ladder *and* across string swings (2026-08-16) — an intuition for how much error is forgiven is only worth building if the answer does not change per attack.
- **Homing follows the ladder, runs through the base lunge, and stops at commit**, so it never leaks a tier the defender has not been told about, and stopping at commit is where the reaction window opens.
- **Evaluated in the camera's frame** while damage stays actor-framed — defenders must be able to trust what the body does. **The player's authority is selection, not facing**, and two rules writing one yaw would deadlock. **No hysteresis yet**, so expect selection flicker as the wedge grows.
- **AI focus does not drive an attack's target** (measured 2026-08-18): the aim-assist selection is an independent system, and setting an AI's focus does not steer its swing. Load-bearing for **Combat AI**; see the trap.

**An attack owns your movement and your feet** (2026-08-12, from play — this was assumed for months and written down nowhere):
- **Movement input is suppressed for the whole ability** — windup, release *and* recovery. WASD and jump do nothing; the attack's own lunge still moves you. You cannot walk out of your own commitment. Implemented as `UTDGameplayAbility::bLocksMovement`, a checkbox on the shared base, so block, parry or a future crouch adopt it the same way.
- **Attacks cannot start while airborne**, via the existing `bBlockedWhileAirborne`. It gates *activation*, not continuation — an attack that starts grounded keeps running if its lunge carries you off a ledge, which is deliberate. **The refusal is not buffered**: an attack pressed in the air is dropped, not replayed on landing.
- Air attacks are therefore out for now and are a checkbox away from being back in.

**All animations are camera-facing: the character strafes and never turns to face its movement direction** (the user, 2026-08-15). Any clip that reads as turn-to-face is a **vendor limitation being worked around, not behaviour to preserve** — worth stating because snappy direction changes look like they want turn-to-face as the fix, and that is backwards. It is also why a blendspace's direction axis wants **weight** smoothing (`targetWeightInterpolationSpeedPerSec`) rather than input smoothing: strafing crossfades between directions rather than sweeping the parameter through forward.

**Facing is committed at the commit checkpoint and runs at three rates** (graduated from Attack Swap 2026-08-14):
- **Facing freezes from commit to the end of the ability, recovery included**, instantly in both directions — an actor-frame damage volume needs a stable actor frame. Steering stays free through windup, which keeps the whole cancellable portion of an attack steerable, and means **`RecoverySeconds` sets commitment length as well as punish length**. Whoever takes facing away restores it in `EndAbility`, where every exit path converges, never on the montage delegates.
- **The three rates split by whether a number may be tuned by feel.** `TurnRateDegrees` is **derived, not chosen** — 180° ÷ the light's `HoldUntilSeconds` — and must move when that does, or attacks silently point where the turn got to. The other two are free, because the aim guarantee is discharged before either applies: `IdleTurnRateDegrees` when `IsIdle()`, and `CoilTurnRateDegrees` while an attack coils, which only heavy and charged reach. `BP_PlayerCharacter`'s CDO is authoritative for all three.
- **`bAllowPhysicsRotationDuringAnimRootMotion` is `true`**, against a UE default of off, and is the **only** rotation path — disabling it freezes facing everywhere rather than just at rest. Anything wanting a committed direction says so via `SetAbilityFacingLocked`. **Never re-disable it to fix one ability.**

**Input is buffered** (graduated from Input Buffer 2026-08-14): a single slot, last press wins, and the window is grace on *taps* — a held button never expires, which is what makes a buffered heavy or charged reachable at all. The airborne-attack and airborne-dodge refusals deliberately do **not** buffer. `BP_PlayerCharacter`'s CDO holds `InputBufferSeconds`.

Two rules the model depends on:
- **Windup length is preset.** Releasing early inside a band changes nothing — the attack still takes its full time to arrive. The cost is real dead time, and it is what stops a fractionally-held heavy from dominating light.
- **Reactability is measured from the tell, not from the press.** All tiers share one windup, so the defender's window is coil → damaging. Lengthening a windup does not by itself make an attack more reactable; moving the coil earlier does.

- **Light**: released before 150 ms, hits at 200 ms. 2–4 hit string (weapon dependent) — **shipped at three hits 2026-08-18**; the knockdown terminator is Knockdown's. **No light is truly safe** (2026-08-16, superseding *"first hit safe on block; subsequent hits are not"*): recovery is authored long and only chaining skips it, so on whiff **and** on block the real cover is the defender hesitating against the next hit — the delay-and-bait layer above that is the design, **specified 2026-08-16 and verified against the authored values** (finishing is punishable by 350 ms, stopping early by 200, and an immediate chain beats an eager punish by 50): see `Docs/Combat-Decisions.md`. **Every non-final hit carries the target to one authored spacing** in front of the attacker, identical every time; a blocked hit is centred exactly the same but concedes notably less ground. Any hit in the string guarantees the rest — **coupled to the ban on heavy→light; they are one decision, see `Docs/Combat-Decisions.md`**. Last hit knocks down but has heavy endlag. Minimal stamina damage. It never *coils*, so it carries no tell that distinguishes it from a heavy — but **it is not unreactable**, which this file claimed until 2026-08-11: the montage starts on the press, so the windup is a tell from frame one. **250 ms was reactable and the light moved to 200 ms because of it** (2026-08-12); the 150 ms boundary is the measured floor for trivially consistent inputs, not a guess. Whether 200 ms is far enough is itself unverified — it has never been played against a human.
- **Heavy**: held past 150 ms, hits at 500 ms. Single hit. Safe on block, punishable on whiff. Knocks down. Higher range, moderate stamina damage. *Currently **350 ms** coil → damaging, more reactable than intended; deferred until the ladder is tuned as a whole. **It got worse when the light got faster**, and that coupling is easy to miss: the coil begins where the light stops being available, so moving that boundary 200 → 150 ms widened the heavy's tell window by the same 50 ms. Any future change to the light's boundary moves the heavy's reactability with it.*
- **Charged Heavy**: held past 450 ms, hits at 750 ms. Single hit. Breaks block, heavy endlag, knocks down. Highest range. Very reactable.
- Any light in a chain can be held to convert into a heavy.
- Some heavies can chain into further heavies; never into lights.

Timings land within about a frame, biased late. `GA_Attack`'s `Branches` array is authoritative for live values; the reasoning behind the model is in `Docs/Combat-Decisions.md`.

### Defense
- **Any defensive action can cancel an attack's startup** — block, dodge or parry, not block alone. The boundary is the attack's commit checkpoint, marked by `State.Attacking.Committed`: cancel before it, never after. Defensive abilities block on that tag rather than on `State.Attacking`.
- **Block** (hold RMB): 180° forward, measured in the defender's frame when the hit resolves. Movement stays free but slowed to `BlockingMaxWalkSpeed` — a guard is a stance you carry, not a place you stand. Ends on becoming airborne by any means, and cannot be raised there.
   - **Two stamina mechanisms, and only one can break a guard** (2026-08-14). *Drain* is self-inflicted by holding: it runs the bar to 0 and parks it there harmlessly, so holding converts into risk rather than running a countdown. *Damage* is what an attacker inflicts on the guard. There is also an authored one-off `BlockInitialStaminaCost` for raising one, **paid and never required** — raise a guard you cannot afford and it works, cancels what it would have cancelled, and exhausts you. That is not a break: nothing breaks and there is no stun.
   - **A guard breaks exactly when a blocked hit leaves the defender at 0.** One rule covering both damage exceeding what is left and damage landing on an already-empty bar, which is what makes holding at 0 costly rather than free. No special case for the charged: it breaks a *full* guard because its stamina damage equals the whole bar — **change either number and that silently stops being true.** A break is `GuardBreakStunSeconds` of stun refusing every ability, regen suppressed across it, then the ordinary pause, then exhausted regen.
   - **A guard is committed for `MinimumBlockSeconds` and nothing but movement is allowed out of it** — `State.Blocking.Committed`, deliberately parallel to `State.Attacking.Committed`. It exists because a guard with no floor can be feathered at input speed, which read as unfinished in play. It **must** gate the attack or it does nothing, so it narrows *whichever comes last wins*: that still holds between block, dodge and attack, except inside this window. Releasing inside it is remembered, not discarded. Responsiveness comes from the buffer, which fires the refused attack the instant the window ends.
   - **A resume is an intended block, and all blocks are created equal** (the user, 2026-08-14). A guard the system puts back up because the button is still held pays the full cost and takes the full commitment. An exemption would make both conditional on something the player cannot see.
   - **An exhausted guard ends the instant its commitment expires**, held button or not (2026-08-14). Derived rather than added: you cannot block while exhausted, and all blocks are created equal — so a guard raised too poor to pay for itself still owes the full `MinimumBlockSeconds`, and the commitment is then the only thing holding it up. Cancelled, not released; a release would be the player's. It comes back when exhaustion lifts if the button is still down.
   - **Buffer actions, not states.** Block never replays a refused press: a stale request to enter a state is meaningless, since the button either is or is not down now. Attacks *do* buffer through the guard's commitment, and that asymmetry is what keeps a swing thrown during a block responsive.
   - `BP_PlayerCharacter`'s CDO holds the drain, initial cost, minimum, stun and blocking speed; `GA_Attack`'s branches hold the per-tier stamina damage.
- **Dodge** (LShift): Directional (or back if stationary). Costs 50 stamina. Grants i-frames for the duration. **Not available while airborne** — keyed to the falling state, so it covers walking off a ledge too, unlike the jump's regen pause which keys on the action.
- **Parry** (MB4 or LAlt+RMB): 400 ms active window, 360° coverage. Success = no stamina drain, no blockstun, and (vs melee) 500 ms offensive lock on attacker. Success vs ranged redirects to crosshair. Whiff = 1000 ms defensive lockout. Successful parries can retrigger without impeding other actions.

### Stun & Knockdown
- Blockstun: **Disables offense and nothing else**, for a duration the *attack* authors (`BlockstunSeconds` per branch on `GA_Attack`). **Defense is deliberately untouched** — movement, dodging, the guard itself and, when it exists, **parry**, which means **blockstun and parry never know about each other** *(the user, 2026-08-15, correcting a line that read "offense + parry" from this file's beginning; the implementation never matched it, `GA_Attack` being the only ability that carries `State.Blockstun`)*. Taking a defender's guard for blocking correctly would invert the mechanic. **A guard break supersedes it rather than stacking**: a broken guard is not a successful block. Shipped 2026-08-14 at each tier's own `RecoverySeconds`, 50 ms the safe side of neutral. **The light's stopped being derived that way on 2026-08-16**: once a chain existed, its own recovery was measuring against the wrong threat, so its 0.35 is derived against the **chain cadence** instead — heavy and charged keep the recovery basis. Both derivations are tuning-map rows, and the light's is not free. **The charged's can never fire** — its stamina damage empties any bar, so it always breaks instead; filed as a trap.
- Hitstun: authored per attack (`HitstunSeconds` on `GA_Attack`'s branches and swings; the light's is **0.55**), and it **refuses every ability, defense included** — *that refusal is what makes "any hit guarantees the rest" true*, and it is why hitstun is a Light String mechanic rather than a Knockdown one. **It must outlast the chain gap or the guarantee silently stops holding**, which is why the cadence and this move together. Movement stays free; the lock is Knockdown's, deferred beside the guard break's.
- Knockdown: 1.5 s default get-up. Early get-up via Dodge, Block, or Attack. Get-up attack knocks back, short recovery, very punishable on block/whiff. **A charged's knockdown is hard — fewer get-up options** (2026-08-16); the grades are Knockdown's to build.

### Stamina
- Max 100.
- Dodge = 50.
- Blocking drains based on attack + blocking weapon.
- 0 stamina → Exhausted (no defensive actions or jump) **until stamina refills to 100**, not for a fixed duration. **Exhaustion also slows you**, to `ExhaustedMaxWalkSpeed` — 400 against a normal 500, authored 2026-08-14 so the state reads in the body rather than only as a refusal. It combines with the guard's cap by **taking the slower**, which is reachable because a guard raised too poor to pay for itself leaves you exhausted with the guard up. Stamina floors at 0, so there is no overspending and every exhaustion is identical — dodging at 3 and dodging at 50 both land on exactly 0. Re-emptying the bar the moment you recover is allowed. **Regen continues while exhausted** — it locks out acting, not recovering, and is the only thing that can end it. **The regen pause still applies, though** (2026-08-14, from play): a player may hold block at 0 and suppress their own recovery indefinitely, which is a choice with an obvious exit rather than a trap. Reasoning in `Docs/Combat-Decisions.md`.
- **Regen runs at two rates**, `StaminaRegenPerSecond` normally and `ExhaustedStaminaRegenPerSecond` while exhausted; the character Blueprints' CDOs are authoritative, with defaults in `ATDCombatCharacter`. *(Both rates were confirmed to reach the CDOs unshadowed on 2026-08-14. `StaminaRegenPauseSeconds` beside them is overridden by **both** character Blueprints, which is why the distinction gets stated — they disagreed until 2026-08-15, when the dummy was mirrored back onto the player's value.)* Exhaustion is the slower of the two, so being run dry costs more than the bar it emptied. **The exhausted rate may never be zero** — regen is the only thing that ends exhaustion, so zero means permanent. Paused during any action carrying `State.StaminaRegenPaused` and for 0.5 s after, measured from when the action ends (`StaminaRegenPauseSeconds`), **exhausted or not** — the pause is a cost of acting and exhaustion is not a refund. **`GA_Dodge`, `GA_Block` and `GA_Attack` carry it**, attacks as of 2026-08-14, so a swing is taxed for its whole windup, release and recovery plus the tail. The tail is **shared, not per ability** — that split was specified and declined the same day as authoring overhead for a distinction nobody has felt; play asking for it is the trigger.
- **Jumping costs no stamina but pauses regen** — from the jump until 0.5 s after landing. Keyed to the jump *action*, never to being airborne: walking off a ledge costs nothing.
- **Costs are paid, not required.** No action is ever refused for want of stamina: dodging at 30 works, empties the bar and exhausts you. Never use GAS's `CostGameplayEffectClass`, which gates activation, and do not call `CommitAbility` — checking a cost is the gate. Costs are applied via `UTDGameplayAbility::EffectOnStart`.
- Regen, the pause and exhaustion are orchestrated in C++ on `ATDCombatCharacter`, not by GameplayEffects — see `Docs/Combat-Decisions.md`.

## Technical Preferences
- Prefer **C++** for core systems, characters, AttributeSets, Ability base classes, and any non-trivial logic. Keep the architecture clean and maintainable in code.
- Expose all important tuning values (timings, costs, magnitudes, windows, etc.) to Blueprint via UPROPERTY so the designer can adjust them without recompiling.
- Gameplay Abilities, Gameplay Effects, and animation notify logic can live in Blueprint when that makes iteration faster, but the underlying framework and shared logic should be C++.
- **Gameplay Ability System (GAS) is preferred** for attacks, block, dodge, parry, stamina, hitstun, blockstun, and knockdown. Keep AttributeSets and GameplayEffects clean and data-driven.
- Favor clarity and tunability over clever architecture.

## Implementation Conventions
- **`TheDream` is the project codename, not the title.** It appears in exactly three places: the C++ module (`/Script/TheDream.*`), the content root `/Game/TheDream/`, and the `TD` class prefix. Treat all three as permanent and arbitrary — renaming a module means every Blueprint's stored class path breaks, paid for with `ActiveClassRedirects` cruft that never goes away (there are already five such lines from the `TP_ThirdPerson` rename).
- **The shipping title lives only in `ProjectName` (`Config/DefaultGame.ini`) and localized strings** — never in code, asset names, folder paths, or gameplay tags. Keeping it out of those places is what makes retitling a one-line change instead of a migration.
- **Ownership rule:** everything authored for this project lives under `/Game/TheDream/`. Anything at `/Game/` root is Epic template or third-party content. Combat content therefore lives under `/Game/TheDream/Combat/` (`Abilities/`, `Effects/`, `Animations/`, `Input/`, `Characters/`, `Data/`).
- C++ mirrors this: `Source/TheDream/Core/` (game mode, player controller, base character) and `Source/TheDream/Combat/` (`Abilities/`, `Attributes/`, `Tasks/`, `Notifies/`). Includes are written relative to the module root, e.g. `#include "Combat/Attributes/TDAttributeSet.h"`.
- Never duplicate a World Partition level to make a new map — the external actor packages don't re-path and actors silently go missing. Use File → New Level → Empty.
- Name assets clearly: `GA_Attack`, `GE_StaminaCost_Dodge`, `ABP_Combat`, etc.
- Use data-driven values (curves, data assets, or simple constants) for timings, stamina costs, and windows so they can be tuned without code changes.
- Every new system should be playable in PIE with a debug enemy or training dummy as soon as possible.
  **The dummy mirrors the player's combat values; divergence is a design decision needing an argument**
  (2026-08-15) — a fixture reproduces the conditions under test, so parity is the default rather than
  hand-maintained. Three had silently drifted, and two verification assertions were wrong until mirrored.

## Project Documentation
Five standing files carry knowledge the code cannot (a work-in-flight plan file may sit beside them, and says so in its own header). Read them before working in their area; keep them true in the same commit that makes them wrong. **Each has a trigger rather than being read at large** — that is why they are not in this file, which is loaded in full every session.
- **`Docs/Closing-Down.md`** — the eight-step procedure for ending a session. Trigger: the user says to wind down, and nothing else.
- **`Docs/Debug-Instruments.md`** — this project's own instrumentation: every trace tag, the cvars, the ungated warnings, the attacker and defender fixtures with the configurations that silently invalidate them, the test level's measurable geometry, the regression checker's scenario matrix, and the post-change verification checklist (moved here from `Working-In-Unreal.md` 2026-08-18). Trigger: you are about to measure something in combat. Split from `Working-In-Unreal.md` 2026-08-14 because it was the only part of it that grew — one line per combat feature, forever, which is the correct shape for a doc read by whoever is measuring and the wrong shape for one read every session.- **`Docs/Working-In-Unreal.md`** — how to drive the editor and its MCP toolset without losing work: which writes silently do nothing, when Live Coding is safe versus needing a full editor-closed rebuild, what is not scriptable at all, and the standing regression checks for combat changes.

  **Read it front to back at the start of every session** (2026-08-13, the user's instruction). It is not a reference to reach for when something breaks — nearly everything in it **fails silently**, so it only helps if it is already in your head before you touch the editor. It was cut from 820 lines to ~400 on 2026-08-13 to make that reasonable, and **keeping it readable is now a closedown step**: anything compressible to its rule gets compressed, and the incidents behind them live in git and `Docs/Combat-Decisions.md`. *(Re-trimmed by the 2026-08-15 structure audit after a day parked at its 420 tripwire. Two successive notes here stating a live count both went stale within hours, so this line no longer carries one — `wc -l` is the authority.)*
- **`Docs/Combat-Decisions.md`** — dated log of combat decisions and the reasoning behind them, plus the working sections at the top. **Known traps** are latent defects filed against the slice that trips them; the **tuning map** says which knob to move when a verdict comes back, which obvious-looking knob is wrong, and which values are **derived rather than free** and must be re-derived instead of tuned; the **symbol index** answers *"what was decided about this thing"* for any symbol in the codebase; and the bridge tables cover anything superseded or renamed, **including the item numbers this file stopped using on 2026-08-12**. Append an entry whenever a gameplay choice is made that a future reader could reasonably second-guess; never rewrite an entry to match new code, supersede it with a new one.
- **`Docs/Animation-Library.md`** — where animations come from, the naming convention that makes 5,319 of them searchable, what the library does *not* contain, and how to migrate one in without dragging a duplicate skeleton behind it. Read before asking for or importing any animation.

**Durable knowledge belongs in these files, not in an assistant's per-machine memory.** Anything a future contributor would need — combat reasoning, tooling behaviour, rules and current facts — goes in the repo, where it can be reviewed and corrected. Memory keeps only what is genuinely session- or machine-scoped, and *points* at the repo rather than restating it: `Docs/Working-In-Unreal.md` exists precisely because those notes were once memory-only and therefore invisible.

**This file's budget is ~540 lines, enforced when you add rather than when you audit** (420 on 2026-08-14, raised three times on 2026-08-15; **520 → 540 on 2026-08-16**, the knockback/safety dispensation landing as core-rule material after in-place compression). Past it, compress or relocate first — the person adding a line knows what it replaces. Stated as a number because the vaguer form did not hold: it once went 486 → 466 → **514** in one session, caught by a manual re-read rather than by anything structural. **The number is a tripwire, not a cap** (the user's call): compress or route what grew, and raise it with a dated note when the growth is genuinely rule material with no other home — deleting a live rule to hit a number is the one wrong answer. Three standards came out of the raises so far. **Compress first and mean it.** **Raise with headroom, never to the current count**, or it trips on the next line anyone adds and teaches people to ignore it. And **the criterion is earned-ness, not size** (the user: every line earned, no hypothetical ceiling worth fearing). **Working-rule growth does not shrink back the way Remaining-shaped growth does** — the next raise should ask whether a Working Rules section this size wants its own triggered file.

**Length is a context cost, and the budget applies to what loads *every* session** (the user,
2026-08-15). The always-read files are internalised before any work starts, so verbosity there is
paid in tokens every single time. Trim tails, not heads — a bolded lead clause is what makes a
read-every-session file skimmable.

**The narrow universal budget is what funds generous per-session spending, and that is the point
rather than a consolation.** Read the images, run the extra probe, pull the whole log. **Frugality
*inside* a session is not a virtue here and must never be inferred from the rule above** — the
discipline on what is always loaded exists precisely to buy that freedom.

**One fact, one home; everywhere else points at it.** This applies *inside* the repo, not only between memory and repo — including between two items in this file, and between a doc and a code comment. The 2026-08-12 audit found eight wrong claims and three were a fact stated twice where only one copy was updated; the 2026-08-15 audit found a fourth, this file's own summary of another doc naming a count that had since quadrupled. **Summaries and descriptions are where this happens** — they restate values nobody thinks to update. A second copy does not reinforce a fact, it creates something nobody reviews.

The pattern that works is already in use here — *"`GA_Attack`'s `Branches` array is authoritative for live values"*. **Prefer naming the authority over restating the value**, especially for anything that lives in code. Numbers still belong in prose where they carry an argument, but then they are a *measurement with a date*, not a live value.

**Name the asset, not the C++ class, and this is a correctness rule rather than a style one** (2026-08-14). A Blueprint CDO override shadows a C++ default silently, so a class is the authority only until someone touches a details panel — at least five documented live values disagreed with their headers when this was checked on 2026-08-14, the asset being right in every case. Pointing at `ATDCombatCharacter` therefore lands a reader on a number that is not live. Write *"`BP_PlayerCharacter`'s CDO is authoritative, defaults in `ATDCombatCharacter`"*, which names both and says which wins.

Deliberately **not** kept: per-system design docs. Local rationale belongs in header comments, which are read at the moment the code is; a doc that describes a system drifts out of sync and then gets trusted over the code.

## Working Rules

**Autonomy on the HOW. Interrupt on the WHAT or the WHY.** Stated by the user 2026-08-14, and it is
the principle every rule below is an instance of. Once what to build and why has been agreed, running
the how through to completion is not merely acceptable, it is **preferred** — do not hand steps back
one at a time. But if a genuine question about *what* or *why* emerges mid-run, stop and raise it.

**A design question asked in service of a HOW is welcome, not an interruption** (the user,
2026-08-15). What this rule guards against is handing *decisions* back, not asking what a thing is
for — and combat work does throw up moments where the intuition is the missing input. Ask.

**When something is vague, or two sources disagree, ask. Every single time.** The user's explicit
anti-trap, 2026-08-15, and the boundary the autonomy rule is measured against: this project is
**maximally designer-authored at every step except the HOW**, so resolving an ambiguity quietly
takes a decision that was never yours — including when it looks obvious and turns out right.
**An implementation disagreeing with its spec is a question to ask, not a discrepancy to fix**;
settling one silently that day filed a trap against code that was correct.

**The design runway lives outside the repo, and that is deliberate.** The user maintains living
design documentation that runs well ahead of the build — as far out as ability sets for weapons that
may not survive to exist — and dispenses from it as each thing becomes relevant, so building is
never blocked waiting on design. **Silence in the spec is therefore not a gap and not missing
design**; it almost always means we have not reached that thing yet. Do not invent to fill it, and
**do not try to write the runway down** — a large inert specification would age into a liability
faster than it could be read, which is exactly why it is not in here.

**This is what the ask-every-time rule is *for*, and why it is not merely an error check** (the
user, 2026-08-15). **A question is the cue that now is the moment to commit a design direction and
solidify it** — it pulls a decision out of the runway into the record at the point it becomes real.
So ask early and ask cheaply: the question is the intended input, never a tax on one.

- **The test for which one you are looking at is reversibility.** A HOW decision is one you can undo
  alone; a WHAT decision needs the user to undo it. **Irreversibility therefore converts a HOW into a
  WHAT** — deleting an asset looks like a how ("how do I clean this up") and is not, because only
  they can undo it.
- **The tell, in the moment, is whether you are composing a justification.** If a choice needs
  *defending* in a commit message or a report, it was a WHAT. HOW decisions do not need defending,
  they need doing.
- **Not every WHAT/WHY interrupts.** One that blocks the work does. One that is merely noticed in
  passing goes in the report. If everything routes to an interruption, interruptions stop being
  meaningful — the same reason a permission prompt you always approve is not oversight.
- **Permission prompts are not the mechanism, and must not be used as one.** A prompt only ever asks
  a HOW question, so it cannot gate a WHAT. Rules like *never delete assets or change project
  settings without explicit approval* rest on the classification above, not on a prompt standing
  behind them. **Do not add a prompt to cover a rule.** Reasoning in `Docs/Combat-Decisions.md`.

- **The loop follows from that: objective → *read the traps* → measure → plan → greenlight → execute → report.** Named by the user 2026-08-12,
  after it turned a two-session bug hunt into a measured one-line fix. Given an objective, do all the
  reading and measuring needed to actually understand the problem, **then present a plan and stop.**
  *Measuring comes before planning*, so the plan is built on numbers
  instead of on a guess about what is wrong — a plan proposed before measuring is just the first
  hypothesis wearing a schedule. *The pause is real*: do not begin work that has not been described
  and agreed, however obvious it looks. *Execution after a greenlight is unattended* — drive the
  editor closes, rebuilds, asset writes and verification through to the end. If something mid-execution
  changes what should happen, that is a **new plan** and needs its own greenlight; say so and stop
  rather than quietly widening scope. **This applies to scope as much as to direction**, which is the
  half that is easy to miss — a planned cut that stops short because finishing it would mean deleting
  live rules has changed WHAT is being delivered, however sound the stopping point.
  **Reading the traps is a step, not a hope.** Before measuring, grep `Docs/Combat-Decisions.md`'s
  known-traps section for the name of the item in play and say what it turned up. That section asks to be
  re-read at the start of the slice it is filed against, and on 2026-08-12 that failed exactly as an
  unenforced instruction does: a trap discharged during Attack Swap sat filed for a day and was found
  by an audit rather than by anyone reading it. One grep, at the moment it is most relevant.
- **The regression loop is a living artifact: combat surface and loop coverage stay coupled**
  (2026-08-15, the user's rule). Any package that plans or green-lights a new combat capability must
  explicitly include **one of two things, and there is no third option**:
   1. the scenarios and band checks it will add to `Tools/RegressionCheck/regression-check.sh`,
      **in the same package**; or
   2. a **dated trap** in `Docs/Combat-Decisions.md` recording that coverage is deliberately deferred
      and **naming what is now untested**.

  **Doing neither is a process violation**, not an oversight to be caught later, and it **binds at
  plan time rather than ship time** — which of the two is part of what gets agreed, so it cannot be
  settled by whoever is tired at the end. Why it matters is in `Docs/Debug-Instruments.md`: a loop
  that lags the combat surface still prints green. Binds pending slices as well as future ones —
  Parry, Knockdown, Polish, Death-full and Settings each owe the choice when picked up.
  **Block took the deferral branch twice** on 2026-08-16, both filed as dated traps: animation work
  adds no assertable mechanic, and directional dodge-cancels are invisible to `s3`.
- **Do not declare a task finished on your own.** The loop closes where it opened: greenlight is the
  WHAT gate going in, and *"is this done"* is the WHAT gate coming out. Report what was built, what
  was verified versus merely written, and **what was done beyond what was agreed, or that nothing
  was** — then stop and let the user call it. Added 2026-08-14, and it is deliberately a
  conversational gate rather than a prompt on the push. **This is what makes unattended execution
  safe**, so it is not optional tidiness.
- **Combat and gameplay work is deliberate, not vibed.** Minimize assumptions and state the reasoning, even when it is slower. If a gameplay question has more than one defensible answer, raise it rather than picking one quietly — and record the choice in `Docs/Combat-Decisions.md`. Unprompted initiative is welcome for debug and tooling conveniences (adding a readout to the debug HUD, say); it is not welcome for anything that changes how the game plays.
- **When play and rationale disagree, play wins.** This file and `Docs/Combat-Decisions.md` are full of carefully argued positions. They exist to make choices legible, not to defend them against evidence — a designed distinction that does not survive contact with feel gets dropped, and the entry recording it gets superseded rather than argued for. Do not treat a persuasive past entry as a commitment.
- Always propose a short plan before creating or modifying multiple assets.
- Work in small, verifiable vertical slices. After each slice, stop and wait for feedback.
- Never delete assets or change project settings without explicit approval.
- When implementing an attack or defensive move, include the relevant input binding, montage/notify windows, stamina cost, and at least a basic success/failure outcome.
- **An animation plays in full across the mechanical duration it belongs to.** Fit the clip to the duration; never trim it to hit a number nobody has felt. The fix for a bad-feeling number is to change the number. Reasoning in `Docs/Combat-Decisions.md`.
- After making changes, briefly list the assets created or modified and the key values set.
- **Commit freely; the push waits for the user to call the work done.** Amended 2026-08-14, having previously read *"commit and push… without waiting to be asked."* The split is reversibility: a local commit is a HOW — undoable, and the message is where reasoning gets recorded anyway — so commit in coherent, verified units as the work lands, which also means the user reviews a series rather than a pile of working-tree changes. Pushing is the outward-facing half and follows the completion gate above. The bar for a commit is unchanged: not every file edit, and not a half-finished slice. Pending *tuning* questions do not block a push; pending *correctness* verification does.
- **Every commit you author gets the `Co-Authored-By` trailer, without exception.** A trailer that is present only sometimes makes its absence ambiguous, which is worse than never using one; six commits on 2026-08-09 lost it late in a long session. Do not automate it with a hook — a hook cannot tell who wrote a change, so it would falsely claim the ones you did not.
- **Report what was found, not who found it.** Note a contribution in a clause if it matters to the
  reasoning and move on. Do not tally credit and do not apologise for not having thought of it first
  — the over-crediting **is** the scorekeeping (the user, 2026-08-11: *"a healthy collaborative
  relationship does not expect a 100-0 scoreline"*). This does not touch correcting the record: a
  wrong claim still gets corrected plainly, and a rejected alternative still goes into
  `Docs/Combat-Decisions.md` with its reasoning intact, because that is knowledge rather than credit.
- **Instrument before theorising.** When behaviour is wrong and the cause is not obvious, enable a trace before proposing an explanation, and prefer an experiment that manipulates the suspected cause over one that only observes it. See `Docs/Working-In-Unreal.md`.
- **Never claim something does not exist based on a filtered or derived view.** A search that finds something proves it exists; a search that finds nothing proves only that your filter did not match. Three wrong claims in `Docs/Animation-Library.md` came from exactly this — a prefix filter, a first-token summary, and a mismatched granularity — each reported as absence. Before writing "there is no X", search the authoritative source unfiltered, try synonyms and known misspellings, and quote the command you ran. If you cannot show the search, do not make the claim; say you did not find it and name where you looked.
  **And date every absence claim you write down, with what you searched.** Absence claims rot faster than any other kind, because they are statements about a world that keeps changing around them — the 2026-08-12 audit found two that had gone stale, including "nothing places the dummy below the player" about a level that has a ramp. `Docs/Animation-Library.md` shows the form to copy: *"checked 2026-08-10 across all 6,576 rows of the index"*. A dated, scoped claim tells the next reader its shelf life; a bare one is indistinguishable from a guess and will be trusted like a fact.
- **Do not delete lines you did not write without asking**: most of them are scar tissue from something that went wrong once. *(A re-read-this-file-before-finishing rule stood here until 2026-08-15 and was removed as a duplicate of `Docs/Closing-Down.md` step 3, which says it better and carries a trigger. One fact, one home — the copy nobody reviews is the one that rots.)*
- **At startup, check that the previous session wound down, and complain if it did not.** A dirty working tree, or stranded packages in `Saved/Autosaves/PackageRestoreData.json`, means the closedown procedure did not finish. **Say so before starting work** rather than absorbing it silently. Added 2026-08-15, the user's call, replacing the rule above: a ritual with no inbound check fails quietly, and both signals are free at session start.

## Closing down a session

**The procedure is `Docs/Closing-Down.md`. Read it when the user says to wind down, and not before** —
whether a session concludes is theirs to decide, never yours to infer. Eight steps: make the editor
state safe, commit what is verified and *propose* the push, audit the always-read files for bloat as
well as truth, discharge what you fixed, update the focus, check memory still points rather than
restates, hand off explicitly, and title the session in five words.

## Current Focus

**Items are named, not numbered** (2026-08-12, replacing fifteen numbers). A name keeps the property
the numbers were chosen for — **it does not change when the order does** — and adds one a number
cannot have: it can *go wrong*, so a renamed item gets a row in the retired-item-numbers table
exactly as a renamed symbol does. **The dated archive still uses the numbers** (51 references,
counted 2026-08-14) and is never rewritten; `Docs/Combat-Decisions.md` carries the bridge.

Execution order, the only line that changes when the order does:

> **~~Attack Ladder~~ → ~~Dodge~~ → ~~Sword & Shield~~ → ~~Input Buffer~~ → ~~Death~~ → ~~Dodge Distance~~ → ~~Attack Swap~~ → ~~[hover bug]~~ → ~~[facing pass]~~ → ~~Recovery~~ + ~~Lunge~~ → ~~Target Lock~~ → ~~Block~~ → ~~Light String~~ → Parry → Knockdown → Polish → Death-full → Settings → Netcode → Tuning Rig → Interplay**

**Structure Audit is deliberately absent from that line** — its structural half ran 2026-08-15,
and what remains keeps a trigger rather than a position; see its entry at the end.

**Pick up at Parry.** Light String shipped 2026-08-18 and its roster is settled; see Done. Nothing
is blocked. **Read Parry's entry in Remaining before starting** — its 400 ms / 500 ms / 1000 ms
numbers predate the light's move to 200 ms and must be re-derived against the current ladder, and
its remaining clip question needs a preview rather than a search.

**Two things Light String left owed and unowned.** Its **play pass** belongs to Interplay by nature
and is listed under Done. And **heavy and charged still have no clips of their own** — shopping was
deferred for time on 2026-08-18, and the **bespoke windup pass** that wants them is **Polish**, third
in the order, so both are picked up there.

*(The 2026-08-14 audit's five open checks are all discharged; what still matters moved to where it
is used — the shadowed-value rule above, plus tables in `Docs/Combat-Decisions.md`.)*

### Done

**Detail is carried for the two most recent shipped items only.** When a third ships, the oldest is
**evicted by routing, never by deletion** — every consequence has a destination, and the eviction is
not finished until each one is at it:

| Consequence | Goes to |
|---|---|
| A design rule that still governs play | **Core Combat Rules**, above — that is where a reader looks for it |
| A latent defect or unverified assumption | the **traps** section of `Docs/Combat-Decisions.md` |
| A value that is **derived** and must not be tuned freely | the **tuning map** there, phrased as *"nothing, without re-deriving it"* |
| Which knob moves for a given complaint | the **tuning map** there |
| Rationale about one symbol | that symbol's **header comment** |
| The argument behind any of it | its **dated entry**, where it already is |

**Nothing is lost by an item having no entry**, because the execution-order line above is the
complete roster — every item, struck through when done, and still one line at fifty items.

**Why two and not one:** a just-shipped item's consequences are the least understood, and one slice
of hindsight is what reveals which actually bite. Observed 2026-08-14, when the older items had
already collapsed to a line or two on their own while the two newest carried 62% of the section.

**Why this exists at all.** Done grew with every shipped slice and nothing ever evicted, while the
unbuilt items stayed flat — so a file loaded in full every session grew monotonically with project
progress. The cause was not verbosity: **design rules were accumulating in a changelog instead of
graduating into the spec.** This makes Done O(1) rather than O(items shipped), which is what the
prototype needs to survive becoming something larger. Rule added 2026-08-14; the first eviction
covered seven items.

**Graduation has a bar: the rule must be general.** Something that only makes sense as the history
of one slice is not a rule and goes to the decision log. Otherwise Core Combat Rules becomes the
new dumping ground and the problem has merely moved.

- **Light String** — the chained light. **Shipped at three attacks 2026-08-18**: mechanism played
  and measured, clips authored, and `s4-string` / `s4-guarantee` / `s4-block` green alongside the
  existing suite. Its rules are in Core Combat Rules above. **The knockdown terminator is Knockdown
  & Oki's** — the ender currently *displaces* its victim instead, which that slice replaces.
   - **The roster is V3 `Attack4_Stage1` → `Attack8_Stage2` → `Attack2_Stage2`**, the last also the
     project's first 360° attack, given a 360° damaging volume and a release authored to its own
     notify width so it plays at true speed. Heavy and charged clip shopping was **deferred for
     time and is unowned** — the bespoke windup pass wants them anyway.
   - **`_Complete` for every attack, mid-string included**: the player may stop after any hit, so
     each must resolve on its own. The consequence nobody argued for is that `_Complete` clips end
     in a settled pose — which is why light 2's ending blends into light 3's 360 better than either
     was chosen for. **That pairing is emergent and nothing protects it**; no assertion can see it,
     so swapping either clip spends it silently.
   - **The play pass is owed and belongs to Interplay by nature** — cadence and mash feel,
     hold-to-heavy mid-string, the blocked-string mind game and corner-carry. **The play pass was
     run by the designer 2026-08-18 and passed**, and the buffered-aim 1vX test its trap prescribed
     is done and the trap discharged.
- **Block** — the full defensive half. Mechanics shipped and played 2026-08-14; the animation
  remainders shipped **2026-08-16**, play-verified, with `s2-light` green on all seven assertions
  after a `--self-test` proved the checker can fail. The rules are in Core Combat Rules above.
   - **The guard is a `Blocking` state with the guard pose layered onto the upper body**, not a
     whole-body pose. `Mesh Space Rotation Blend` is what makes it work; left at its local-space
     default the chest inherits the pelvis and the shield still swings. **That is a correctness fix,
     not polish** — block covers 180° in the *defender's frame*, which is camera-locked, so a shield
     that turned with movement showed coverage the mechanic does not have.
   - **Blockstun is a state too, entered only from `Blocking`**, and exits on
     `NOT IsInBlockstun || NOT IsBlocking` — the pose depicts a guard, so it ends when the guard
     does. **Blockstun is felt more than seen**, which is also the standing argument against
     directional blockstun ever paying for itself.
   - **The Selects that swapped the blendspace had to go**, not as cleanup: the EventGraph runs
     before the AnimGraph, so the swap landed on the same frame the transition began and the machine
     blended the block pose to itself. They did not duplicate the state machine, they cancelled it.

**Evicted:** Target Lock on 2026-08-18, its aim-assist rules graduated into Core Combat Rules above. **Evicted 2026-08-14 and fully routed:** Attack Ladder, Dodge, Sword & Shield, Death, Dodge
Distance, Attack Swap and Input Buffer, plus the three things that were never items — the netcode
groundwork **Slices A and B**, the **hover bug** and the **facing pass**. Their rules are in Core
Combat Rules, their open questions in the slices that answer them, their latent defects in the traps
section, and their reasoning in dated entries. `Docs/Combat-Decisions.md`'s symbol index is the way
back to any of it.

Open and **not** a defect: the character can stand on the ramp's near-vertical edge face, so walking
off it descends that face before free fall. Whether `MaxWalkableFloorAngle` should permit it has
never been examined; the value has not been read.

Every item gets done; only the sequence was ever in question. The first three were ordered by
dependency, the rest is judgement and may be revisited.

### Remaining

In execution order, and all sequential. **Light String shipped 2026-08-18** and Block on 2026-08-16; see Done.

- **Parry.** **Re-searched 2026-08-11** by enumerating every distinct `SwordShield` move rather than grepping for parry words, and the earlier picture was too thin. Beyond `Block1_Parry` there are `Block1` and `Block2` — discrete block actions with their own `_Idle` and `_Hit` — so there are **three candidate shapes plus failure states**, not one clip, and all are already migrated. The two packs split by **idiom**: V1 does held guard (Block's), V3 does discrete actions (a parry's). The `SwordShield` archetype holds three differently-named packs (`SwordAndShieldAnimV1`, `SwordShieldAnimV2`, `SwordSwordAnimV3`) and dual-sword content is all `DualSwordAnimation*` in its own archetype — so `SwordSword` is a vendor naming quirk, not a stance. What is still open needs a preview, not a search: whether V3's guard pose reads consistently beside V1's. Details in `Docs/Animation-Library.md`. **Re-derive the spec's numbers against the current ladder before building** — the 400 ms window / 500 ms reward / 1000 ms whiff lockout predate the light's move to 200 ms (flagged 2026-08-15).
- **Knockdown** *(functionality; from Stun's 2026-08-15 split, renamed and halved 2026-08-18)* — knockdown itself, the 1.5 s default get-up, the three early get-up options and the get-up attack, the charged's **hard knockdown** grade, plus the guard break's full-lockout state its trap defers here, **jump-as-ability** which rides it, and hitstun's movement lock (deferred beside the guard break's). **It is the light string's terminator**: the ender knocks down, and today it *displaces* instead — `ApplyKnockbackToTarget` runs on every unblocked hit with no final gate, left alone deliberately because this slice replaces what the ender does to its victim wholesale. That also makes it **what widens `s4-360`**, which asserts the first burst only for exactly that reason. **`SwordShield` has no get-up content whatsoever** — unfiltered search for `Rise|GetUp|StandUp|Recover|Wake|Prone|Ground|KnockDown|Knock|Fallen|Down` returned zero for the archetype (2026-08-10). It exists only in `DaggerCombatAnimationV1` (18: `Rise1`–`Rise9`, two variants each) and `Unarmed` (8, including the bundle's only explicit `KnockDown`). Knockdown recovery therefore needs a **cross-archetype migration** — raise it before the slice starts.
- **Polish** *(style over substance; split from Knockdown 2026-08-18, the designer's call)* — deferred work that changes how something *reads* rather than what it does. **Carries the bespoke windup pass**: heavy and charged get their own clips, their windups become **blended transitions** into real anticipation, and **coil is deprecated**. It belongs here rather than in Knockdown because the reactability arithmetic is untouched — the blend occupies exactly the window the coil did, 350 ms light→heavy and 300 ms heavy→charged — so only the tell's *expression* changes, freeze to visible repositioning. **Sits early deliberately**, right after Knockdown: it must precede Interplay or the feel verdict is taken with both tiers still playing the light's clip. Spec, candidate pool and the two measured findings behind it are in `Docs/Combat-Decisions.md`, 2026-08-18.
- **Death-full** *(from the same split)* — death's real treatment replacing the debug ragdoll, hit-reaction animation, and the questions **Death** deferred. `SwordSwordAnimV3` has **four directional** `Hit_<DIR>` and **four directional** `Death_<DIR>` clips, not single standalone ones (verified 2026-08-10). **Hitstun ships with Light String** (settled 2026-08-16); what this slice owes it is the reaction *animation*.
- **Settings menu.** Raised 2026-08-12. Mouse sensitivity is the immediate want, and it should own
  **`TurnRateDegrees`** too — that number stopped being cosmetic the moment attacks began pointing
  wherever it had turned to, so exposing it is a balance decision rather than a comfort one, and a
  player lowering it would be quietly worsening their own aim without being told. Also the natural
  home for a **turn cap** if fast-spin inputs ever need bounding, which single-rate facing already
  provides incidentally. **A remote playtester's packaged build has no editor and no cvars — this
  menu is their only tuning surface** (2026-08-15); it precedes Netcode's real-remote milestone by
  construction. Last of the megaslice.
- **Netcode** — the behavioural pass the 2026-08-15 recon mapped: the two `SetTimer` sites and
  i-frame lag compensation (one problem twice), prediction windows, client stamina prediction, the
  loose-tag aim-assist asymmetry, and a shareable direct-connect build — lobbies and matchmaking
  stay out of scope. **It opens with the three checks V2 could not run** (2026-08-15, reclassified
  from chores: client attack → server damage, the client-tag re-measure now `DEATH`/`EXHAUSTED` are
  sited in `Apply*`, and `OnRep_PlayerState` via the `ASC RESOLVE` line, which is confirmed working
  in standalone). None is input-blocked any more — `Net PktLag` runs from the editor console. **The kill-question comes first**: `PktLag` 40/80/120 emulation, one human as
  client versus the fixtures, measuring whether the reactability budget survives a round trip
  *before* any prediction machinery exists. The single-player checker never reads a two-player
  log; **a netcheck sibling — bands, assertions and a self-test over both logs, grown from the
  two-log recipe — is a budgeted deliverable of this slice, not an option** (2026-08-15, the
  user's call: the riskiest phase does not run on the weakest verification).
- **Tuning Rig** — every designer-facing combat value live-tunable at runtime, because Interplay's
  real cost is iteration latency: a tweak today is a PIE restart locally and a full reconnect
  against a remote player. **v1 is local-only and lands inside the megaslice** (2026-08-15, the
  user's call — the first sitting that wants it): a reflection-driven panel over the `Combat|*`
  categories writing **live instances**, `TUNE` trace lines for every change, and **the checker
  refusing any log containing them — shipped with v1**, or a tuned smoke-log silently pollutes a
  regression run. CDO write-back stays a once-per-session editor-side step, where the staleness
  traps live. **v2 is this roster position**: the dev-only remote channel for per-machine values
  (the far client's buffer, their sensitivity), designed against Netcode's replication reality.
  **Derived values surface as derivations, read-only or auto-recomputed** — the rig encodes the
  tuning map's relationships rather than exposing bare floats, or it industrializes the exact trap
  class the docs fence. Rationale and the design questions: `Docs/Combat-Decisions.md`, 2026-08-15.
- **Interplay** — the deliberate feel pass, one remote human against the designer, **on the wire**,
  because the shipping game is the networked one. Consumes everything parked on verified-good —
  the reach/travel/spacing re-author, the lunge strength curves, the heavy's reactability retune,
  blockstun and commitment tuning — and **re-derives the checker's bands once, against final
  numbers, never patching them to green**. The naive player's reads outweigh the designer's.
  **Owns the input-forgiveness subslice** (2026-08-16): whether the buffer extension over-forgives
  mashing in a game built on deliberate precision, and whether the chain windows are too vast.
  Kept on probation rather than settled, because none of it is felt yet; see the decision log.
  **Verified-good is called here; Combat AI follows it, never precedes it** — the reasoning,
  including why Netcode needs no AI, is in `Docs/Combat-Decisions.md`, 2026-08-15.

**Structure Audit — the structural half ran 2026-08-15, by the user's call; the feel half keeps
its trigger.**

Raised 2026-08-12 as an audit of what is designer-facing, widened the same day to the project
entire. **Run 2026-08-15**: the docs re-audited for truth and budget, the never-referenced
`Variant_Combat` / `ThirdPerson` / `LevelPrototyping` template trees deleted from source and
content (the asset registry showed zero external referencers; `Characters` and `Input` are the
template content that remains, both live), StateTree dropped from the module and plugin lists,
and the founding irritant discharged — `ReleaseStartSeconds` and `WindupSection` now live in
`Combat|Animation`, the guard's knobs and getters in `Combat|Block`. One limit met: the details
panel ignores struct-member categories inside arrays, so grouping *inside* `Branches` still
rests on the property comments.

**Candidate raised 2026-08-15: trimming `/Game/Characters`.** 128 assets, **five referenced** — the
jump/fall/land clips, `CR_Mannequin_FootIK` and `SK_Mannequin`. The rest (a Pistol set, a Death set,
others) is dead weight. **It is a per-asset trim, never a folder deletion** — the details and the
deliberate two-skeleton arrangement are in `Docs/Animation-Library.md`. Whether the template's six
`MM_Death_*` clips are worth keeping is **Death-full's call**, not this trim's: `SwordSwordAnimV3`
already has four directional `Death_<DIR>` clips authored for an armed character.

**Deliberately not done, and why:** splitting `ATDCombatCharacter` — moving a UPROPERTY orphans
every Blueprint CDO override of it, so reorganising before the systems settle would be paid for
twice; jump-as-ability, which rides **Knockdown** per the guard-break trap; and the decision
log's archive, which is append-only by design.

**Lunge strength curves stay parked for Interplay** (2026-08-13 as the verified-good trigger,
resolved to Interplay's roster position 2026-08-15). They are last-10% feel tuning, not
structure: assets exist, wired to nothing, and the tuning map carries the warning that a curve's
mean must be 1.0 or it silently scales the authored distance. The reach/travel/spacing re-author
shares that home — **Interplay consumes both**; see its entry in Remaining.

**Verification infrastructure — all three packages shipped 2026-08-15** (defense-capable dummy,
regression loop, two-player recon). `Docs/Debug-Instruments.md` carries the fixtures, scenario matrix,
checker and two-player recipe; the plan file that contracted them was deleted on delivery, as its
own header required.