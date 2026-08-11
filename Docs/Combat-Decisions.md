# Combat decision log

What was decided, why, and what is still open. Newest entries at the top.

This file records **reasoning**, not implementation. Code and Blueprints are the
authority on how things work and what the numbers currently are; entries here explain
why the shape is what it is, and stay true even after the code moves on. An entry is
never rewritten to match new code — a reversal gets a new entry that supersedes it.

Append an entry whenever a gameplay or combat choice is made that a future reader could
reasonably second-guess. Skip it for anything the code says plainly on its own.

---

## Open questions

- **Does the 250 / 500 / 750 ms ladder hold up once there is something to fight?**
  Playtested 2026-08-09 and judged good in isolation — nothing felt wrong about the three
  tiers or about escalating between them. But that verdict was reached against a training
  dummy, which cannot test either of the things the ladder exists to serve: spacing and
  whiff punish. Treat it as provisionally answered and re-ask once block, dodge and parry
  exist. The alternative still on the table is a faster 250 / 500 ladder (roughly Divine
  Knockout vs. New World feel); all thresholds are per-branch `EditDefaultsOnly` floats,
  so trying another is a Blueprint edit.
- **How far should the coil be allowed to creep?** A slow creep was chosen over a hard
  freeze (see below). The creep's *rate* is no longer a choice — it is derived from the
  distance still to cover and the time left before the deepest branch commits — so the
  knob is `CoilEndSeconds`, how far it may creep, rather than a rate. Closer to where the
  coil begins reads as a hold; further reads as one continuous wind-up. The current value
  is a guess, and the thing to judge is still "loaded" vs. "stalled".
- **Should all three attacks share identical impact frames?** They no longer do. The
  coil advances the animation while held, so the gap between commit and first active
  frame shrinks the longer you charge. This may read correctly — a wound-up attack
  snapping out faster is intuitive — but it reverses an earlier deliberate goal.
- **"Release" is overloaded.** It names the damaging phase *and* the button coming up
  (GAS gives us `InputReleased`, which cannot be renamed). Mitigated by convention
  rather than solved; revisit if it causes a real misread. "Active" is the standard
  alternative.

- **Does an aborted attack cost anything?** Cancelling into block currently costs only the
  time spent. If feinting into guard turns out to be too cheap to punish, a stamina cost on
  the cancel is the obvious lever — but it should not be added pre-emptively.
- **Recovery is shorter than it looks, by exactly the montage's blend-out.** Found in review
  2026-08-10, not by play. `UTDMeleeAttackAbility::StartAttackMontage` binds `HandleMontageFinished`
  to **both** `OnCompleted` and `OnBlendOut`, and `OnBlendOut` fires when blending *starts* — so
  the ability ends, and `State.Attacking` / `State.Attacking.Committed` come off,
  `BlendOut.blendTime` before the swing visually finishes. That is **0.25 s** on
  `AM_LightAttack_01`, which is large beside a 250 ms light. The consequence is that mechanical
  recovery and visible recovery differ, and recovery *is* the punish window. Settle deliberately
  when recovery and punish windows are built: either recovery ends at blend-out and the animation
  is authored to agree, or the ability waits for `OnCompleted` and blend-out becomes dead time.
  Do not discover this by tuning around it — it is also the true cause of the debug attacker
  resetting before its swing looked done, which was patched with a delay rather than diagnosed.
- **The melee trace opens on any `Event.Melee.WindowBegin` reaching that ASC.**
  `UAbilityTask_MeleeTrace` subscribes by tag and does not check which montage sent the event.
  Correct while exactly one montage carries the notify, and quietly wrong the moment a second one
  does — a plausible future for the light string or a shield bash. Worth a montage check before
  any second `Release Window` notify exists, not after.
- **Does a 2.25× roll read as snappy or as fast-forwarded?** `DodgeSeconds` is 0.4 against 0.9 s
  source clips, so the derived play rate is 2.25×. Unanswerable until `AM_Dodge` exists, and it is
  being built with **whole, untrimmed clips** per the entry below — so this question is now asked
  honestly rather than pre-empted. If it does read sped-up, the first lever is `DodgeSeconds`
  itself, not the clip.
- **Input buffering is a prerequisite for judging any of this fairly.** Raised 2026-08-10 while
  testing the dodge. Without it, an input pressed during a committed action is simply dropped,
  which is indistinguishable from the action feeling unresponsive — so every timing verdict is
  currently confounded by inputs that never registered. It is not part of the defense slice, but
  it should land before the ladder is tuned as a whole, or the tuning will be fitted to a
  handicap.
- **Is the roll's authored travel distance right for our spacing?** Play rate fixes the
  0.9 s length (see the entry below) but it does **not** change how far the roll goes — a
  faster roll covers the same ground in less time. If the distance itself is wrong, the knob
  is `AnimRootMotionTranslationScale` on the montage task, not the rate. Unjudgeable until
  it is in play, and it matters because spacing is the top feel goal.
- **Should the debug auto-attack move, or only swing?** A dummy that attacks on a timer from
  a fixed spot tests i-frames but not spacing, and spacing is the top feel goal. Adequate for
  the dodge slice; inadequate for judging whiff punish later.
