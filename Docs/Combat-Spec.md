# Combat spec

**Read this before changing how combat behaves** — a timing, a cost, a window, a volume, a state
transition. Not otherwise. `CLAUDE.md` keeps only the vocabulary and the ladder table, because those
are needed to read a commit message or a trace line in any session.

**`GA_Attack`'s `Branches` array and the character Blueprints' CDOs are authoritative for live
values.** Numbers below carry an argument, not a reading — when the two disagree, the asset wins.
Reasoning for every choice is in `Docs/Combat-Decisions.md`; which knob to move when a verdict comes
back is its tuning map.

---

## The laws

The governing layer: every mechanical section below is an implementation of these, and a change
that bends one is a design decision, never a tuning pass. *Graduated 2026-08-18 from the Exchange
entry in `Docs/Combat-Decisions.md`, which keeps the map, the scenario walks, and that date's
numbers; when current math and a law disagree, the math is what moves.*

1. **The two ledgers.** Every exchange settles in initiative and stamina; nothing else keeps
   score. A mechanic that arrives wanting a third currency is misdesigned.
2. **Stamina never gates; time gates only at commitments.** Every input is always accepted. Only
   commitments refuse you — your own attack past its checkpoint, a stun, a lockout, the guard's
   floor — and every refusal in the game traces to one.
3. **Punishment attaches to failure.** A whiffed or blocked commitment hands the opponent its
   recovery; a clean hit waives defense instantly, returns movement when the victim can respond,
   and is never taxed like a miss.
4. **Accidents never impersonate reads.** Every defensive expression is a deliberate, orthogonal
   input. The system may not manufacture a call you did not make — for you or against you.
5. **Block answers pressure, never commitment.** The guard is home against the fast layer and the
   wrong place to stand once a commitment is telegraphed; teaching you to leave it is the ladder's
   job.
6. **Reaction survives; only a read profits.** Calm reaction to a real tell always has a safe
   exit, and converting a commitment into punishment always requires having predicted it.
7. **The ladder brackets timing from both jaws.** The fast commitment punishes hesitation; the
   slow one punishes anticipation and worn guards. Between the jaws sits exactly one honest
   answer.
8. **The mixup lives where discrimination dies.** Tiers are indistinguishable precisely as long as
   they must be; a tell, once given, is always real.
9. **Depletion escalates the skill demanded.** As stamina falls, priced answers disappear and
   reads remain — the losing player is funneled toward the highest expression, never locked out of
   playing.
10. **Feints are defenses worn as offense.** An abort routes only into a priced defensive action,
    so the bluff always costs the bluffer; bluffing lives where coils are long.
11. **A hit ends the argument; a block continues it; a parry reverses it.** The three resolutions
    of any swing, and the whole conversation in one line.

---

### Offense (Melee)
**An attack is defined by when it hits, not by how it plays.** Each tier authors the moment its
hitbox goes live and the input boundary you must release before to get it — **the
two-numbers-per-tier ladder table lives in `CLAUDE.md`**, the one fact this file cedes, because
those numbers are needed to read a trace line in any session. Every play rate is derived from them
at runtime.

**Space is authored the same way, as of 2026-08-12.** Each tier also authors its damaging volume — reach, arc and a vertical band — as an `FTDAttackHitbox` on its branch, so range is a designed number rather than a property of the clip.

**Displacement too, as of 2026-08-12** — two authored distances in centimetres, not a scale on the clip. A shared **base lunge** from the press to the light's input boundary, then a **per-branch lunge** from the commit checkpoint to the end of the release window. The coil carries neither, which is what keeps the tiers indistinguishable for as long as they must be.

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