- **Can exhaustion still fail to fire for an action paid at exactly 0?** The delegate only
  fires on a *change*, so a cost applied at exactly 0 stamina changes nothing and cannot
  retrigger. Unreachable today — exhaustion now ends only at full, and every defensive action
  is locked out until then, so you cannot be at zero and un-exhausted. **Block may make it
  reachable**, because `ActivationBlockedTags` gates activation and not continuation: a block
  held through zero keeps draining and keeps `State.StaminaRegenPaused` applied, which would
  stall recovery and therefore stall the exit condition. Check it when block is built.
  (The related worry that exhaustion "does not scale with overspend" is closed rather than
  fixed: stamina floors at 0, so overspending does not exist and every exhaustion is the same
  length by design.)

---

## Retired names

Dated entries are never rewritten, so they still name things the code has since dropped.
This table is the bridge; without it a reader greps for a name, finds nothing, and
concludes the log is wrong rather than merely old. Add a row whenever a name changes.

| Entries say | Code now |
|---|---|
| `CoilPlayRate` | Derived at runtime from distance and time remaining. The authored knob is `CoilEndSeconds`. |
| `CoilCeilingSeconds` | `CoilEndSeconds` |
| `CoilStartSeconds` | `Branches[0].HoldUntilSeconds` — the coil begins exactly where the light stops being available, so it is one number, not two. |
| `WindupSeconds`, `MinHoldSeconds`, `MaxHoldSeconds` | Per-branch `HoldUntilSeconds` (the input boundary) and `ReleaseAtSeconds` (when the hitbox goes live). |
| `LogTDCoil` | `LogTDCombatTiming`, behind the `TD.DebugCombatTiming` cvar. |

---

## 2026-08-10 — The defensive regen pause is 0.5 s; felt feel beat the argued distinction

`StaminaRegenPauseSeconds` drops 1.0 → 0.5, which makes it equal to `JumpRegenPauseSeconds`.

This supersedes the reasoning in "Jumping is taxed in recovery" below, which set the jump's tail
at half the defensive one *because* "a jump is a weaker commitment than a dodge and should not be
taxed as heavily." That argument is sound and it lost to play: with the montage finally attached
and the dodge legible, 0.5 s simply felt right and 1.0 s did not. The distinction was reasoned
into existence before anyone had felt either number.

**The two values stay separate even though they are now identical.** Not an oversight — the
separation is capacity, not an active claim. Collapsing them into one number would make
re-splitting them a code change rather than a tuning pass, and there is no cost to holding two
fields that happen to agree.

The transferable rule, now also in `CLAUDE.md`: **when play and rationale disagree, play wins.**
This log exists to make choices legible, not to defend them against evidence. A carefully argued
entry is not a commitment — it is a record of what was known at the time, and superseding one is
the system working rather than failing. That matters here specifically because this log is long
and persuasive, and a persuasive argument is exactly the kind of thing that can outlive its
usefulness quietly.

## 2026-08-10 — The evade is a dash, not a roll; the roll choice came from an incomplete search

`AM_Dodge` is built from the eight `AS_SwordAndShieldAnimV1_Dash_*_RM` clips. This supersedes
"Sword and shield, rolls for every evade" below.

**The original entry was wrong by omission, not by reasoning.** It argued that the library has no
forward `Dodge` in any archetype while `Roll` covers all eight, so committing to rolls avoided
mixing two move types under one button. Every word of that is true. It simply never checked
`Dash`, which also covers all eight directions in the same pack, and was sitting migrated in the
project the whole time. The user found it. The constraint that supposedly forced rolls never
actually discriminated between the candidates.

The full set, all eight-directional and all already migrated:

| Clips | Length | Play rate at `DodgeSeconds` 0.4 |
|---|---|---|
| `SwordAndShieldAnimV1_Dash` | 0.733 s | **1.83×** |
| `SwordSwordAnimV3_Dash` | 0.833 s | 1.92× |
| `SwordAndShieldAnimV1_Roll` | 0.900 s | 2.25× |
| `SwordSwordAnimV3_Flip` | 0.967 s | 2.42× |

Length is the smaller argument. The larger one is that **the move type should match the duration
that play actually settled on.** 0.4 s was reached by feel, twice, before any animation existed —
and a full body roll compressed into 0.4 s is a complete rotation in under half a second, which
would read as frantic at any multiplier. A dash is natively a quick reactive evade, so at 0.4 s it
is being asked to do roughly what it already does.

The counter-argument is real and is being set aside rather than refuted: the earlier entry held
that a roll's commitment suits a move costing half the stamina bar, and that rolls are the genre's
readable idiom for i-frames. If the dash proves too weightless for a 50-stamina cost, that is a
finding, and the rolls are one montage away.

Root motion was enabled on all eight dashes, since like every `_RM` clip in the bundle they ship
with it off. Verified on disk rather than by read-back.

**The transferable lesson is about when this was catchable.** It was raised at the last cheap
moment — `AM_Dodge` had exactly one segment in it. The same question a day later costs rebuilding
eight sections. A decision recorded with confident reasoning is not the same as a decision whose
alternatives were actually enumerated, and this log cannot tell those apart on its own.

## 2026-08-10 — Animations play whole; visual consistency beats hypothetical feel

`AM_Dodge` is built from **entire, untrimmed roll clips**, so the animation and `DodgeSeconds`
line up exactly. At 0.4 s against 0.9 s clips that means a 2.25× play rate, and 2.25× is accepted
rather than engineered away.

This supersedes advice I gave twice earlier the same day, that the sections should be trimmed to
their "usable evasive portion" to keep the multiplier down. That was wrong in a specific and
instructive way: it proposed cutting the animation to hit a play rate nobody had looked at yet,
on a guess about how 2.25× would read. Trimming a clip to a hand-picked endpoint is not neutral —
it silently makes the *animator's* midpoint the design, and it does so before anyone has seen the
baseline it is supposedly fixing.

**The rule, stated generally: an animation plays in full across the mechanical duration it
belongs to.** It is fitted to that duration, never cut down to hit a number that has not been
felt. If the result feels wrong, change the duration — that is a single authored number with a
derived rate behind it — rather than hiding the problem inside the asset.

This does not weaken "the animation is not the balance authority", which still holds and is why
`DodgeSeconds` is authored and the rate derived. The two are complements: **mechanics decide how
long, the animation gets all of that time.** What is forbidden is the third option, where a clip
is quietly reshaped so a number stops being questioned.

The practical consequence: judge the baseline first, then deviate only if play demands it. Travel
distance is deliberately left alone for the same reason — it is unjudgeable until there is
something on screen to judge.

## 2026-08-10 — The dodge is 0.4 s, judged before it had an animation

`DodgeSeconds` went 0.5 → 0.3 → 0.4 in one sitting, settling at 0.4. 0.5 was a guess made before
any clip existed; 0.3 overshot.

**What makes this verdict unusually trustworthy is when it was taken.** There is still no
`AM_Dodge`, so the judgement was made against the pure mechanical duration — the lockout and the
i-frame window — with no animation to blame or to hide behind. Once a montage exists that signal
is gone for good, because a dodge that feels wrong can then always be read as the animation's
fault. Retuning before authoring was deliberate for exactly this reason, and it is worth
repeating for block and parry: **judge the timing naked, then build the animation to fit it.**

The order also protects the model. The play rate derives from `DodgeSeconds`, so had the montage
been authored first, the tempting fix for "this feels too long" would have been to speed the clip
up and leave the duration alone — which is the animation quietly becoming the balance authority,
the thing this project has now rejected three times.

The consequence to carry into authoring: 0.4 s against 0.9 s clips is a **2.25× play rate**. That
is inside the range the earlier entry warned about, and the lever if it reads fast-forwarded is
trimming the sections, not raising the duration back up. See the open question above.

## 2026-08-10 — No dodging in the air, and why it keys off state where the jump keys off action

Found in play: you could dodge while airborne. Not intended, so `GA_Dodge` now refuses to
activate while falling.

**This keys on the airborne *state*, not on having jumped** — so it also stops a dodge after
walking off a ledge. That is deliberately the opposite of the jump's regen pause immediately
below, which keys on the *action* and lets a ledge-walk cost nothing. The two look inconsistent
side by side and are not, because they answer different questions. The regen pause is a **cost**,
so it should only attach to something you chose to do; falling because you walked off a ledge is
not a choice and should not be billed. The dodge block is about what is **physically coherent** —
a ground roll in mid-air is nonsense however you got up there.

Implemented as `bBlockedWhileAirborne` on the shared ability base rather than a check inside the
dodge, so block and parry can adopt it with a checkbox when those exist. Left **off** by default:
an air attack is a legitimate thing to want later and this should not quietly forbid one.

Worth flagging against the "costs are paid, not required" rule, which this does *not* violate:
that rule is about stamina never refusing an action, not about state never refusing one. Dodging
is already blocked by `State.Attacking.Committed`, `State.Dodging` and `State.Exhausted`, and
being airborne is another such state. It also does not fall foul of "an input that silently does
nothing is the worst feedback available", because unlike an empty stamina bar, being in the air
is unmistakably legible to the player.

**Once input buffering exists this should probably become a defer rather than a refusal**, so a
dodge pressed just before landing fires on landing instead of vanishing. That is the shape that
makes the rule feel like timing rather than like a wall, and it is noted on the property.

## 2026-08-10 — Jumping is taxed in recovery, not in bar

Jumping costs no stamina and never will, but it suppresses regen from the launch until 0.5 s
after landing.

This is a third category the economy did not previously have. Defensive actions **cost**
stamina and pause regen; jumping only pauses. So a jump is never refused, never contributes to
exhaustion, and cannot be the thing that empties you — but spamming it holds the bar flat, which
matters precisely because the bar is what unlocks the defensive options. The cost of jumping is
paid in *when you get your stamina back*, not in how much you have.

Its tail is 0.5 s against a defensive action's 1 s, deliberately. A jump is a weaker commitment
than a dodge and should not be taxed as heavily; giving it its own number rather than sharing
`StaminaRegenPauseSeconds` is what keeps that adjustable.

**The pause is keyed to the jump action, not to being airborne**, and the distinction is the
whole rule. Walking off a ledge is not something you did, so it costs nothing — driving this
off `IsFalling()` would have taxed falling itself, which is both unintuitive and exploitable in
the wrong direction (it would discourage using terrain). For the same reason it hooks
`OnJumped`, the actual launch, rather than `Jump()`, which only records a button press: a press
that never becomes a jump — held against a ceiling, or pressed while already falling — must not
charge for something that did not happen.