- **Light**: 2–4 hit string (weapon dependent) — **shipped at three hits 2026-08-18**; the knockdown terminator is Knockdown's. **No light is truly safe** (2026-08-16, superseding *"first hit safe on block; subsequent hits are not"*): recovery is authored long and only chaining skips it, so on whiff **and** on block the real cover is the defender hesitating against the next hit — the delay-and-bait layer above that is the design, verified against the authored values (finishing is punishable by 350 ms, stopping early by 200, an immediate chain beats an eager punish by 50); argument in `Docs/Combat-Decisions.md`. **Every non-final hit carries the target to one authored spacing** in front of the attacker, identical every time; a blocked hit is centred exactly the same but concedes notably less ground. Any hit in the string guarantees the rest — **coupled to the ban on heavy→light; they are one decision** (see the log). Last hit knocks down but has heavy endlag. Minimal stamina damage. It never *coils*, so no tell distinguishes it from a heavy — but **it is not unreactable**: the montage starts on the press, so the windup is a tell from frame one, and **250 ms proved reactable, which is why the light hits at 200** (2026-08-11/12); the 150 ms boundary is the measured floor for trivially consistent inputs. Whether 200 ms is far enough is itself unverified — it has never been played against a human.
- **Heavy**: single hit. **Plus on block** — the intended paid transaction, 50 stamina bitten and initiative retained — punishable on whiff. Knocks down. Higher range, moderate stamina damage. **Rapid as of 2026-08-18**: it arrives at **350 ms**, giving a coil → damaging window of **200 ms**, down from 350. That shortening is the point rather than a side effect — at 350 ms of tell, blocking until the coil and then reaction-dodging answered every held attack with no read required, and profited. The ladder now poles as a **fast layer** (light 200, heavy 350; the read is *"they pressed"*) against a **slow layer** (charged 750; the read is *"they're charging"*). *The old coupling still holds and still has nothing enforcing it: the coil begins where the light stops being available, so any change to the light's boundary moves the heavy's reactability with it.*
- **Charged Heavy**: single hit. Breaks block, heavy endlag, knocks down. Highest range. Very reactable.
- Any light in a chain can be held to convert into a heavy.
- Some heavies can chain into further heavies; never into lights.

Timings land within about a frame, biased late.

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
- **Parry** (MB4): **shipped 2026-08-18.** A **300 ms** active window from the press, 360° and with no facing test — it is a read of *timing*, and requiring it to be spatial too would price two skills into one input. The window is **mechanical**, a timestamp rather than a notify, so the animation can be retimed or absent without changing what the parry does. Costs **no stamina** to throw, completing the pricing symmetry: dodge is stamina-priced, block is priced in both ledgers, parry is purely **time**-priced.
  - **Success negates the hit entirely** — no damage, no stamina damage, no blockstun, no hitstun, no knockback — and **plants the attacker at zero distance**, who then rides their own attack into recovery. **The reward is derived from that and is not authored**: recovery already *is* the punish window, so it scales with the victim's commitment for free, and a parried charged is near-guaranteed where a parried light is not. The spec's old 500 ms offensive lock **re-derives to deleted**, subsumed with better per-tier texture.
  - **A parried attack cannot chain, and loses the string** (2026-08-18). It cannot skip its own recovery, and the attacker's next press starts a fresh swing 0. That second half is what compensates where the derived model is weakest — the light has the shortest recovery and so the feeblest punish — without authoring a per-branch bonus, which was raised and rejected.
  - Success also pays **+25 stamina** and **discharges the regen pause outright**. A parry carries `State.StaminaRegenPaused` like every other ability, so a *whiff* pays the pause in the ordinary way and a success does not: the reward shows up in the stamina ledger as well as on the clock.
  - **Whiff pays `State.ParryLockout`**, 600 ms of refused defensive activations, floored so that a whiff timed against the fast layer stays locked through the charged's 750 ms arrival. **The same tag carries the post-dodge gap**, 150 ms after any dodge ends — without it a predictive dodge chains into a parry covering the charged, and the vise's late jaw unscrews.
  - **Refused while blocking** (a property of the guard, not of blockstun), while dodging, while exhausted, and while airborne. **Never buffered**: a replayed parry is a mistimed parry, and a buffered one is a call the player did not make. **Blockstun and parry never know about each other.**
  - **Movement is locked for its duration**, following the split the code already draws: actions own their displacement, states leave you mobile, which is why block is the one defensive ability that does not lock.
  - *Success vs ranged redirects to crosshair* stays unbuilt and out of scope with ranged. Parry **ships felt-not-seen** — `AM_Parry` is outstanding, and nothing plays a montage yet.