Note this makes jumping while exhausted doubly locked: it is already refused by
`State.Exhausted`, so the pause cannot extend an exhaustion it is not allowed to start.

## 2026-08-10 — Exhaustion ends at full, and the bar was lying about the pool

Two changes from the first play session, one design and one bug they exposed together.

**Exhaustion now lasts until stamina reaches Max, not four seconds.** This supersedes the flat
`ExhaustionSeconds` in "Costs are paid, not required" below.

**Every exhaustion is identical, and that is the intended shape.** Stamina floors at 0, so
overspending does not exist as a concept: dodging at 50 and dodging at 3 both land on exactly 0
and recover at the same rate. An earlier draft of this entry claimed the change made the
punishment scale with how empty you ran yourself — it does not, and could not without letting
stamina go negative. That alternative was raised and rejected: a cost that does not appear on
the bar is invisible state, which is exactly how the base-value bug below stayed hidden.

What ending on recovery buys is the **exit condition, not the duration — and not protection of
any kind**. The old timer released you at roughly 62 stamina, the new rule releases you at 100,
and under either one you are free to spend straight back down to 0 and exhaust yourself again.
That is the system saying yes and stamina deciding; a rule that prevented it would contradict
"costs are paid, not required" directly. An earlier draft of this entry sold the change as
stopping exactly that, which was both factually wrong — nothing blocks a dodge the instant
`State.Exhausted` clears — and the wrong shape for this design.

The actual gain is that the exit state is *uniform and knowable* rather than an accident of
arithmetic. 62 was never a designed number; it was whatever `ExhaustionSeconds` and
`StaminaRegenPerSecond` happened to produce together. Retune regen to 10/s and that same four
second timer returns you at about 25 — exhaustion would hand back a bar nearly as empty as it
found you, and nothing would announce that the mechanic had stopped working. Ending at Max is
invariant to both numbers. It costs about 5.5 s rather than 4 s, and it removes a second number
that can disagree with the bar. `ExhaustionSeconds` is deleted rather than kept as a floor: nothing
refills stamina instantly, so a floor would be an unreachable branch, and this morning's cleanup
is the argument against leaving those lying around.

The consequence to hold onto: **regen continuing during exhaustion is now load-bearing, not
merely humane.** It was previously a kindness that stopped exhaustion being inescapable; it is
now the only mechanism that can end it. Anything that suppresses regen while exhausted makes
exhaustion permanent. Block is the obvious future hazard, since a held block keeps
`State.StaminaRegenPaused` applied and `ActivationBlockedTags` gates activation rather than
continuation.

**The bug: attribute *base* values were never clamped.** Regen ticks through
`ApplyModToAttribute`, which writes the base value, and only `PreAttributeChange` was
overridden — which guards the current value. So the base climbed past Max unbounded while the
bar read a correctly clamped 100. Measured live at base **105.33** against a displayed 100.

This is worth recording because of how it presented rather than what it was. The reported
symptom was "two dodges reach zero but do not exhaust, and a third dodge is wrongly allowed",
which reads as a bug in the exhaustion rule. It was not: the pool genuinely held more than the
bar showed, two dodges left a fraction rather than zero, and the HUD's `%.0f` rendered that
fraction as "0". Every layer was behaving correctly on the value it was given. It also means
the standing check "a dodge from full reads exactly 50" would have failed — it would have read
55.33 — so the check was right and had simply never been run.

`PreAttributeBaseChange` now clamps alongside the other two. All three are needed and they
catch different writes: current values, direct base writes, and instant effects.

**The debug auto-attacker resets to its spawn transform before each swing.** Attack montages
carry root motion, so a dummy attacking on a loop walked itself to the edge of the map. Zeroing
`AnimRootMotionTranslationScale` was rejected: suppressing the lunge shortens the dummy's
effective reach, and reach is the thing spacing tests measure — it would have quietly made the
open question about the dummy's mobility worse. Resetting *before* the swing rather than after
also means every attack starts from an identical transform, so repeated measurements compare.

**`TD.DebugCombatTiming` now defaults to on.** Every real bug in the timing model was found by
measuring, and this session added one more found by reading `baseValue`. It costs nothing when
nothing is attacking, and the standing advice to reach for it early is better served by not
having to reach at all. Turn it off when combat stops being the thing under test.

## 2026-08-10 — `CommitAbility` is gone from every ability

Removing `CostGameplayEffectClass` left the call that *checks* it in place. All three combat
abilities still opened with `if (!CommitAbility(...)) { EndAbility(...); return; }`, and the
dodge's carried a comment describing the gate as live: *"Failing here is the 'not enough
stamina to dodge' case."* That is precisely the behaviour the entry below reversed.

The calls were provably inert — `CommitCheck` tests cost and cooldown, and every ability has
both set to `None`, verified on the CDOs before removing anything. So this changes nothing at
runtime. It is worth doing anyway because the residue was a loaded trap: `Docs/Working-In-Unreal.md`
lists "an action that silently does nothing at low stamina means `CostGameplayEffectClass` has
crept back in" as a standing regression check, and a `CommitAbility` call sitting in
`ActivateAbility` under a comment endorsing the gate is the most likely way for it to creep.
Deleting the call makes the gate unrepresentable rather than merely unused — the same move as
collapsing three hold thresholds into one per-branch number so a threshold could not sit inside
a band.

**`EffectOnEnd` is now an unused hook.** It was added to carry the second half of the regen
pause; that half now falls out of `RegenSuppressedUntil` on the character being pushed forward
while the tag is present, so nothing needs applying when an ability ends. The property is kept
rather than deleted — it is a reasonable general hook and removing a `UPROPERTY` is a reflection
change — but its comment now says plainly that nothing sets it, because a doc comment claiming a
property carries a mechanism it no longer carries is exactly the drift this log exists to catch.

Two smaller corrections in the same pass. `DefaultEffects` was documented as "where stamina
regen lives" months after `GE_StaminaRegen` was deleted and regen moved into C++ forty lines
below it. And `GA_Dodge` had an empty `AbilityTags` while `GA_Attack` carries `Ability.Attack`;
the dodge now carries `Ability.Defend.Dodge` there too. `AbilityTags` is what
`CancelAbilitiesWithTag` and `BlockAbilitiesWithTag` match on, so an ability that cannot be
named cannot be cancelled — which block and parry will almost certainly want to do.

## 2026-08-10 — Costs are paid, not required; the system says yes and stamina decides

**No action is ever refused for want of stamina.** Dodging at 30 stamina works, empties the
bar and exhausts you. It does not silently fail.

This replaces GAS's `CostGameplayEffectClass`, which is a *gate* — checked in
`CanActivateAbility`, refusing the activation if the cost cannot be met — with an
`EffectOnStart` on the shared ability base that simply applies. The distinction is the whole
design: this combat is meant to be free-flowing and responsive, with **stamina management as
what unlocks the system's potential rather than what prevents you using it**. An input that
does nothing is the worst possible feedback, because it is indistinguishable from a dropped
input.

That only works if overspending is punished, which is why exhaustion could not be deferred
to last as originally planned. Until it existed there was no downside to anything.

**Exhaustion is a lockout on acting, not on recovering.** At zero stamina `State.Exhausted`
is applied for 4 s, blocking defensive actions and jump — but regen keeps running throughout.
Stopping regen too would make exhaustion inescapable rather than punishing.

**Any defensive action cancels an attack, not just block.** This supersedes the reading in
"2026-08-10 — The four questions gating defense, settled", which took `CLAUDE.md`'s "can
cancel attack startup into block" literally and wired dodge to be *blocked* by
`State.Attacking`. The boundary is unchanged — cancel until the attack commits, never after
— but it is now stated once, by a `State.Attacking.Committed` tag applied at the commit
checkpoint, which defensive abilities block on instead of `State.Attacking`. Adding block and
parry needs no new cancel logic.

## 2026-08-10 — The stamina economy is orchestrated in C++, not by GameplayEffects

Regen, its pause, and exhaustion live on `ATDCombatCharacter` rather than in a periodic
GameplayEffect with tag requirements.

**This is not a move away from GAS.** Attributes, tags and the ASC are unchanged, and the
AttributeSet was always C++ — GAS *is* a C++ framework. What moved is only the orchestration.
The framing of "GAS or C++" as alternatives was a mistake in the discussion that led here;
the real axis is what is *authored in a details panel* versus what is *code*.

The reason is concrete: UE 5.8 expresses a GameplayEffect's tag behaviour through
`gEComponents`, which cannot be scripted, and the inline containers accept writes while
reading back empty. So every tag-gated effect needs a human in the editor. The economy is
also a small state machine — a pause that outlives the action causing it, a timed lockout —
which is clearer as fifty lines of C++ than as three effects and their components.

Consequences accepted:

- The regen rate, pause and exhaustion duration are now `UPROPERTY`s on the character rather
  than effect assets. Still designer-tunable per Blueprint, but no longer swappable per
  effect, which would matter if different characters ever needed different economies.
- Regen watches for the pause tag rather than abilities telling it to stop. That is
  deliberate: an ability that is cancelled or interrupted cannot leave regen suppressed
  forever, because nothing is holding a flag it might fail to clear.
- `GE_StaminaRegen` and `GE_StaminaRegenPause` were deleted rather than left unused, since an
  effect that looks live and is not is worse than no effect at all.

## 2026-08-10 — I-frames last exactly as long as the dodge

`CLAUDE.md` says the dodge "grants i-frames for the duration", and it is now built that way:
one number, `DodgeSeconds`, with no separate i-frame window.

The rejected alternative was mine, and it had been built before anyone questioned it — an
authored `IFrameSeconds` of 0.3 inside a 0.5 s dodge, leaving a vulnerable recovery tail. It
was defensible on feel-goal grounds ("clear punish opportunities") but it was not in the
spec, and more damningly the two numbers had no stated relationship. 0.3 and 0.5 were both
guesses, and nothing said what made either right or what should happen to one if the other
moved.

So a whiffed dodge is **not directly punishable**. Its cost is 50 stamina and being unable
to act for the duration, and spam is bounded by the stamina economy rather than by
vulnerability. This is a real design position, not a simplification: it says the risk of
dodging is resource commitment, not exposure.