### Stun & Knockdown
- Blockstun: **Disables offense and nothing else**, for a duration the *attack* authors (`BlockstunSeconds` per branch on `GA_Attack`). **Defense is deliberately untouched** — movement, dodging, the guard itself and, when it exists, **parry**, which means **blockstun and parry never know about each other** *(2026-08-15; the old "offense + parry" wording was wrong from this file's beginning and the implementation never matched it — see the log)*. Taking a defender's guard for blocking correctly would invert the mechanic. **A guard break supersedes it rather than stacking**: a broken guard is not a successful block. Shipped 2026-08-14 at each tier's own `RecoverySeconds`, 50 ms the safe side of neutral. **The light's stopped being derived that way on 2026-08-16**: once a chain existed, its own recovery was measuring against the wrong threat, so its 0.35 is derived against the **chain cadence** instead — heavy and charged keep the recovery basis. Both derivations are tuning-map rows, and the light's is not free. **The charged's can never fire** — its stamina damage empties any bar, so it always breaks instead; filed as a trap.
- Hitstun: authored per attack (`HitstunSeconds` on `GA_Attack`'s branches and swings; the light's is **0.55**), and it **refuses every ability, defense included** — *that refusal is what makes "any hit guarantees the rest" true*, and it is why hitstun is a Light String mechanic rather than a Knockdown one. **It must outlast the chain gap or the guarantee silently stops holding**, which is why the cadence and this move together. Movement stays free; the lock is Knockdown's, deferred beside the guard break's.
- Knockdown: 1.5 s default get-up. Early get-up via Dodge, Block, or Attack. Get-up attack knocks back, short recovery, very punishable on block/whiff. **A charged's knockdown is hard — fewer get-up options** (2026-08-16); the grades are Knockdown's to build.

### Stamina
- Max 100.
- Dodge = 50.
- Blocking drains based on attack + blocking weapon.
- 0 stamina → Exhausted (no defensive actions or jump) **until stamina refills to 100**, not for a fixed duration. **Exhaustion also slows you**, to `ExhaustedMaxWalkSpeed` — 400 against a normal 500, authored 2026-08-14 so the state reads in the body rather than only as a refusal. It combines with the guard's cap by **taking the slower**, which is reachable because a guard raised too poor to pay for itself leaves you exhausted with the guard up. Stamina floors at 0, so there is no overspending and every exhaustion is identical — dodging at 3 and dodging at 50 both land on exactly 0. Re-emptying the bar the moment you recover is allowed. **Regen continues while exhausted** — it locks out acting, not recovering, and is the only thing that can end it. **The regen pause still applies, though** (2026-08-14, from play): a player may hold block at 0 and suppress their own recovery indefinitely, which is a choice with an obvious exit rather than a trap.
- **Regen runs at two rates**, `StaminaRegenPerSecond` normally and `ExhaustedStaminaRegenPerSecond` while exhausted; the character Blueprints' CDOs are authoritative, with defaults in `ATDCombatCharacter`. *(`StaminaRegenPauseSeconds` beside them **is** overridden by both character Blueprints; the two rates are not — confirmed 2026-08-14/15.)* Exhaustion is the slower of the two, so being run dry costs more than the bar it emptied. **The exhausted rate may never be zero** — regen is the only thing that ends exhaustion, so zero means permanent. Paused during any action carrying `State.StaminaRegenPaused` and for 0.5 s after, measured from when the action ends (`StaminaRegenPauseSeconds`), **exhausted or not** — the pause is a cost of acting and exhaustion is not a refund. **`GA_Dodge`, `GA_Block` and `GA_Attack` carry it**, attacks as of 2026-08-14, so a swing is taxed for its whole windup, release and recovery plus the tail. The tail is **shared, not per ability** — that split was specified and declined the same day as authoring overhead for a distinction nobody has felt; play asking for it is the trigger.
- **Jumping costs no stamina but pauses regen** — from the jump until 0.5 s after landing. Keyed to the jump *action*, never to being airborne: walking off a ledge costs nothing.
- **Costs are paid, not required.** No action is ever refused for want of stamina: dodging at 30 works, empties the bar and exhausts you. Never use GAS's `CostGameplayEffectClass`, which gates activation, and do not call `CommitAbility` — checking a cost is the gate. Costs are applied via `UTDGameplayAbility::EffectOnStart`.
- Regen, the pause and exhaustion are orchestrated in C++ on `ATDCombatCharacter`, not by GameplayEffects — see `Docs/Combat-Decisions.md`.