There is precedent for this working. Divine Knockout and New World both shipped dodges whose
cost was resource and commitment rather than exposure. New World's problem was not the model
but its erosion — accumulating enough ways to reduce or refund a dodge's cost that players
became uncatchable. That is a failure of *degree*, so it is recorded as something to watch as
the economy grows, **not** as a rule against ever discounting a dodge. Divine Knockout avoided
the question entirely by putting dodge on a flat 3-second cooldown.

If dodge proves too safe in play, the fix is a recovery window authored in **absolute time**
and i-frames derived as `DodgeSeconds - RecoverySeconds` — never a fraction. What makes
recovery punishable is how it compares to an attack's startup, and a fraction silently
shrinks the punish window below anything usable whenever the dodge is retuned faster. That
is the same reasoning that keeps the attack ladder in absolute milliseconds.

## 2026-08-10 — The dodge authors its duration and derives the play rate

The roll clips are 0.9 s; `DodgeSeconds` was guessed at 0.5 s before any clip existed. Rather
than accept the clip's length or re-cut the animation, the montage is played at whatever rate
makes the authored duration true — `MontageLength / DodgeSeconds`.

This is deliberately the same shape as the attack ladder, where a branch authors *when it
hits* and every play rate is derived at runtime. The alternative — treating the clip's length
as authoritative and tuning the design around it — inverts that, and would make the animation
the balance authority for the second time in this project. It was rejected for i-frames
earlier today for the same reason.

Two consequences worth stating plainly:

- **Rate changes duration, not distance.** A roll played at 1.8× covers the same ground in
  half the time. If the *travel* is wrong for our spacing, the knob is
  `AnimRootMotionTranslationScale`, which is a separate decision and still open.
- A large enough rate multiplier will read as fast-forwarded rather than snappy. If the
  number needed turns out to be extreme, that is evidence the roll is the wrong clip for a
  reactive defensive option, not evidence that the rate needs raising further.

## 2026-08-10 — Sword and shield, rolls for every evade, root motion first

**The melee archetype is sword and shield.** It is the largest set in the library (1,046
assets) and the only stance with a shield, which is what makes block's 180° forward coverage
read as a thing the character is doing rather than a rule the game is enforcing. Reach also
grows, which serves spacing — the top feel goal — and incidentally makes i-frames less
fiddly to test.

Chosen over staying unarmed, which was free but leaves block unmotivated and reach short,
and over one-handed sword, which ships **no evasive animations at all** — a mismatch on
precisely the move being built next.

The cost is real and lands on offense, which is currently verified and working: new attack
clips mean re-authoring the montage, re-placing the `Release Window` notify by hand (notifies
cannot be placed programmatically) and updating `ReleaseStartSeconds` to match. This is why
the swap is its own slice rather than something folded into the dodge work — the timing model
is the most expensive thing in the project to re-verify, and it should not be disturbed
halfway through building something else.

Note also that the library has **no shield mesh at all**, so the shield is a placeholder
primitive until one is sourced. That is a prop gap and does not affect the animations.

**Every evade is a roll, in all eight directions.** The library has no forward dodge in any
archetype, while rolls cover all eight — so the choice was between mixing two move types
under one button or committing to one. One move type wins: a dodge that is a sidestep in
four directions and a roll in the other four would need different i-frame windows, different
durations and different punish profiles depending on which way you pressed, which is a lot of
hidden complexity for a defensive option that has to be read instantly.

The accepted cost is that a roll is longer and more committal than a sidestep. That is a
tuning problem — `IFrameSeconds` and the recovery tail after it are exactly the knobs for it
— and it is arguably the right shape anyway, since dodge costs half the stamina bar and
ought to feel like a commitment.

**The mechanic keeps the name Dodge.** The tags, input and ability are all `Dodge`
(`InputTag.Dodge`, `State.Dodging`, `UTDDodgeAbility`); "roll" is what the animation is, not
what the move is. Renaming would churn the spec, the ini and the code to describe a clip.

**Displacement stays authored, via root motion.** Every dodge and roll in the library is
root motion and none is in place, so this is the default rather than a preference. Root
motion can be switched off on the montage and displacement driven in code without new
assets, so the cheaper experiment runs first; revisit if the distance proves untunable
against spacing.

## 2026-08-10 — The focus-return frame hitch reproduces; it is a trigger, not an anomaly

The windup-skipping hitch described under "2026-08-09 — Branches are described by when they
hit" happened again, on the same trigger: the first attack thrown after clicking into the
PIE window to return focus. It presents as a visibly stuttery light that deals no damage,
and the damage total confirms the mechanism rather than merely resembling it — three
subsequent attacks left the dummy at exactly 20 health, so the phantom attack contributed
zero, meaning its release window opened before any trace existed.

That entry called it "observed once", and its "not worth chasing yet" rested partly on
being unable to reproduce it. It is now reproducible on demand, which changes the
assessment even though the conclusion holds for now:

- It is still an **editor-only** trigger. Nothing has shown a hitch large enough to do this
  arising from gameplay, and a packaged build has no equivalent of returning window focus.
- It remains worth fixing eventually, because the failure is silent. An attack that arrives
  with no windup is unfair; one that arrives with no windup *and* no hitbox is merely
  confusing, which is why this has never cost anything yet.
- The fix, when it is worth doing, is to clamp the frame delta the ability reasons about,
  not to special-case focus. Nothing currently clamps it.

Do not treat a stuttery, damage-less first attack after clicking into PIE as a regression.
Anything else of that shape is not this.

## 2026-08-10 — The four questions gating defense, settled

**Block and parry keep separate buttons.** Block is hold-RMB, parry is MB4 or LAlt+RMB, and
each is active on the frame it is pressed. This supersedes the closing line of "2026-08-09 —
Ability input is routed by gameplay tag", which anticipated that they would share one;
`InputTag.Block` and `InputTag.Parry` were already authored separately in
`DefaultGameplayTags.ini`, so the shared-button claim was the outlier rather than the plan.

The tap-versus-hold resolution that works for the attack ladder does not transfer, and why
is worth keeping. An attack can defer its identity because a windup is playing either way,
so nothing is lost while the branch is undecided. Defense has no such runway: block must be
guarding on the frame it is pressed, and parry must open its window on the frame it is
pressed. Any scheme that waits to see which one you meant has already spent the time that
made pressing it worthwhile. Light/heavy/charged are a ladder of commitment; block and parry
are two different reactions to the same instant.

**An attack can be cancelled into block until it commits, not until it coils.** Punishment
is meant to attach to committing to a move, not to having wound one up. The commit
checkpoint is already the instant the attack resolves, so ending the cancel window there
reuses a boundary the model has rather than inventing a second one.

The alternative considered was ending it when the coil starts, on the grounds that a 700 ms
charge abortable on reaction makes the most committal attack the safest. What defuses that:
the cancel is into **block only** — never into another attack, a dodge, or a free action —
so an aborted charge has spent up to 700 ms achieving nothing and ends up holding a guard
that drains stamina. That is a real cost; it is simply not a punish.

**The minimum stamina economy ships with the first defensive feature**, rather than after
all three. Dodge costs 50 of a 100 pool, so without costs, regen and the regen pause there
is nothing to judge — the cost *is* the design. Exhaustion is deferred until a second
defensive action exists to be locked out of: with only dodge built, "exhausted" and "not
enough stamina to dodge" are one state wearing two names.

**Dodge is the first defensive feature**, being the most self-contained of the three, and
building it is what forces the stamina economy into existence.

## 2026-08-10 — The GA_LightAttack fallback is removed

`GA_LightAttack` was left on disk unreferenced when `GA_Attack` replaced it, as insurance
in case the branch model did not work out. It did work out and has been playtested, so the
fallback now insures against a system that is verified — which git history already does,
and better. Keeping a second ability answering `InputTag.Attack` also keeps the original
bug within reach: granting both fires both on one press.

`UTDMeleeAttackAbility` stays. It is the abstract base `UTDChargedAttackAbility` derives
from and carries the montage, trace and damage plumbing; only the Blueprint went.

Supersedes the closing note of "One ability with three branches, not three abilities".

## 2026-08-09 — Branches are described by when they hit; every play rate is derived

A branch authors `ReleaseAtSeconds` (when its hitbox goes live), `HoldUntilSeconds` (the
input boundary) and `ReleaseSeconds` (how long the hitbox lasts). Nothing authors a play
rate. Windup, coil, commit and release rates are all computed at runtime from the
montage's **measured** position.

The key inversion: **the shared windup runs at whatever rate the *fastest* branch needs**
— `ReleaseStartSeconds / Branches[0].ReleaseAtSeconds`, currently 1.44 — and slower
branches are produced by the coil holding them back, never by a later branch accelerating
to catch up. Authoring a per-branch commit rate was tried first and worked, but it made
the light visibly snap mid-swing and silently halved its active window, because a rate
chosen for the windup carried on through the release.

Deriving from measured position rather than assumed position is not a detail. Two separate
bugs came from assuming: a coil rate computed from where the coil was *meant* to start
overran the release window, so the notify fired before any trace existed and the charged
attack dealt no damage at all while still applying its tag.

Costs accepted, in rough order of how much they matter:

- **Timing lands within about one frame, biased late** — measured 253 / 509 / 769 against
  targets of 250 / 500 / 750. The release notify is detected on a frame boundary, so it
  can only ever be late.
- **A true freeze is now impossible.** Zero is floored out of every rate. If a design ever
  wants a genuinely held pose it needs a different mechanism, not a play rate.
- **`ReleaseStartSeconds` duplicates the notify's placement**, because notifies cannot be
  read off a montage and the windup rate is needed before any notify fires. The ability
  compares itself to reality when the window opens and warns on drift — mitigated, not
  prevented.
- **`CoilEndSeconds` must stay below `ReleaseStartSeconds`** and nothing enforces it at
  author time. Violating it reproduces the silent no-damage bug exactly.
- **Play rates are no longer readable from the asset.** What the montage actually does is
  a computation, which costs debuggability; the temporary `LogTDCoil` output is the
  substitute.
- **A long enough frame hitch skips an attack's windup entirely.** Observed once when
  clicking into the PIE window returned focus: a single frame advanced 0.333s, carrying
  the montage to 0.48 and firing the release window's begin and end 9ms apart, before the
  attack had even committed. Harmless in the editor, but an attack that arrives with no
  windup is a fairness problem if it can ever happen in play. Not worth chasing yet;
  worth remembering that nothing currently clamps a frame delta.

## 2026-08-09 — Reactability is measured from the tell, not from the press

A 500 ms heavy windup looks reactable if you read the number on its own. It is not,
because reacting to a heavy first requires knowing it is *not* a light, and nothing
distinguishes them until the coil appears. The window a defender actually gets is coil →
damaging: roughly 240 ms for the heavy, which sits right at the edge of human reaction.
The charged attack is genuinely reactable because its coil holds long past the point a
heavy would have committed, which is itself the tell.

Consequences worth holding onto:

- Lengthening a windup does **not** by itself make an attack more reactable. Moving
  `CoilStartSeconds` earlier does, and so does giving a branch its own animation.
- The shared windup is not only an authoring convenience — it is the mechanism that keeps
  the reaction window short. Giving a branch a distinct `MontageSection` later buys
  readability at the direct cost of that branch's ambiguity.
- The light is unreactable because it never coils at all, not because it is fast.

This corrects a wrong edit to CLAUDE.md made the same day, which downgraded the heavy to
"reactable" purely on the size of the number.

## 2026-08-09 — Documentation: a decision log, not per-system design docs

Per-system design docs were considered and rejected. Header comments in this codebase
already carry local rationale well, and a doc that describes a system drifts out of sync
with it — at which point it is worse than nothing, because it gets trusted over the
code. The concrete evidence: `WindupSeconds` sat in `TDChargedAttackAbility.h` documented
as "the most important number in the system" while nothing in the `.cpp` ever read it. A
system doc written alongside it would have repeated that error confidently.

A decision log degrades honestly instead: a dated entry about what was chosen stays true
even once the code changes. `Docs/Working-In-Unreal.md` was split out for the same
reason — it was previously held only in per-machine assistant memory, invisible to the
repo and lost on a machine change.

## 2026-08-09 — Attack phases are windup / release / recovery; coil sits inside windup

*Windup* is everything before damage, *release* is the period the attack can deal damage,
*recovery* runs from the end of the damaging phase to the end of the attack.

Coiling is deliberately **not** a fourth phase — it is a sub-state of windup, the portion
that is slowed while waiting for the commit checkpoint. It is visual feedback, so its
tuning values are named `Coil*` rather than after a phase.

Recovery has no code yet; it is currently just the montage tail, with the ability ending
on montage completion. It gets real values when endlag and punish windows land.

The `UAnimNotifyState_MeleeWindow` class marks exactly the release phase and by rights
should be renamed, but placed notifies serialize against the class path — a rename needs
an `ActiveClassRedirects` line and would break the montage's existing notifies, which
cannot be re-placed programmatically. Only the editor-facing `DisplayName` was changed.

## 2026-08-09 — Windups are preset, not resolved at the moment of release

Previously the attack resolved the instant the button came up, so a 251 ms hold produced
a heavy that came out at 251 ms. Light was strictly dominated: there was no reason ever
to tap. Now the windup runs to a preset length, and holding through a checkpoint
escalates to the next tier and its longer windup. Releasing early inside a band changes
nothing.

That delay *is* the heavy's cost, and it is what buys light a reason to exist. The
consequence to accept is dead time: release at 260 ms and nothing visibly happens until
the attack commits.

Because a band's edge and the previous tier's windup length are the same instant, each
branch needs only one number. `MinHoldSeconds`, the ability-level `WindupSeconds` and
`MaxHoldSeconds` collapsed into a single per-branch `WindupSeconds`, which makes the old
bug — a threshold sitting inside a band — unrepresentable.

## 2026-08-09 — The coil creeps toward a ceiling rather than freezing

A hard freeze at the coil point would guarantee every branch identical impact frames, but
reads as a statue over a long charge. A slow creep reads as tension instead. Left
open-ended the creep is a bug: at `CoilPlayRate` 0.1 a charged hold advances the montage
past the first impact frame, so the attack would fire with part of its active window
already spent.

`CoilCeilingSeconds` caps how far the coil may advance, just short of the first active
frame. Every branch keeps its full release window; only long holds ever reach the cap.

## 2026-08-09 — One ability with three branches, not three abilities

`GA_Attack` (a Blueprint of `UTDChargedAttackAbility`) replaced `GA_LightAttack` rather
than joining it — both answer `InputTag.Attack`, and granting both fires both on one
press. `GA_LightAttack` is left on disk unreferenced as a fallback.

The ability's own asset tag is the generic `Ability.Attack`. Which of the three it turns
out to be is decided at commit time, so it cannot be an authored tag; C++ applies
`Ability.Attack.{Light,Heavy,Charged}` as a loose tag while the swing runs. That loose
tag is also how the debug HUD reveals which attack was actually thrown — the intended
verification method, rather than inferring it from the animation.

## 2026-08-09 — One shared animation for all three attacks

An attack's identity is a *consequence* of how long its windup lasted, so it cannot be
known up front to pick a clip. Branches carry an optional `MontageSection` for when one
earns its own animation later; until then all three share the strike.

## 2026-08-09 — Ability input is routed by gameplay tag

`UTDGameplayAbility::InputTag` is matched against a `UInputAction → FGameplayTag` map on
the character, so adding an ability is a content change rather than a code change.
`InputTag.*` is deliberately a separate namespace from `Ability.*`: the input is not the
move. One press of `InputTag.Attack` resolves to light, heavy or charged depending on how
long it is held, and block and parry will share a button.
