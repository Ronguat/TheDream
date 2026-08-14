# Combat decision log

What was decided, why, and what is still open. Newest entries at the top.

This file records **reasoning**, not implementation. Code and Blueprints are the
authority on how things work and what the numbers currently are; entries here explain
why the shape is what it is, and stay true even after the code moves on. An entry is
never rewritten to match new code — a reversal gets a new entry that supersedes it.

Append an entry whenever a gameplay or combat choice is made that a future reader could
reasonably second-guess.

**The bar, raised 2026-08-11: does this record something the code cannot?** In practice that
means **what was rejected, and the mechanism by which it failed** — because code contains only
the winner. "We built an authored `IFrameSeconds` inside a longer dodge and dropped it", "we
authored per-branch commit rates and the windup rate carried through the release and halved the
light's active window", "we raised letting stamina go negative and rejected it": none of that is
recoverable from a codebase, and every one of them is a proposal somebody will make again.

If an entry would be equally true as a header comment, **write the header comment instead** —
code cannot drift from itself. Audited on the day this bar was set: 9 of the first 27 entries
cleared it; the rest explained the current design, which the codebase does better.

**How to read this file.** The sections above the first dated entry — **known traps**, the
**tuning map**, **what has been superseded**, **retired item numbers** and **retired names** — are the working part, and
they are short on purpose. The dated entries below are an archive. Read the working sections
when starting a slice; grep the entries when you want to know *why* something is the shape it
is. Do not read it front to back.

**On the fact that it only grows.** Reviewed 2026-08-11 and kept deliberately. **The growth is
proportionate, not pathological** — a codebase accumulates decisions as it accumulates code, and
a file recording the ones worth keeping grows alongside it. A decision log that stopped growing
would mean the project had stopped being built. What has to stay bounded is the *working*
sections above, not the archive.

Compacting superseded entries was considered and rejected: all seven supersessions to date are
*partial* —
each kills one claim inside an entry whose other claims are still live — so stubbing them would
destroy current reasoning to save bytes nobody pays for. The archive is reached by search, not
by reading, so its length costs approximately nothing, and git already holds anything that did
get removed. Length is not the risk here; **a stale claim that does not announce itself is**,
which is what the table below exists for.

---

## What has been superseded

A new entry says what it supersedes. The old entry does not say it has been gutted — so reading
one directly can hand you a dead number with full confidence. Check here before trusting any
dated entry. Add a row whenever an entry supersedes part of an older one.

| This entry | …has a claim that is now wrong | Corrected by |
|---|---|---|
| 2026-08-09 — Ability input is routed by gameplay tag | block and parry will share one button | 2026-08-10 — The four questions gating defense |
| 2026-08-09 — One ability with three branches | `GA_LightAttack` is kept on disk as a fallback | 2026-08-10 — The `GA_LightAttack` fallback is removed |
| 2026-08-10 — The four questions gating defense | only *block* can cancel an attack's startup | 2026-08-10 — Costs are paid, not required |
| 2026-08-10 — Costs are paid, not required | exhaustion lasts a flat 4 s (`ExhaustionSeconds`) | 2026-08-10 — Exhaustion ends at full |
| 2026-08-10 — Jumping is taxed in recovery | the jump's tail is half the defensive pause | 2026-08-10 — The defensive regen pause is 0.5 s |
| 2026-08-10 — Sword and shield, rolls for every evade | every evade is a roll | 2026-08-10 — The evade is a dash, not a roll |
| 2026-08-10 — No dodging in the air | once buffering exists the refusal should become a defer | 2026-08-11 — The input buffer remembers a press |
| 2026-08-10 — Facing is camera-relative | the stock ABP plays a forward run while strafing | corrected **inline** in that same entry — nothing was decided on it, so it was a factual error rather than a reversal |
| 2026-08-11 — PvP is the destination | `CLAUDE.md` still lists netcode as out of scope "and that stands" | corrected **inline** in that same entry, within the hour — the user withdrew the scope call once it was restated back to them |
| 2026-08-11 — PvP is the destination | 14 network-unaware `SetTimer` sites | corrected **inline** in that same entry — the real figure is **2**; the count swept in Epic template code, debug timers, and one that must stay local |
| 2026-08-10 — Facing is camera-relative | locomotion ships from `SwordAndShieldAnimV1`, and mixing packs must be avoided | 2026-08-11 — V3 becomes the base stance (V1 reads as permanently guarding, and V1 has no `Hit`/`Death` clips so mixing was never avoidable) |
| 2026-08-11 — Dodge travel ships at the clips' authored distance | the eight directions agree, so one uniform scale is the right shape and no per-direction data is needed | 2026-08-11 — V3 becomes the base stance (true of V1's clips, false of V3's: spread 90.6 uu) |
| 2026-08-11 — The training dummy gets the sword too | the blade's length is an authored number, `BladeLengthCm` | 2026-08-12 — A hitbox is authored, not traced (the *principle* survives and is why it generalised: authored beat mesh-derived, then authored volumes beat authored blades) |
| 2026-08-11 — Dodge travel ships at the clips' authored distance | reach is unmeasurable because the trace follows `hand_r` and nobody knows where that socket is at the impact frame | 2026-08-12 — A hitbox is authored, not traced (reach is `MaxReachCm`, an authored number, so the dodge-versus-reach comparison is now simply readable) |
| 2026-08-11 — The light is reactable at 250 ms | prefer scaling root motion over code-driven movement; code is the exception to pay for knowingly | 2026-08-12 — Root motion scaling is not enough control (play says a multiplier only amplifies the animator's curve; the netcode reason survives and is answered by GAS root motion *sources*, not by hand-rolled movement) |
| 2026-08-12 — The facing unlock is asymmetrical with the lock | facing fades into and out of its lock, over 50 ms in and half of recovery out | 2026-08-12 — The real cause was an engine default (the fades were deleted the same day: any value below full authority disables the snap, so chained attacks never caught the camera) |
| 2026-08-12 — Two facing-fade bugs | the chained-attack sluggishness was caused by the fade suppressing the snap | 2026-08-12 — The real cause was an engine default (`bAllowPhysicsRotationDuringAnimRootMotion`; removing the fade did not move the bug) |
| 2026-08-12 — The attack montage hovers because it is bound to the wrong skeleton | the whole entry — the skeleton was never the cause | 2026-08-12 — The hover was six centimetres of mesh offset |
| 2026-08-12 — A hitbox is authored, not traced | the hover is "cosmetic, filed rather than fixed" and shared by every *root-motion* montage | 2026-08-12 — The hover was six centimetres of mesh offset (it is shared by every pose, root motion irrelevant, and it is fixed) |
| 2026-08-10 — Sword and shield, rolls for every evade | the library has "no shield mesh at all" | **Never superseded by an entry — corrected in `Docs/Animation-Library.md`**, which records both the mesh (`Shield_Heater`) and why it was missed: it carries no `SM_` prefix, so a prefix-filtered search returned nothing. Found still uncorrected here by the 2026-08-12 audit. |
| 2026-08-10 — The dodge is 0.4 s, judged before it had an animation | if the rate reads fast-forwarded, the lever is "trimming the sections" | 2026-08-10 — Animations play whole (same day, and it names this as the advice it supersedes; the row was never added). The rule is now also in `CLAUDE.md`: fit the clip to the duration, change the duration if it feels wrong. |
| 2026-08-11 — The light is reactable at 250 ms | facing snaps on movement input and turns smoothly at rest | 2026-08-12 — Facing becomes one rate (one derived rate in all states, plus an idle rate) |
| 2026-08-12 — Attack displacement is two scales | displacement is two multipliers on the clip's root motion, and a branch can only differentiate the travel its clip performs after commit | 2026-08-12 — Lunge is two authored distances (both scales are deleted; displacement is authored in centimetres and the clip contributes nothing, so what a branch can differentiate no longer depends on the clip at all) |
| 2026-08-12 — Root motion scaling is not enough control | the likely answer is that Lunge takes `RootMotionScale` to 0 and owns displacement outright | 2026-08-12 — Lunge is two authored distances (taking the scale to 0 does **not** work: animation root motion suppresses root motion sources whether or not it is scaled, so the character stops moving entirely. The montage must carry no root motion) |
| 2026-08-12 — Lunge is two authored distances | Lunge added two distances and **zero** timing values, because the boundaries it needed already existed — recorded as a virtue | 2026-08-13 — The gate is per tick, and lunge duration is a designed quantity (play found the lunge simultaneously too slow and too far, which is one fact: `ReleaseSeconds` was setting a movement feel) |
| 2026-08-13 — Target Lock | the clamp shortens the authored distance before the lunge starts | 2026-08-13 — The gate is per tick (pre-shortening bakes in a prediction; a retreating target became unreachable, which is worse than no system at all) |
| 2026-08-13 — Target Lock | the aim half corrects by the *minimum sufficient angle*, never a snap to centre | 2026-08-13 — Target Lock's rotational half aims the lunge (minimum-sufficient was measured against the damage wedge and is therefore always zero; the deadzone that replaced it protected leading, which this game does not have) |
| 2026-08-13 — Target Lock | the aim half is **post-commit only**, because the windup is where the player steers | 2026-08-13 — Target Lock's rotational half aims the lunge (homing runs *through* the windup at the existing turn rate and stops at commit — the player's authority moves from facing to target selection, which is why the wedge is read from the camera) |
| 2026-08-11 — Dodge travel ships at the clips' authored distance | displacement comes from the montage's root motion, corrected per direction by MeasuredTravelCm | 2026-08-13 — The dodge stops reading displacement off its clips (both the scales and the measurements are deleted; all eight directions travel DodgeTargetDistanceCm) |
| 2026-08-13 — The gate is per tick, and lunge duration is a designed quantity | the per-tick gate is the whole answer to a lunge arriving at a body — it "can only ever subtract travel", so the authored distance is the only ceiling needed | 2026-08-14 — The lunge stops on a hit (a pause is not a stop: the gate reopens when the body stops existing, so a killed target is slid through. The gate's own reasoning survives untouched — this is a second mechanism, not a correction to it) |
| 2026-08-10 — Exhaustion ends at full | recovery speed is one number, so `StaminaRegenPerSecond` *is* the exhaustion duration | 2026-08-14 — Exhaustion recovers at its own rate (the rate is split; exhaustion still ends at Max and nowhere else, so the entry's actual claim survives — what changed is which number sets how long it takes) |
| 2026-08-13 — Target Lock's rotational half aims the lunge | the wedge is per branch and *is* the contract — "aim inside it and the body ends at 0 degrees of error", authored per tier | 2026-08-14 — The homing wedge follows the ladder (it was per branch only at commit, while homing ran every tier on branch 0's; the heavy's and charged's values did nothing and had never been observed. The contract is real *after* this fix, not before it) |
| 2026-08-13 — Target Lock's rotational half aims the lunge | a 12.9° camera error arriving at commit as 0.0° is offered as the mechanism working | 2026-08-14 — The homing wedge follows the ladder (the measurement is real and unchanged, but it was taken on a light, where branch 0's wedge is the correct one — it says nothing about the heavy or charged, which is how the defect passed a play-verification) |
| 2026-08-13 — Target Lock's rotational half aims the lunge | reach is authored per branch — "author it longer in reach than the damage wedge", roughly this branch's lunge plus its damage reach | 2026-08-14 — Aim assist reach is derived (reach is no longer authorable at all; the struct has no reach field. Travel plus damage reach plus one shared `AimAssistMarginCm`, so the *estimate* in that entry was right and hand-authoring it was the mistake) |
| 2026-08-14 — The homing wedge follows the ladder | the three authored numbers are live placeholders to be authored by feel, and reaches must be kept non-decreasing by hand | 2026-08-14 — Aim assist reach is derived (same day: reach stopped being authored, so there is nothing to feel out per branch and monotonicity became unrepresentable. Only the margin is a feel number now) |
| 2026-08-14 — Aim assist reach is derived | the margin is 200, "signed off but unfelt" | **Played the same day and settled at 100**, giving reaches of 550/650/750. No entry supersedes it — the derivation and the margin's meaning are unchanged, only the number. `GA_Attack`'s CDO is authoritative; treat the 200 in that entry as a dated measurement. |
| 2026-08-12 — Lunge is two authored distances | `CoilTurnRateDegrees` is "600, the user's value" | **Unresolved as of 2026-08-14, and flagged rather than corrected.** The C++ default is **300** and `git log -S` shows it has never been anything else in code, so either it shipped at half the value named or `BP_PlayerCharacter` overrides it. Not readable without an editor; filed as a check in `CLAUDE.md`'s Current Focus. The entry's *reasoning* — that the rate is safe at any value including zero — is unaffected either way. |

---

## Known traps, indexed by what sets them off

Latent defects and unverified assumptions in code that **already exists**, each filed against
the slice that makes it bite. Re-read this when starting that slice, not at session start — a
flat list read once is forgotten by the time it matters.

**That re-read is now a step in `CLAUDE.md`'s working loop rather than a request made here**, because
asking politely did not work: a trap discharged during Attack Swap sat filed for a day afterwards and
was found by a documentation audit, not by anyone starting the slice it was filed against. Grep this
section for the item's name before measuring anything.

These are not design questions. Nothing here needs play to settle; they need checking.

**Discharge a trap in the same commit that fixes it.** This section is the most load-bearing part
of the file and the only one with no natural expiry: a trap fixed by someone not reading this file
stays here and misdirects the next person, which is worse than never having filed it. Say what
discharged it and keep anything from it that is still true — the Slice B entry became a note about
the client path being unexercised rather than simply disappearing. Removing a trap silently is the
one edit here that cannot be reviewed, because nothing is left to review.

**Mid-Attack-Swap — *reach has changed and the placed spacing has not.*** Filed when the trace moved
onto the blade, and **still live after 2026-08-12's swap to authored hitboxes**, which is the
third time reach has moved in two days. `MaxReachCm` is now the authored answer per branch, so
the *re-judge `TraceRadius`* half is discharged — that property no longer exists — but the
placed-spacing half is not. Re-check the training dummy's distance in `L_CombatTest` against the
new wedges, and remember an automated PIE run is one fixed spawn: no damage from a scripted swing
says nothing about hit detection.

*This replaces a trap filed hours earlier claiming the trace connected with nothing. It was
wrong — see the diagnostic note below, which is the part worth keeping.*

**Sharply worse as of 2026-08-12, and now the blocking item rather than a background one.** Lunge
authors travel outright, and the first values played put a light at **400 cm of travel** against a
dummy placed at **200 cm** with a `MaxReachCm` of **150**. Attacks now overshoot the target by more
than its entire standing distance. Reach and the placed spacing have to be re-authored *together*
with the lunge distances, because all three are one felt quantity — and until they are, no spacing
verdict from this level means anything.

**Half-answered 2026-08-13 by Target Lock's clamp, and the half that remains changed shape.**
Overshoot is no longer possible — travel is clamped to stop short of a body, so the failure the
paragraph above describes cannot occur. What replaced it is the opposite reading: with the clamp in,
the *authored* distances barely run at the placed spacing at all. Measured over six attacks at
200 cm, the base lunge ran in full (~100 cm, the sweep correctly finding nothing in reach of it) and
the branch lunge was clamped **200 → 0** every time, because the attacker was already 13.5 cm from
contact and inside the 40 cm standoff. A light that authors 300 cm of travel is now performing 100.

So the numbers still need re-authoring together and the trap stays open, but the question is no
longer "how do we stop overshooting" — it is **what the authored distances should be given that the
clamp is what actually decides them at close range.** That is the reach/travel/spacing pass, and it
now has a working clamp to be tuned against rather than being tuned around a defect.

*And the placed spacing is 200 cm, which this trap said all along.* A reading of 175.8 was taken on
2026-08-13 and reported as a correction to it; it was measured in a live PIE world where the dummy
had drifted 24 cm during play. The trap was right and the correction was wrong. See
`Docs/Working-In-Unreal.md` on why a PIE-world transform is not a placed transform.

**The hit-detection half is discharged 2026-08-14, and it was never broken.** Re-measured in a fresh
PIE with nobody moving, placed transforms confirmed in the *editor* world first (dummy `x=200`,
`PlayerStart x=0`): damage lands in exact multiples of the authored 15, and `TARGET release` reads
**118.7 / 117.1 / 123.0 cm** against the 124 the geometry predicts — 42 capsule radius + 40 standoff +
42 radius. The branch lunge measured **70.8 cm in 43 ms**, or 1646 cm/s against an authored 1667. The
clamp does exactly what it claims and the ladder connects at the placed spacing.

*The "clamped 200 → 0 every time" reading above is **withdrawn** — an instrument fault, not a
finding, and not a decision that needs superseding.* Both it and a second attempt on 2026-08-14 computed the attacker's closing distance against
the target's **placed** origin while the target had been shoved across the floor by the very attacks
being measured — the player finished one run at `x=-110.75`. This is the PIE-transform trap wearing a
different hat: it is usually stated as "do not read an actor's own transform out of PIE", and the
general form is broader — **do not measure one actor's travel against another actor's assumed
position.** A moving reference frame reads as a movement fault in the thing being measured.

**What remains open is only the authoring**, and it is a design question rather than a defect: what
the distances *should* be, given the clamp is what decides them at close range. Reach, travel and the
placed spacing are still one felt quantity, and two tiers still play the light's clip.

**~~Open bug — the character hovers while a root-motion montage plays.~~ Discharged 2026-08-12.**
It was never about montages, root motion, skeletons or clips. The mesh component sat at Z **−90**
under a **96** capsule half-height, so the feet floated exactly **6 cm** in every pose on both
characters. `ABP_Combat`'s foot-IK Control Rig spent those 6 cm of correction every frame absorbing
it, which is why the offset was visible only where that IK does not run: inside montages, and in
mid-air. Fixed in `ATheDreamCharacter`'s constructor, beside the `InitCapsuleSize` it has to agree
with. Full account in the dated entry below.

Four things from the hunt are kept here because they are still true and outlived the bug:

**An assumed control is worse than no control.** The `RootMotionRootLock` hypothesis was first
killed by "the dodge has the same setting and does not hover" — and **nobody had ever checked
whether the dodge hovers.** A report that the hover does not occur *during locomotion* was silently
converted into *the dodge is fine*: two different claims about two different systems, one observed
and one assumed. A comparison case only disconfirms if the case was actually *measured*; an assumed
control carries the authority of evidence while being a guess. It corrupts not the conclusion but
the test used to reject a conclusion.

**Before testing whether a symptom depends on X, test whether it depends on anything at all.** This
is the one that would have ended the hunt on the first day. The very first measurement correctly
established *pose, not translation* — actor Z was constant to four decimals through every montage —
and the next question should have been "does it hover with nothing playing?" rather than which
animation property caused it. **The dummy hovers in the level viewport, statically, with no PIE.**
A static symptom deserves a static test first, and the cheapest instrument in the project turned
out to be looking at the level.

**A sufficient explanation is not the actual one**, three times on this bug: the Epic skeleton
binding, `RootMotionRootLock`, and the `IsSlotActive` wiring were each true, each capable of
producing the symptom, and each not the cause. The pattern is only broken by manipulating the
suspected cause and watching the symptom fail to move.

**Build a montage *from its clip*, never empty-then-assign**, so the skeleton is right by
construction. The rename came with it — one montage serves light, heavy and charged, so
`AM_LightAttack_01` was always a misnomer and `AM_Attack` matches `GA_Attack`.

**Before the charged or heavy gets its own clip, and during Lunge + Recovery** — *the coil is a freeze, and
this is now measured rather than predicted.* The 2026-08-12 entry "The coil has no room on a short
clip" predicted a coil play rate of 0.03–0.05× from the arithmetic. Measured the same day across
~40 heavy and charged throws on the current timings: **`rate=0.049` to `rate=0.097`**, mean around
0.072. The montage advances at 5–10% speed for the whole coil, which is a freeze with a slight drift
rather than a slowdown.

**The 2× spread between throws is itself the diagnosis.** The rate is derived from the montage
distance *remaining* to `ReleaseStartSeconds`, and that distance is only about 0.06 of montage — so
a single frame of jitter in where escalation fires (observed positions 0.2265 to 0.2533) swings the
derived rate by a factor of two. It is a small difference between two nearly-equal numbers, which is
what "no room" looks like in the arithmetic rather than in the animation.

Nothing is broken by it and no warning fires — the ungated *coil skipped* and *notify drift* checks
were both silent across every throw, and escalation lands at 151–169 ms against an authored 150,
about a frame late as the spec claims. **What it is, is the concrete argument for the charged and
heavy getting their own clips**, which is otherwise only argued on appearance: a longer clip with a
later damage point is the only thing that gives the coil somewhere to live. Expect this to surface
during Lunge + Recovery, since re-authoring reach and travel per tier is when bespoke clips would land anyway.

**~~Whenever the ladder's reactability is judged — heavy and charged run ~70 ms longer than their
authored numbers, and the coil is where it goes.~~ The reactability half is discharged 2026-08-12:
the hitbox is on time on every tier.** Measured from the trace's own world clock, press to
`RELEASE BEGIN`: light **202–207 ms** against an authored 200, heavy **506–508** against 500,
charged **751–754** against 750. The heavy carries the *same* +6 ms bias as the light, which never
coils — so the coil is not delaying anything, and heavy and charged are **not** more reactable than
authored. Whatever runs long sits after the damaging window, which is the outcome the trap called
harmless.

That is the question it asked, answered: *"if the hitbox genuinely goes live ~70 ms after
`ReleaseAtSeconds` claims, heavy and charged are more reactable than authored… nobody has separated
those two."* Separated by comparing press-to-release across tiers, which is what it proposed.

**Not fully closed, and deliberately left here:** the *total* overhead was never re-measured,
because the trace has no ability-end line and nothing prints one. So this records where the
overhead is **not**, rather than what remains. Anyone wanting the total needs to add the line first.

The original measurement, kept because it is the evidence: totals against arithmetic — light
authored 0.75 s and measured 0.758–0.771 (**+0.01**), heavy authored 1.10 and measured 1.172
(**+0.07**), charged authored 1.45 and measured 1.515–1.525 (**+0.07**).

**Recovery is not the source** — it was measured directly in the same runs and lands within 8 ms of
authored at every tier. The light is the tier that never coils and it is the tier with no overhead,
which is what points at the coil. It also predates authored recovery: charged measured 1.17 against
an arithmetic 1.10 before any of this existed.

**And the inference from it was wrong, which is the part worth keeping.** "The light is the tier
with no overhead and the tier that never coils" pointed at the coil, and the coil was innocent —
the light also had no overhead *because it starts closest to its own release*, and both facts have
the same cause without one causing the other. The measurement that settled it did not compare tiers
at all; it compared each tier against its own authored number. **A correlation across tiers
suggested the mechanism, and only a per-tier absolute measurement could test it.**

**Whenever the light's `HoldUntilSeconds` changes** — *`TurnRateDegrees` is derived from it and
nothing enforces the link.* The rate is 180° ÷ the light's commit time, which makes 1200 the slowest
value that can always bring facing round before the attack's wedge freezes. Move the commit and the
guarantee lapses silently: the character simply starts committing attacks partway through a turn,
which is invisible without the `FACING LOCK` trace and was measured at 71% of flick-attacks landing
outside their own wedge before it was found. Recompute both together.

**That number now has a second dependent, added 2026-08-12 — the base lunge ends on the same
boundary.** `UTDChargedAttackAbility::GetBaseLungeDurationSeconds()` returns `Branches[0].HoldUntilSeconds`
rather than an authored copy, deliberately, so *that* link cannot drift. But it means one value now
sets three things: when the tiers become distinguishable, how long the shared lunge runs, and the
turn rate that must close 180° inside it. Two of the three are derived in code and safe; the turn
rate is the one still copied by hand.

**A consequence worth knowing before it surprises someone:** the maximum rotation possible during
the base lunge is `TurnRateDegrees × HoldUntilSeconds` = exactly **180°**, by construction. A
corkscrew lunge is therefore impossible without changing one of those two numbers, and a *lower*
turn rate during that window would break the aim guarantee rather than tame anything. This was
raised as a proposed cap and rejected on those grounds.

**Whenever an attack montage is swapped, or any new ability drives a root motion source** —
*animation root motion suppresses root motion sources completely, and scaling it to zero does not
help.* `UCharacterMovementComponent::PerformMovement` says so in its own comment: *"Animation root
motion overrides Velocity and currently doesn't allow any other root motion sources"*, and the
branch it guards is `if (HasAnimRootMotion())` — which stays true no matter what
`SetAnimRootMotionTranslationScale` is set to.

So a montage carrying root motion produces **zero** lunge, not a doubled one. Measured 2026-08-12:
the character stood perfectly still through a dozen charged attacks while every authored distance
read back correctly. The fix is that `AM_Attack` plays the library's in-place (`_IP`) clip rather
than the `_RM` one — same length to four decimals, same skeleton, so no notify drift.

**This is enforced in code, not by memory**: `StartAttackMontage` logs an ungated warning when
`AttackMontage->HasRootMotion()` and a lunge distance are both non-zero. Trust the warning over
this paragraph, and never "fix" a suppressed lunge by scaling something.

The reason it is worth a trap rather than a comment: **the failure is silent and the obvious remedy
is the wrong one.** Zeroing the animation's contribution is what everyone tries first, it reads as
correct, and it produces a character that does not move at all — a symptom easily blamed on the new
system rather than on the old one still winning.

**Before Light String — a buffered attack aims where the camera is when it *fires*, not when it was
pressed.** Filed 2026-08-12, from play. The `FACING LOCK` trace reads ±0.0° and is *correct*: the
body is aligned with the camera at commit. What it cannot see is that the commit happened up to
**~440 ms after the press** — buffer wait plus the 150 ms windup — and the camera moved during it.

So the aim guarantee holds and the attack still goes somewhere the player did not choose. **A clean
`FACING LOCK` is not a clean bill of health**; it answers an angle question, and this is a time
question. Neither trace shows it alone — `BUFFER` knows the lateness, `FACING LOCK` knows the angle,
and nothing correlates them. Logging the camera yaw delta between press and commit would.

**It scales with the lunge**, which is what promotes it from a curiosity: at 400 cm of travel you
now go the wrong way, where before you only swung the wrong way.

**The user's worst-case test, to run when Light String lands** — a three-light chain in 1vX, first
attack on target A, second on target B 180° away, third back on A. That is the case where camera
movement between press and commit is both maximal *and* deliberate, rather than the flailing that
surfaced it. Deferred on the judgement that good-faith play against normally-moving targets does not
reach it; the chain test is what would falsify that.

**Two defensible behaviours, and only one has been felt:** aim at activation (current — the attack
tracks your latest intent) or aim latched at press (the attack honours the moment of decision).
Most action games do the first. This one is a spacing game, and the first is what felt wrong.

**Unexplained, filed 2026-08-12 — one burst of per-frame activation attempts that never
reproduced.** During airborne-input testing, `REFUSED … airborne` fired on **15 consecutive frames**
(world 13.710–13.842, ~8.8 ms apart). Everywhere else in that log and in a deliberately heavier
follow-up test, refusals sit **110–210 ms** apart, which is a human mashing.

**Two explanations were killed by evidence, and no third is offered.** Held input repeating: the
ability input binds `ETriggerEvent::Started` / `Completed`, once per press. A buffered press
retrying each tick: no `stored` line within four seconds either side, and no `fired` or `expired`
closing it out. No project code calls `CanActivateAbility` except the override that logs.

**It is not a correctness problem** — every refusal in the burst was the correct answer, and the
airborne rule is verified working. What it costs is instrument trust: a per-frame event drowns
low-frequency ones inside any capped `GetLogEntries` window, which `Docs/Working-In-Unreal.md`
already files as a way logs lie about absence.

**What would identify it next time, and is now in place:** the `REFUSED` line carries the avatar's
name. It did not before, and because abilities are `InstancedPerActor` the player's and the training
dummy's instances are *both* legitimately `GA_Attack_C_0` — so the log could not say whose refusal
it was, and "was this the dummy?" could not be ruled out. That gap is what made the burst
unresolvable rather than the burst itself being subtle.

**Whenever an ability's input binding is changed** — *`IA_Attack` carries an `InputTriggerDown`,
which holds the action in the Triggered state every frame the button is down.* Nothing spams today
only because the C++ binds `Started` and `Completed`, which fire once each per press. Rebinding to
`ETriggerEvent::Triggered` — an ordinary-looking change — would produce a per-frame activation
attempt, a per-frame refusal trace, and per-frame buffer churn, for as long as the button is held.
The asset and the binding have to be read together; neither is wrong alone.

**Whenever `LungeStandoffCm` or any branch's `MaxReachCm` moves** — *the two are coupled and
nothing enforces it.* The clamp stops a lunge `LungeStandoffCm` before contact, so the attacker
finishes at `84 + LungeStandoffCm` centre-to-centre. Reach is measured to the target's body, so the
attack lands only while `LungeStandoffCm < MaxReachCm`. Above that the clamp **causes** whiffs by
parking the attacker outside the hitbox it just aimed — silently, and looking exactly like a
hit-detection fault rather than a spacing one.

Today's values leave a wide margin (40 against 150) and the realistic way to break it is to tune
reach *down* during the reach/travel re-author and not think of this. Note the asymmetry that makes
it easy to miss: **standoff is per ability and reach is per branch**, so one number has to stay below
the smallest of three that are edited in a different part of the same asset.

**Before Light String and before Stun** — *knockback and the next attack's travel are one budget, and
nothing connects them.* A displacement that pushes a target further than the following attack can
cover — today base 100 + branch 200 + `MaxReachCm` 150 — makes a chain structurally unable to link.
It will read as the combo being broken rather than as knockback being too strong, because the number
that has to change lives in a different system from the symptom.

Filed before knockback exists rather than after, since this is exactly the kind of value that gets
tuned in isolation for impact feel and then breaks something two slices away.

*Two things that make it less urgent than it sounds, both from the user, 2026-08-13.* The per-tick
gate already handles displacement natively — a knocked-back target is the retreating-target case,
which is the one the gate was rewritten to serve, so linking survives being pushed. **The
pre-shortened clamp we deleted would have been actively hostile here**: knockback pushes them out,
the travel was already zeroed on their behalf, and the link becomes impossible. Good thing it went
before knockback arrived rather than after.

And the constraint binds *finishers and heavies* rather than the linking hits, because lights are
intended to have low knockback by design. **That intent is recorded as illustration, not
commitment** — the user was explicit that they did not want to promise the pattern yet: lights link
into each other, dropping a chain leaves both parties awkwardly close in a scramble, and finishing it
is a knockdown into oki. Useful as a worked example even if it turns out not to be the design.

**Whenever `LungeStandoffCm` is tuned — it is also the spacing of every linked exchange.** Filed
2026-08-13, following from the low-knockback intent above. If lights barely displace, each hit in a
chain re-closes to exactly the gate distance, so two characters sit pinned at 84 + `LungeStandoffCm`
(currently 124 cm) for the whole chain. The number will be *judged* on the slide it was authored to
fix and *felt* mostly as how far apart people stand while trading — one number, two jobs, and the
second is the one nobody will think of while tuning it.

**Decided 2026-08-14, and the trap is milder as a result.** The lunge now stops on a hit against a
viable target, so this pinning only describes an exchange in which nothing connects. Where hits *do*
land, final spacing depends on where in the release window the hitbox caught them, so a chain
breathes rather than sitting still — which is the property this note said would be traded away, now
bought. The number still does two jobs and the second is still the easy one to forget; what it no
longer sets is the spacing of a *connecting* chain.

**~~Whenever any branch's `AimAssistWedge` is authored — reaches must not decrease across the
ladder.~~ Discharged 2026-08-14, hours after being filed, by making it unrepresentable.** Reach is no
longer authored: it is derived as `base lunge + branch lunge + branch damage reach + AimAssistMarginCm`,
so it is a constant plus the branch's own lunge, and lunges increase up the ladder. A later branch
reaching less than an earlier one cannot be expressed.

**The sharper half went with it.** The case that actually bit was `MaxReachCm` of 0 — *disabled*
rather than narrow — which switched homing off partway through a hold. Derived reach is never 0, so
that route is gone too; `FTDAimAssistWedge::bEnabled` is now the only way to turn a branch's assist
off, and it defaults to *on*.

*The rule this trap protected is now in the type system rather than in a list, which is the outcome
to prefer wherever a constraint can be made unrepresentable. Kept as a record because the reasoning —
that a shrinking wedge drops a target already locked — is still why the derivation must stay
monotonic if anyone changes the formula.*

**Before the first multiplayer slice — *aim assist reads a loose tag across the network boundary,
and loose tags do not replicate.*** Filed 2026-08-13, found by the traps grep before building the
rotational half rather than after.

`UTDDodgeAbility` applies `IFrameTag` with `AddLooseGameplayTag`, which is local to the machine that
calls it. **Damage gets away with this** because `HandleTraceHit` is authority-gated, so only the
server ever asks. **Aim assist does not**: `FindAimAssistTarget` runs on both machines, so an
attacking client cannot see a remote opponent's `State.Dodging` at all. The client would steer onto
a dodging target the server skips, and the attack points two different ways.

So this is the loose-tag trap in a new and worse place — the existing entry warns that a client's
`CanActivateAbility` can pass a check the server failed, and this is the same defect deciding
*geometry* rather than permission. Unreachable today, single-player only. The fix is the standing
one: **decide on the server, apply everywhere** — a replicated property whose `OnRep` applies the
tag, following `bDead` / `bExhausted`.

**~~Before Block — exhaustion can become permanent.~~ Closed 2026-08-14 as a non-defect.** The
mechanism is real and unchanged: `ActivationBlockedTags` gates activation, not continuation, so a
guard held through zero keeps `State.StaminaRegenPaused` applied, and regen is the only thing that
ends exhaustion. **It is not a trap because the player is holding the button.**

The user's call, and it is the general form worth keeping: **a state you are choosing to stay in, with
the exit always available, is not a deadlock.** Holding block at zero accomplishes nothing — anything
actually blocked breaks the guard — so a player doing it is griefing only themselves, and releasing
both ends it and starts recovery. Nothing needs defending against.

*This replaces a discharge claimed earlier the same day, which said the trap was fixed by making
suppression not apply while exhausted. That fix shipped and play threw it out within hours: it closed
the bounded cases along with the unbounded one, so a dodge that exhausted you began regenerating
during its own duration, refunding the cost of the action that emptied the bar. **The pause is a cost
of acting and exhaustion is not a refund** — see the dated entry.*

**The related edge is untouched and still filed**: the stamina delegate only fires on a *change*, so
a cost applied at exactly 0 changes nothing and cannot retrigger exhaustion. Narrow today, since costs
clamp at 0 and the 100→0 transition does fire.

**And `ExhaustedStaminaRegenPerSecond` can still make exhaustion permanent in one keystroke.** Zero is
not "no recovery while exhausted", it is a character locked out of every defensive action for the rest
of the match — and unlike the held guard, *that* one has no exit the player can reach. The
`ClampMin="0.01"` is load-bearing rather than tidy, and the reason lives here as well as in the header
because a clamp with no stated reason is the kind of thing a later edit removes.

**Before the first real multiplayer test — the ASC's client path is written and unexercised.**
Slice B shipped 2026-08-11 and its three filed traps are discharged: the PlayerState's 1 Hz net
update frequency is raised in `ATDPlayerState`'s constructor, the character re-resolves its ASC
in `OnRep_PlayerState` as well as `PossessedBy`, and `ATheDreamGameMode` now sets
`PlayerStateClass`. What is *verified* is the server path only — single-player PIE has no client,
so `OnRep_PlayerState` never fired in any test that passed. It is the half that is hardest to
reason about and the only half nothing has run.

A fourth trap was **not** filed in advance and is the one worth carrying forward as a pattern
rather than a fact: *the ordering hazard was invisible from the design and obvious from the
engine's callback order.* Seeding was guarded by one bool on the character, and a player pawn's
`BeginPlay` runs **before** it is possessed — so the flag would have been spent on the fallback
ASC and the PlayerState's real one never seeded. The player would have had no attributes and no
abilities **while the never-possessed training dummy worked perfectly**, which is what would have
made it hard to find. The guard now lives with the ASC (`ATDPlayerState::HasSeededDefaults`).
The general form: **when one code path serves two lifecycles, the one that works is not evidence
about the one that does not** — and here the working one was the simpler, so it would have been
tested first.

**Whenever new state is added, from 2026-08-11 on** — *a loose gameplay tag does not replicate,
and this is now a PvP project.* `AddLooseGameplayTag` is local to the machine that calls it. If
the caller is authority-only — anything driven by an attribute delegate, which are all bound
behind the authority gate — clients never see the state at all, and their own
`CanActivateAbility` will happily pass a check the server has already failed. Use a replicated
property whose `OnRep` applies the tag locally, following `bDead` / `bExhausted`. The rule is
**decide on the server, apply everywhere.**

**Before any multiplayer slice** — *i-frames have no lag compensation, and the dodge is 400 ms of
invulnerability.* The immunity check reads the target's ASC on the server. A client dodges at T
and the server learns at T+RTT/2; an attack resolving inside that gap ignores a dodge the player
has already watched begin. This is the "I dodged that!" complaint and this design can least
afford it — i-frames last exactly as long as the dodge, with no vulnerable tail to absorb the
disagreement. Also unsolved: no prediction windows despite every ability being `LocalPredicted`,
and **2** network-unaware `SetTimer` sites — `TDChargedAttackAbility`'s checkpoint and
`TDDodgeAbility`'s duration.

*Recounted 2026-08-11; the original figure of 14 came from a module-wide grep.* There are 13, and
**six are in `Variant_Combat/`** — Epic template code this project explicitly does not derive
from. Of our seven, four are debug-only (auto-attack press/release/reset, the debug revive) and
one is the buffered-release replay, which is *local input* and by this project's own rule
deliberately must **not** be networked. Note the correction does not shrink the work: the dodge
timer is the i-frame problem in this same paragraph, which is the hardest item on the list. **A
count taken across a whole module measures the module, not the debt** — the same filtered-view
error the absence rule exists for, in its counting form.

**~~Before Attack Swap~~ — *the melee trace follows `hand_r`.***
**Discharged 2026-08-12, twice over.** The trace moved to the blade on 2026-08-11, and on
2026-08-12 the whole socket-following approach was replaced by authored `FTDAttackHitbox` wedges
— so `TraceSocket`, `BladeAxisLocal`, `BladeStartCm`, `BladeLengthCm`, `BladeTraceSegments` and
`TraceRadius` are all deleted. Reach is `MaxReachCm`, authored per branch.

Kept because it is still true and no longer has a home: **the values were tuned against a fist
standing in for the whole hitbox**, so nothing carried forward from them numerically. The
starting wedges are a fresh guess and have never been played.

**~~Before a second `Release Window` notify exists (Attack Swap, or Light String)~~ — *the melee trace opens
on any `Event.Melee.WindowBegin` reaching that ASC.*** **Discharged during Attack Swap, and it sat here
filed for a day after being fixed** — found by the 2026-08-12 doc audit, not by anyone reading this
section. `UAbilityTask_MeleeTrace::IsWindowForThisAttack` now filters on **both** edges, deliberately:
a foreign montage's window *ending* must not close ours, which would truncate an active swing rather
than merely failing to start one. A null montage still means accept-any, which is the pre-item-6
behaviour kept for abilities that do not set one.

**That this survived is the argument for the rule above it.** The file says discharging a trap in
the same commit that fixes it is the most load-bearing habit here, and this is what breaking it
looks like: a fixed defect still warning the next reader off a fix that already exists. Light String
was its trigger and would have hit it.

**~~Before Recovery~~ — *recovery is shorter than it looks, by exactly the montage's blend-out.***
**Discharged 2026-08-12** by `FTDAttackBranch::RecoverySeconds`, which settles it the first way the
trap offered: **recovery ends at blend-out**, and the authored duration is measured to that
boundary, with the montage warped to fit. The blend still plays afterwards and is still
mechanically over; it is now follow-through by design rather than by accident.

Two things from it are kept because they are still true. **Mechanical and visible recovery differ**,
and always will under this resolution — a spectator sees 0.25 s more attack than the attacker is
committed to. And the blend-out is still the true cause of the debug attacker resetting before its
swing looked done, which was patched with a delay rather than diagnosed.

**And one thing the trap did not know, found the same day and worth more than the rest:** *the
blend-out boundary is not a fixed montage position — it moves with the play rate.* With
`BlendOutTriggerTime` negative, the engine begins blending when the montage's **remaining time at
the current rate** equals the blend duration, so halving the rate halves the distance from the end.
Anything deriving a rate against a fixed `Length - BlendTime` is right only at rate 1.0. See the
dated entry; the correct solution is `R = (Length - Position) / (RecoverySeconds + BlendTime)`.

**~~Re-check `InputBufferSeconds`, which was sized against a recovery nobody chose.~~ Checked
2026-08-12, and it is now a *watch* rather than a re-check.** Recovery was tuned to 0.40 / 0.50 /
0.60 and played, and the window did drop exactly one input — a light tap expiring 260 ms after
press while chaining. **Left at 0.20 deliberately**: the user did not feel it, and there is no
value that is simply correct, because a buffer long enough to never drop a tap during a 0.75 s
swing queues an attack most of a swing ahead. Full account and the arithmetic in the dated entry.

**What makes it live again**, in rough order of likelihood: recovery tuned longer still, the light
string making rapid tapping the primary input pattern, or a dropped input reported in normal play.
Read the `BUFFER` trace before touching the number — `expired` is a window question, and no line at
all means the press never reached the character. Its ceiling is set by the longest lockout the
design refuses to shorten, which is exhaustion; see the 200 ms entry.

**The third trigger fired 2026-08-12: 13 dropped presses out of 88 buffered, about one in seven.**
All expiries clustered at **256–306 ms after press**. Not caused by the lunge — attack durations
were untouched — but by tapping faster than the 0.75 s attack cycle, hard enough that *every* press
in the session was buffered. Left at 0.20 pending a decision, because the session was deliberately
abusive rather than representative.

**Two things that measurement clarified about the number itself.** First, the window runs from the
**release**, not the press: while the button is down the expiry is pushed forward every tick
(`ExpiryWorldTime = Now + InputBufferSeconds`), and that stops on release. A 90 ms tap therefore
gets ~290 ms of real grace, which is why the expiries cluster where they do rather than at 200.

Second, and this is the consideration the original entry did not have: **the window is also a cap
on how stale a buffered attack's aim can be.** Lengthening it drops fewer inputs *and* allows more
aim drift; shortening it does the reverse. See the buffered-aim trap above. There is no value that
is simply correct because the same number is answering two questions that want opposite answers.

Note this replaces the trap that stood here until 2026-08-11 — that every timing verdict was
confounded by inputs which never registered. That was Input Buffer's whole justification and it is
discharged.

**Before the first multiplayer slice, added 2026-08-12, rewritten the same day** — *the attack
facing lock is local state on the character and does not replicate.* `FacingTurnScale` and its
two fade fields are plain floats, not even `UPROPERTY`, set by `SetFacingAuthority` from the
ability. **This does not meet the project's own rule that new state is a replicated property or
an attribute.** It is recorded as a knowing exception rather than an oversight.

Why it is nonetheless *not* a teardown: `SetFacingAuthority` is the correct API either way, and
what a networked version changes is who calls it and whether the decision replicates. That is an
extension. Under `LocalPredicted` the owning client runs the ability and so applies the lock
itself, and the server has authority and replicates rotation, so the two machines that matter
agree.

**The first draft of this trap said "harmless today" and that was the easy read, not the checked
one.** What actually made it harmless was that a simulated proxy has no `Controller`, so
`UCharacterMovementComponent::PhysicsRotation` returns before `bUseControllerDesiredRotation` can
do anything — an engine implementation detail nobody here chose, propping up a rule this project
had already written down. `UpdateCameraRelativeFacing` is now explicitly guarded to locally
controlled or authority, so the correctness no longer rests on that.

**The general form, which is the part worth carrying:** *"it works" and "it is guarded" are
different claims, and a system that only works is one refactor in someone else's code away from
not working.* The same distinction the Slice B entry draws between the server path being verified
and the client path merely being written.

**A second local-state exception joined it 2026-08-12: `bAbilityMovementLocked`**, driven by
`UTDGameplayAbility::bLocksMovement`. Same category, same knowing exception, recorded here rather
than as a second trap because it is the same fact about the same character. It is *input*
suppression, so the machine that owns the input is the one that must honour it — and a simulated
proxy has no input to suppress, which is why it is more clearly local than the facing lock is.

Still owed when multiplayer is real: the fade is a float changing every frame, so replicating the
*value* is wasteful — replicate the decision (this attack is in its lock phase) and let each
machine run its own fade. That is "decide on the server, apply everywhere" again, but it is not a
third copy of the `bDead` / `bExhausted` pattern, because what replicates is a phase rather than
a bool.

**Whenever `MaxWalkSpeed` changes** — *it is coupled to the blendspace's top row and nothing
enforces the link.* `BS_SwordShield_Locomotion` places its run samples at Speed 500 because
`MaxWalkSpeed` is 500. Change one and the character tops out partway up the blend, playing a
permanent half-walk at full speed, with no error anywhere. Change both together.

Note 500 was inherited from Epic's template and **still has never been measured** against what
the `Run` clips are authored for — 3c shipped without visible foot sliding, which makes it
acceptable rather than correct. The `_RM` variants encode the authored displacement, so this
stays measurable rather than a matter of taste. See the tuning map's foot-sliding row.

---

## Tuning map — a verdict comes back, which knob moves

**The right-hand column is the point.** Each row is a place where the obvious-looking fix is
the wrong one, usually because it would quietly make the animation the balance authority or
silently shrink a window under a later retune. Read the row before reaching for a number.

The *questions* — does the ladder feel right, is the dodge too safe — are the user's and are
kept in their own notes. What belongs here is only what to move once a verdict arrives.

| Feels wrong | Move this | **Not** this |
|---|---|---|
| Dodge reads fast-forwarded | `DodgeSeconds` | The clip. Trimming sections to drop the play rate makes the animator's midpoint the design, and does it before the baseline has been felt. |
| Dodge travels too far or short | `AnimRootMotionTranslationScale` on the montage task | The play rate. Rate changes *duration*, never *distance* — a faster dash covers the same ground in less time. |
| Dodge is too safe | A recovery window in **absolute** time, i-frames derived as `DodgeSeconds - RecoverySeconds` | A *fraction* of the dodge. What makes recovery punishable is how it compares to an attack's startup, and a fraction shrinks the punish window below usable whenever the dodge is retuned faster. |
| An attack is too reactable, or not enough | `CoilEndSeconds`, or moving where the coil starts | The windup length. Reactability is measured from the **tell**, not the press — a longer windup with the same coil changes nothing. |
| The snap-to-camera pop reads badly | **Nothing — the snap is gone.** Facing is one smooth rate in both states as of 2026-08-12 | *(This row used to forbid always-smooth on the grounds that it sends dodges sideways. That was wrong: a dodge resolves its direction relative to facing and travels relative to the same facing, so lag cancels. Disproven in play.)* |
| Attacks do not land where the player aimed | `TurnRateDegrees`, and check the `FACING LOCK` trace for the error at commit — **but a clean trace does not exonerate**, see the buffered-aim trap | The wedge's `ArcDegrees`. Widening the arc to cover a facing that arrived late hides an aim bug behind a bigger hitbox, and does it in every direction at once. |
| `TurnRateDegrees` feels too fast or slow | Nothing, without re-deriving it. It is 180° ÷ the light's `HoldUntilSeconds`, the slowest rate that always arrives before the wedge freezes | Lowering it for feel. Below the derived value there are flicks the character cannot finish, and the attack silently points somewhere the player did not aim — which is what 500 was doing to 71% of flick-attacks. |
| The character spins on the spot like a prop while standing around | `IdleTurnRateDegrees`, freely — it cannot affect aim, because the fast rate resumes at the press and the whole windup runs on it | `TurnRateDegrees`. The three rates exist separately so this complaint has somewhere safe to go; answering it with the derived rate trades a cosmetic problem for a hit-detection one. |
| A held heavy or charged tracks too hard, or feels too committed, once it coils | `CoilTurnRateDegrees`, freely and at **any** value including zero — the coil is after the aim guarantee is discharged, so everything it governs is tracking rather than aiming. It is a **power** value even so: it is exactly how far a held attack may be redirected *after* the defender has been told it is coming | `TurnRateDegrees`, and not the coil's length either. The light never reaches this rate at all — it commits where the coil would start — so a complaint about the *light* turning wrong is never this row. |
| An action feels like it turns too slowly to start | Whether `IsIdle()` is wrongly returning true for it — every ability and every buffered press should already exclude it | `IdleTurnRateDegrees`. Raising it to fix one action's start hides a classification bug and drags the idle look back toward the pop it was added to remove. |
| Feet slide during locomotion | `MaxWalkSpeed`, set from the `_RM` clips' measured displacement | The animation's rate. 500 came from Epic's template and was never measured; derive the speed from the clip rather than scaling the clip to an unchosen number. |
| An action feels unresponsive at low stamina | Nothing — find what is gating it | Adding or restoring a cost gate. Costs are paid, never required; if an input silently does nothing, `CostGameplayEffectClass` or `CommitAbility` has crept back in. |
| Exhaustion feels too long or short | `ExhaustedStaminaRegenPerSecond`, since recovery *is* the duration. Separate from the normal rate as of 2026-08-14 | A duration knob, and no longer `StaminaRegenPerSecond` — that governs normal play only, and moving it to retune exhaustion now changes dodge cadence instead. `ExhaustionSeconds` stays deleted: splitting the *rate* keeps the property that killed it, because exhaustion still ends at Max and nowhere else. |
| An attack slides past a target after killing it | Nothing — the lunge stops outright on a hit against a viable target, as of 2026-08-14 | `LungeStandoffCm`. The gate *pauses* while a body is in the way and resumes when one is not, so a corpse losing its capsule is not a case any standoff distance can express. Lowering it to hide the slide shortens every lunge that never hit anything. |
| A tier assists onto targets it should not, or misses ones it should catch | That branch's `AimAssistWedge` — live from its escalation onward as of 2026-08-14, so it is finally observable | Branch 0's wedge, unless the complaint is about the *light* or about the span before the first boundary. That one governs every tier until escalation, so widening it to fix the charged silently widens all three in the one span that must be indistinguishable. |
| Aim assist locks on from too far, or gives up too early | `AimAssistMarginCm` — one number for every tier, meaning "how far past my hit range assist still selects" | A per-branch reach. There isn't one: reach is derived from that branch's travel and damage reach, so it follows a lunge retune automatically. Re-authoring it by hand is what produced two values that had never done anything. |
| Assist reaching further than an attack can hit feels wrong | Nothing — that gap **is** the design | Shrinking the margin to zero. A wedge matching hit range turns lock-on into a free rangefinder and makes assist answer whether you were in range, which is the one thing Target Lock forbids it. |
| The drawn aim wedge does not match the tier being thrown | Nothing — it follows the ladder now. Read `AIM WEDGE` in the trace rather than judging the radius | Your eyes. Wedge sizes cannot be read out of a viewport; that is exactly how two never-observed values got authored and committed. |
| An input still feels dropped, with buffering on | `InputBufferSeconds` — but read the `BUFFER` trace first and find out whether it was stored, fired or expired | The attack's own timings. A press that expired unfired is a question about the window; moving `ReleaseAtSeconds` to compensate tunes the ladder around an input problem and hides it. |
| An attack reaches too far or not far enough | The branch's `MaxReachCm` | The animation, the clip choice, or the play rate. Reach stopped being a property of the art on 2026-08-12; if a swing looks like it should reach further than it does, that is an argument for changing the number *or* the clip, and only the number is balance. |
| An attack hits things beside you that it visibly missed | `ArcDegrees`, or skew `ArcCentreDegrees` toward the side the blade crosses | `MaxReachCm`. Narrowing reach to fix a coverage problem shortens the attack everywhere to fix it in one direction. |
| Attacks feel like they clip through you at point blank | `MinReachCm` — but expect it to feel worse | Nothing else. A hole at the centre is authorable and is almost always wrong: the attacker's own body is already there, so the case is rarer than it seems. |
| The facing freeze reads abruptly going *in* | **Nothing — the freeze is a hard lock by design.** `FacingLockFadeSeconds` was the answer here for one day and is deleted; see retired names | A fade that scales rotation authority, which is what that property was. Any value below full authority disabled the snap branch that then existed, so the fade did not soften the handoff, it replaced the whole lock with smooth turning. |
| An ability's direction can be steered when it should be committed | `SetAbilityFacingLocked(true)` for its duration | `bAllowPhysicsRotationDuringAnimRootMotion`. Turning it back off fixes one ability by re-breaking every other, which is how the dodge got a committed direction it never declared. |
| Control returning after a swing reads abruptly | Where the lock *ends* — it now runs to `EndAbility`, and the idle rate handles the catch-up gently when nothing else is happening | An interp on the rotation rate. It was the obvious fix and turned out to be unnecessary twice over: the two rates already cover both cases, and the failure modes that killed the original fade were artifacts of a snap branch that no longer exists. |
| An attack is too punishable, or not punishable enough | The branch's `RecoverySeconds`, in absolute seconds — it *is* the punish window | The clip, its length, or any play rate. Recovery stopped being a property of the animation on 2026-08-12; the montage is warped to fit the number, never consulted about it. |
| Recovery does not last what it is authored to | Whether something else set the montage rate after `RELEASE OFF` — the trace prints the derived rate and the blend-out boundary it solved for | A correction factor on `RecoverySeconds`. The boundary moves with the play rate, so a fudge tuned at one recovery length is wrong at every other; the rate-dependence is solved for in `ComputeRecoveryPlayRate` and any residual error is a different bug. |
| An attack does not close enough ground | `UTDMeleeAttackAbility::LungeDistanceCm`, the base lunge every tier shares | The clip, and **not** a branch's value if the complaint is about the whole ladder. The base lunge is the only displacement that exists before the tiers can be told apart. |
| One *tier* does not lunge far enough | That branch's `LungeDistanceCm`, which runs from the commit checkpoint to the end of the release window | The base lunge. Raising that to lengthen one tier lengthens all three, and does it in the one span a defender must not be able to read a tier from. |
| The lunge jerks or stalls at the commit boundary | The **ratio** between the two distances. Speed is `Distance ÷ Duration`, so the seam is continuous when `D_branch = D_base × (T_branch ÷ T_base)` — today 1.333× | Either distance alone. Each one sets a speed, and it is the mismatch between them that is felt; changing one without the other moves the discontinuity rather than removing it. |
| A lunge travels a different distance than it is authored | Whether the montage plays an in-place (`_IP`) clip, and whether a strength curve averages 1.0 | The distance. Animation root motion suppresses root motion sources outright, so a montage with root motion produces *no* lunge at all, and a curve whose mean is not 1 scales the distance silently. Both are settings, not tuning. |
| The character floats, sinks, or its feet do not meet the ground | The mesh component's relative Z, which must be the negative of `InitCapsuleSize`'s half-height | Anything in the animations. Clip settings, root motion, root lock and skeletons were all investigated and all innocent; the offset is static and visible in the level viewport with nothing playing. Check it there before opening a single animation. |
| Feet look right while moving but wrong during attacks | The same mesh Z — a discrepancy that only shows inside montages is foot IK masking it everywhere else | The montage or the clip. `ABP_Combat`'s Control Rig silently absorbs a constant offset, so "only montages are wrong" means "only montages lack the correction". |

Add a row whenever an entry below establishes that a fix belongs in one place rather than
another. That is the reusable part of an entry; the argument around it is not.

---

## Retired item numbers

**Items were numbered 1–15 until 2026-08-12 and are named now.** The numbers were stable
identifiers that carried no meaning, so every cross-reference cost a lookup — and the traps above
had already started writing *"before block (item 7)"* on their own, which is what a scheme looks
like when it has stopped paying for itself. Names keep the property the numbers were chosen for:
**a name does not change when the order does.**

**The 39 references below this line still say "item 6".** Dated entries are never rewritten, so
this table is how they stay readable. Do not renumber anything to match them.

| Entries say | Item is now | |
|---|---|---|
| item 1 | **Attack Ladder** | done |
| item 2 | **Dodge** | done |
| item 3 (and 3b, 3c) | **Sword & Shield** | done; the letters were sub-points, not items |
| item 4 | **Death** | done |
| item 5 | **Dodge Distance** | done |
| item 6 | **Attack Swap** | done |
| item 7 | **Block** | |
| item 8 | **Input Buffer** | done |
| item 9 | **Light String** | |
| item 10 | **Parry** | |
| item 11 | **Stun** | |
| item 12 | **Recovery** | ships with Lunge as one slice |
| item 13 | **Lunge** | ships with Recovery as one slice |
| item 14 | **Structure Audit** | **widened and re-scoped 2026-08-12** — see below |
| item 15 | **Settings** | |

**Item 14 is the one row that is not a straight rename.** It was "a structural audit of what is
designer-facing"; it is now the project's structure entire, and it is triggered by the combat model
being verified good rather than holding a position in the order. One consequence for anyone
following an old reference into it: **anything deferred to item 14 on *polish* grounds is pointing
at something that is no longer a polish slice**, and needs checking rather than assuming a home.

The one known case turned out to be already resolved. **Facing interpolation** was parked against
"the polish audit, item 14" in two header comments and the tuning map, and it is **done** — see the
2026-08-12 entry on holding the lock through recovery. The lock running to `EndAbility` plus
`IdleTurnRateDegrees` covers the case an interp would have smoothed, which the commit that shipped
it says outright. Nothing is owed.

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
| `TraceSocket`, `BladeAxisLocal`, `BladeStartCm`, `BladeLengthCm`, `BladeTraceSegments` | Deleted 2026-08-12. An attack's volume is `FTDAttackHitbox`, authored in the attacker's frame. |
| `TraceRadius` (per branch and per ability) | Deleted 2026-08-12. Reach is `MaxReachCm`; there is no thickness knob, because a wedge has no thickness. |
| `GetAttackTraceRadius()` | `GetAttackHitboxes()` |
| `AM_LightAttack_01` | **`AM_Attack`**, rebuilt on GDHBundle's skeleton 2026-08-12. One montage serves all three tiers, so the old name was always a misnomer. |
| `bAttackFacingLocked`, `SetAttackFacingLocked()` | `bAbilityFacingLocked`, `SetAbilityFacingLocked()` — the dodge uses it too. |
| `FacingLockFadeSeconds`, `FacingUnlockRecoveryFraction`, `FacingTurnScale` | Deleted 2026-08-12. Facing is a hard lock, and the smoothing these were meant to provide **proved unnecessary** rather than deferred — the lock running to `EndAbility` plus `IdleTurnRateDegrees` covers the case they would have smoothed. |
| `StationaryTurnRateDegrees` | **`TurnRateDegrees`**, renamed 2026-08-12 when facing stopped having a separate moving mode. No longer stationary-only, and no longer cosmetic — it decides where an attack points. |
| `bSnapFacingWhileMoving` | Never shipped. A temporary A/B switch for the facing pass, deleted with the snap branch it selected. |
| `RecoveryPlayRate` | **`FTDAttackBranch::RecoverySeconds`**, 2026-08-12. Recovery is authored as a duration per branch and its rate is derived, as windup and release already were. A rate could only set the punish window indirectly, through however long the clip's tail happened to be. |

---

## 2026-08-14 — The regen pause survives exhaustion, and a held button is not a deadlock

Shipped and reverted the same day. The bypass — exhaustion ignoring `State.StaminaRegenPaused`
entirely — lasted a few hours and play threw it out.

### What it got wrong, and the shape is worth naming

The bypass existed to stop a held guard stalling the only condition that ends exhaustion. That
problem is real but it is specifically about an **unbounded** suppressor. A dodge's own duration and
the 0.5 s tail are **bounded**: they expire on their own and cannot stall anything. Bypassing all
suppression closed the bounded cases for free, and the user felt it immediately — a dodge that
exhausted you began regenerating *during the dodge*.

**The pause is a cost of acting, and exhaustion is not a refund.** If anything it is the state where
that cost should bite hardest, since the whole point is that emptying the bar hurts. Verified from
play rather than argued: *"the rates themselves feel great"*, and this one thing did not.

The generalisable error: **a fix aimed at an unbounded case that also swallows the bounded ones.**
The bypass was written as "ignore suppression while exhausted" when the actual requirement was
"do not let suppression run forever" — a much narrower statement, and the difference is exactly the
cases a player can feel.

### Why nothing replaced it, which is the interesting half

The obvious repair was a narrower bypass, or a cap on how long suppression may hold while exhausted,
or making Block's guard break responsible for ending the ability. The user rejected all of it and
dissolved the problem instead:

> allow players to keep blocking at 0 stamina, and obviously their guard gets broken if they actually
> "block" anything, so they're doing nothing but griefing themselves by not releasing block

**A state you are choosing to stay in, with the exit always available, is not a deadlock.** That is
the rule to carry forward. The mechanism that looked like permanent exhaustion needs the player to
hold a button that accomplishes nothing, and one key-release both ends it and starts recovery. It
needs no guard, no cap, and no coupling to Block's design — which is a strictly better outcome than
any of the three fixes, because each of those would have added a mechanism to defend against a
player's own choice.

Note the asymmetry that makes this safe, and it is what distinguishes this from the
`ExhaustedStaminaRegenPerSecond = 0` case that *is* still guarded: a held guard has an exit the
player can reach, and a zero regen rate does not.

### What it cost to find

Nothing structural, but it is a clean example of why the closedown rule exists. The bypass shipped
with a confident header comment, a discharged trap, and three documents restating it as a rule —
all written the same afternoon, all wrong within hours, and none of it caught by review because the
reasoning was internally consistent. **Only play disagreed.**

---

## 2026-08-14 — Aim assist reach is derived, and the margin is the only number anyone authors

The user's hypothesis, and it turned out to be the governing rule written as arithmetic: **a wedge
should reach further than the attack can hit.**

If the wedge matched hit range exactly, then locking on would *encode* whether the target is
reachable — a free rangefinder, and aim assist quietly answering the one question Target Lock forbids
it. The rule has always been that it may correct *where you are pointed*, never *whether you were
close enough*. Overshooting is what keeps that true, and it buys a second thing the user named: a
defender can watch an attacker turn onto them and still whiff, so a near miss is readable rather
than mysterious.

Erring larger is also the right direction. A wedge *shorter* than hit range would drop targets at
maximum range — where travel is longest and aiming matters most — which is the "backwards" failure
the deadzone design was rejected for a day earlier.

### The formula, and the term that was missing from it

Both wedges are `FTDAttackHitbox` and both measure `MaxReachCm` **to the target's body** rather than
its origin, so the capsule radius cancels and the sum is clean:

    reach = base lunge + branch lunge + branch damage reach + margin

The user's first form was *branch lunge + 200*, which inverts the intent: it omits the base lunge and
the damage reach, landing **below** hit range and dropping targets that could actually be struck.
Travel plus reach *is* the furthest a body can be and still be hit; the margin is the only judgement
in the sum.

    light   100 + 200 + 150 + 200 = 650
    heavy   100 + 300 + 150 + 200 = 750
    charged 100 + 400 + 150 + 200 = 850

**A corroboration worth recording:** the hand-tuned light was 600 against a derived 650 — and the
light is the *only* wedge that was ever observed working, because of the branch-0 defect above. The
one value with real feel behind it and the derivation agree within 8%; the two that were never seen
were out by 250 and 250.

### Why the margin is exposed and not the total

The user asked for a single universal dial representing the constant part — the 450. It is exposed as
the **200** instead, and the two are the same knob: turning 200 → 250 moves the total exactly as
turning 450 → 500 would.

The difference is drift. 450 silently bakes in the base lunge and the damage reach, so retuning the
base lunge from 100 to 150 would shrink the real margin to 150 while the authored number sat
unchanged — the `TurnRateDegrees` failure shape, which already costs this project a trap. Exposing
the margin reads the other terms live, so the knob means exactly one thing and cannot go stale. Same
tunability, honest units.

### The struct exists so that reach cannot be authored

`FTDAimAssistWedge` replaces `FTDAttackHitbox` on the branch and has **no reach field at all**. That
is the point rather than tidiness: the previous design reused the hitbox, derived nothing, and three
wedges were hand-authored of which two never did anything. **A field that is silently ignored is
worse than no field**, so there is no field.

Everything else stayed a dial at the user's request — arc, arc centre, and the vertical band remain
per branch even though nothing differentiates them today, on the grounds that they cost nothing to
keep and are expensive to rediscover.

**`bEnabled` had to be added, and the reason is a trap the codebase already documents.** "Off" cannot
be expressed as an arc of 0: a zero arc still passes the subtended-angle widening in `OverlapsCapsule`
and would quietly select anything close enough, which is exactly why `MakeDisabled` zeroes *reach*
rather than arc. Reach is derived here and never 0, so that route was unavailable and an explicit flag
took its place. It defaults to **on**, the deliberate opposite of the old per-branch default — an
unauthored branch should aim like the rest of the ladder rather than silently drop homing mid-hold.

### Measured the day it landed

`AIM WEDGE reach=650` at activation, `750` at the escalation to heavy, `850` at the escalation to
charged, `0 homing=0` at commit — matching the derivation exactly. Lights print only the 650. Damage
exact (health 60 after one charged at 40), no stuck tags, `LUNGE STOP` still firing on hits and not
on a corpse.

**The type change cost nothing**: all three wedges came through on the new struct's C++ defaults,
which happen to equal what was authored — `bEnabled true`, arc 40, centre 0, band ±70. Verified by
reading them back rather than assumed, since a `UPROPERTY` type change is exactly the migration that
silently wiped a Blueprint map once before.

**200 is signed off but unfelt.** It is larger than the 100 cm separating the tiers' lunges, so all
three assist from similar overshoot even though their hit ranges differ by 200 — deliberate, since
how wrong your aim may be is a property of the player rather than of the swing, which is the same
argument that keeps the arc uniform at 40°.

---

## 2026-08-14 — The homing wedge follows the ladder, and a debug view authored two values nobody ever saw

**The defect, which shipped in the rotational half on 2026-08-13 and was found the next day.** Homing
ran the entire windup on `Branches[0].AimAssistWedge` — hardcoded — for every tier. The per-branch
wedge was read once, at commit, by which point homing had driven the aim error to ~0. So the light's
reach silently governed homing for all three tiers, and **the heavy's 1000 and charged's 1100 did
essentially nothing.**

### How it survived a play-verification, which is the part worth keeping

The debug draw shows the homing wedge. The homing wedge was always branch 0's. So during a *charged*
windup the viewport drew the *light's* volume, in the charged attack's colour, at the charged
attack's moment, with nothing on screen naming which branch it belonged to.

The user authored all three wedges against that view. Editing the light moved the drawing; editing
the heavy and charged moved nothing, and the changes were read as having applied. **Two of the three
committed numbers had never been observed at all**, and 600 was the only value anyone had seen work.

Three things made it hard to see, and they generalise:

- **A debug view that is honest about a mechanism can still be dishonest about a *quantity*.** The
  draw never lied — it showed exactly what homing used. It simply never said *whose* number that was,
  and the reader supplied the obvious wrong answer.
- **Sizes cannot be read by eye.** Both parties spent a session comparing remembered radii. The wedge
  is now traced (`AIM WEDGE`, printing reach and arc) precisely so nobody has to.
- **The symptom surfaced immediately after unrelated work in the same files**, so it read as a fresh
  regression. It was a day old. `git log -S` on the draw and a diff of the suspect commit are what
  separated "changed today" from "was always broken" — and the answer was neither party's memory.

*The user diagnosed it, including proposing that they had placebo'd themselves, and the confirming
test was theirs: set the light's wedge to 3000 and throw a charged. The charged homed from 3000 cm.*

### The fix, and why widening does not leak the tier

**Homing now re-arms with the new branch's wedge at each escalation**, in the same block of
`HandleCheckpoint` that advances the branch. Light's wedge until the attack escalates to heavy, then
heavy's until charged.

The obvious objection is that a per-tier homing range is a *tell* — a defender at 800 cm would see
the body snap toward them for a charged but not for a light. It does not apply, because escalation is
the same instant `EnterCoil()` fires, and the coil is the designed tell. Before the first boundary
every tier still homes on branch 0's wedge, which is the only span that must be indistinguishable.
**The wedge widens only at moments the defender is already being told**, so this is consistent with
the reactability model rather than an exception carved into it.

Note what this does *not* fix: the per-branch wedge is still read again at commit, and homing has
still absorbed the error by then. That call is now merely redundant rather than inconsistent — it
uses the same wedge that has just been homing.

### The monotonicity assumption is the designer's, recorded as theirs

Wedges must be non-decreasing in reach across the ladder. **Deliberately not enforced in code**, at
the user's direction, and the reasoning is why it is safe to leave unenforced: a shrinking wedge does
not break anything, because the body has already turned. Worst case it stops tracking or re-picks,
reading as a slightly misleading rotation before the lunge. Filed as a trap rather than a guard,
because policing a designer's stated commitment with runtime behaviour trades a clear rule for a
silent one.

**The sharp edge is 0, not a small number.** `MaxReachCm` of 0 is *disabled*, not narrow, so a later
branch left unauthored switches homing off mid-hold and the body stops tracking partway through a
charge. That is the one case where the failure is not merely cosmetic.

### Measured the day it landed

One held attack, from the trace: activate `reach=600`, `ESCALATE -> branch 1` with `reach=1000`,
`ESCALATE -> branch 2` with `reach=1100`, commit `reach=0 homing=0`. Wedge changes share a timestamp
with their escalation, and the boundaries land at 0.158 / 0.460 / 0.702 against an authored
0.15 / 0.45 / 0.70. Lights show one wedge and a held attack shows three, which is the check to repeat.

**The three authored numbers are now live and observable for the first time.** 1000 and 1100 should be
treated as untested placeholders and authored by feel, not inherited as decisions.

### A correction to this session's own commit, because it reads as design intent and is not

Commit `0743a99` shipped the three wedges with the message: *"The arc is uniform because it means
'how wrong your aim may be', which is a property of the player rather than of the tier. Reach is not:
it scales with the tier's own travel, which is what keeps assist from selecting a target the lunge
could never have reached."*

**The first sentence stands. The second was invented.** It is a post-hoc rationalisation, written by
the assistant around two numbers that were assumed to be authored deliberately and had in fact never
done anything. It reads like a recorded design decision and should not be cited as one — nobody chose
600/1000/1100 against observed behaviour, because there was none to observe.

The general form is worth more than the correction: **a plausible reason offered for someone else's
number manufactures a decision that was never made.** Describing what a value does is safe; explaining
why it was chosen, when you were not there, is not.

---

## 2026-08-14 — The lunge stops on a hit, and a pause was never going to cover it

The user's request, and the reasoning is a distinction the codebase already had but had never been
made to carry weight: **the standoff gate pauses, and pausing is not stopping.**

`FTDRootMotionSource_FacingForce::IsWithinStandoff` contributes nothing on a tick where a body sits
ahead, but time keeps advancing and the source stays live, so travel resumes the moment the
obstruction leaves. That is exactly right for the job it was written for — a target backing away
mid-attack has to stay reachable, which is the whole reason the gate is per tick rather than
pre-computed. It is exactly wrong once a hit has landed: **killing a target removes its capsule, the
gate opens on a corpse, and the attacker slides forward through the space it occupied.** No standoff
distance can express that, because the thing being gated on has stopped existing.

So the stop is a second mechanism rather than a tuning of the first. `UTDGameplayAbility` holds a
weak handle to the task it started and `StopLunge()` ends it; `UTDMeleeAttackAbility::HandleTraceHit`
calls it. Both attack paths inherit it and the dodge, which also lunges, does not — it has no trace.

### Where it fires, which is three decisions rather than one

**Not against geometry.** A hit on something with no ASC is a wall, and walls do not stop a lunge:
the movement component already handles sliding along one, and this project does not track hits against
world geometry at all.

**Not against an i-framed target.** A dodged attack runs on, lunge included. The evade is *supposed*
to make the swing sail past, and stopping the attacker dead would hand them the spacing as
compensation for being read — turning a successful defensive read into a positional reward for the
person who was beaten. This is why the stop sits *after* the immunity check rather than before it.

**Keyed to the hit, not to the damage.** The user's point: damage has to travel through effect
application and waits on authority, while the hit is detected right here. They are the same instant on
the server today, so this buys nothing yet and costs nothing — but they are different events, and
hanging movement off the slower of the two is the kind of thing that eventually reads as a slide.
`DamageEffectClass` stopped gating the function's early-out for the same reason: it gates damage,
which is a consequence, and an ability with no damage effect configured must still stop.

### What it does to the network story, stated rather than solved

**The trace is server-only by deliberate design and the lunge is a predicted root motion source.**
The gate is safe across that boundary because it is *geometric* — both machines run the same sweep
against the same replicated positions and agree without being told. A stop is not: it is driven by a
fact only the server has, so an owning client keeps travelling until a correction arrives.

Bounded arithmetic, since nothing has ever run two machines: release opens 0.05 s into a 0.12 s branch
lunge, so at the earliest possible hit the remaining travel is ~117 cm light, ~175 heavy, ~233 charged.
In practice far less, because the gate has usually already paused travel by the time anything is in
contact. Filed as a trap against the first multiplayer slice rather than papered over. Built
server-side anyway: `FRootMotionSource` has a replication contract with `UpdateStateFrom` for exactly
this reconciliation, so this is the channel the engine intends, not a hole.

### Two things checked rather than assumed

**`EndTask()` really does stop the character.** It routes to `OnDestroy`, which calls
`RemoveRootMotionSourceByID`. And the `ClampVelocity`/0 that `StartLunge` passes — there so no
momentum survives into the next phase — still applies: `FRootMotionSourceGroup::CleanUpInvalidRootMotion`
processes `FinishVelocityParams` for sources `MarkedForRemoval`, not only for finished ones. A stopped
lunge therefore leaves no residual slide, which would have been the same bug in a new place.

**It is traced.** `LUNGE STOP` joins the timing log, because a stop and a gate that simply stayed shut
for the rest of the lunge produce an *identical resting position* — indistinguishable from outside,
and only one of them survives the target dying.

### Measured the day it landed

Play-verified against the auto-attacking dummy. `LUNGE STOP` fires once per connecting attack, ~38 ms
after the release window opens. The decisive case arrived free: the killing blow logged `LUNGE STOP`
and `DEATH` at the **same timestamp (21.226)**, and the next swing — thrown at a corpse during the
three seconds before the revive — produced **no stop at all**, because it hit nothing. A natural
experiment for both halves of the rule in one run.

---

## 2026-08-14 — Exhaustion recovers at its own rate, and that is not the timer that was deleted

Two changes at the user's direction: `StaminaRegenPerSecond` 25 → **40**, and a new
`ExhaustedStaminaRegenPerSecond` holding the old **25**. Normal recovery gets faster; exhaustion
recovers exactly as slowly as it always did, and is now genuinely a penalty rather than the same rate
wearing a lockout.

**This is not `ExhaustionSeconds` coming back, and the difference is the whole reason it is safe.**
That was a *duration* — a second termination condition that could disagree with the bar, which is why
it was deleted. This is a *rate*. Exhaustion still ends when stamina reaches Max and at no other
moment, so the bar remains the single source of truth for how long it lasts. What splitting the rate
buys is that the penalty can be tuned without also retuning how fast everyone recovers in normal play,
which welding them prevented. The tuning-map row moved with it: the knob for "exhaustion feels too
long" is now the exhausted rate, and reaching for `StaminaRegenPerSecond` retunes dodge cadence
instead.

**Worth naming, because it is not obvious from the property's name:** `StaminaRegenPerSecond` is how
fast a *dodge* becomes affordable again. At 50 a dodge and 40/s it is 1.25 s to the next one against
2.0 s before, plus the pause. This was a balance change to evasion cadence as much as to recovery.

### The knob can reintroduce the bug that was fixed beside it

`ExhaustedStaminaRegenPerSecond = 0` is not "no recovery while exhausted"; it is **permanent
exhaustion**, every defensive action locked out for the rest of the match, because regen is the only
thing that ends the state. Clamped to a minimum above zero, with the reason recorded in both the
header and the trap — a clamp whose reason is not written down is one a later edit removes.

Note the shape: a fix and a fresh way to cause the same defect shipped in the same commit. The
mechanism was only reachable through code before and is now reachable through a designer-facing
number, which is a *widening* of the failure surface even though the failure itself got harder to hit.

### What is verified and what is not

The defaults are live — both character Blueprints read 40 and 25 off their CDOs after the rebuild,
which also proves the reflection change landed. `StaminaRegenPerSecond` turned out **not** to be a
serialized Blueprint override, so the C++ default reached both; `StaminaRegenPauseSeconds` is one
(player 0.5, dummy 1.0), which is worth knowing before assuming the next stamina default propagates.

**The rates themselves are not play-verified, and that is a limitation of the build rather than an
omission.** Nothing in the game spends stamina without a human pressing dodge, and the attribute set
cannot be written through the toolset — `SpawnedAttributes` is not reflection-readable in UE 5.8 — so
there is no automated path to a non-full bar. The exhausted branch is further out of reach: it needs
exhaustion, which needs the dodge. Verified by construction and review; the arithmetic to check in
play is 0 → full in **2.5 s** normally and **4.0 s** while exhausted.

---

## 2026-08-13 — Target Lock's rotational half aims the lunge, not the swing

Play-verified the day it landed. The user's verdict is the design thesis confirmed rather than
discovered: *"Player intent feels MORE precise than before, even though logically, we've been
lowering the player's precision."* They predicted exactly that when proposing the final shape —
**"this will FEEL the most precise, even though it's not literally"** — so it is a prediction that
held, not a rationalisation after the fact.

### The first build was provably inert, and arithmetic said so before play could

It shipped correcting *"just far enough to bring the target into the damage wedge"*. That is always
zero. The assist wedge is the narrower of the two by design, so everything eligible for assist was
already inside the damage wedge. No values of reach, arc or distance change it — both wedges get the
same subtended-angle widening, so the inequality never flips.

**The fix was not a wider wedge but a different question.** With a 60 degree damage arc widened by
the target's own body, the swing has +/-36 degrees of tolerance at range and +/-50 at contact:
pointing slightly wrong essentially cannot miss. **The swing does not need aiming. The lunge does.**
Travel follows facing frozen at commit, and a 25 degree error over the light's 200 cm of branch
travel lands 84 cm to the side — the standoff gate never closes, because you were never heading at
them, so you sail past at full distance into recovery beside them.

The user's framing, which is the whole design in one line: **a margin of error for aiming the lunge,
not for aiming the mouse.**

### The deadzone existed for a technique this game does not have

A deadzone replaced the damage wedge as the correction target, then was deleted the same day. It was
protecting the ability to *lead* a moving target — a snap to centre would drag the player back onto
where the target *is*, undoing the lead.

**That concern was imported from a general principle rather than from this game's numbers.** The
hitbox opens 50 ms after commit into a 60 degree arc; a target at walking speed crosses about 7
degrees of a +/-36 window. Leading a melee swing is not a technique here, so the deadzone bought
nothing.

And it cost real range, because setting it equal to the authored half-arc collapses the whole
correction to the subtended term:

| Distance | Max correction, deadzone 10 / arc 20 |
|---|---|
| 450 cm | 5.4 deg |
| 300 cm | 8.0 deg |
| 200 cm | 12.1 deg |
| 124 cm | 19.8 deg |

**Backwards** — least help at range, where travel is longest and aiming matters most, and most help
at contact, where the gate has already solved it.

### So the correction is full, and the wedge is the contract

Snap to dead on. **The wedge is the margin of error, and it is aimed rather than corrected into.**
That gives one knob with an unambiguous meaning — *how wrong may your aim be and still connect* —
and a contract that holds mechanically:

- aim inside the wedge, and the body ends at 0 degrees of error;
- space inside travel plus reach, and the gate stops you at 124 cm against a 192 cm damage reach;
- so the hit follows, short of a defensive action.

The governing rule survives untouched: this corrects *where you are pointed*, never *whether you
were close enough*. Spacing still decides everything.

### The frame is the camera's, and the reason generalises

The wedge is evaluated from `GetAimYawDegrees` — control rotation where there is one, the body
otherwise. The user's statement of why is the one to keep: **the aim assist wedge should be
camera-driven because it aids the attacker's inputs, while the damage dealing needs to be
telegraphed enough that defenders can trust their eyes.**

Assist is measured where intent lives; damage is measured on the body, which is the only thing an
opponent can read. They coincide whenever facing has caught up, which is most of the time — and the
moment homing makes them diverge is exactly the moment the distinction starts mattering.

### Homing during the base lunge, and why it is not the homing that was rejected

The user's proposal, and it solves the artifact that would otherwise have capped how wide the wedge
could be: without it the entire turn lands in one frame at commit, which reads as a pop. Homing
spreads it across the windup so what arrives at commit is a residual.

**Measured the day it landed:** a target 12.9 degrees off the camera produced `turned +0.0` at
commit. Homing had already absorbed all of it, and the body was 12.9 degrees away from the camera,
tracking the target. That is the mechanism visible in one line.

**It costs nothing that post-commit tracking would have cost.** Continuous tracking was rejected
because a defender's movement could no longer make an attack whiff. Homing stops *at* commit — which
is where the defender's reaction window opens in the first place — so freezing at the boundary that
already exists leaves whiff punish exactly as it was. It is also tier-safe: the base lunge is
shared, so homing during it is identical across all three and leaks nothing.

**It reuses `TurnRateDegrees` and adds no number**, also the user's call. But it cannot simply be a
second rule writing the same yaw: that is last-writer-wins, and made to compete at equal rates it
deadlocks, since turning away raises the bearing and homing pulls back exactly as hard — a tagged
target could never be left, which would destroy the "deliberately pick the further of two" property.

**So the player's authority moves up a level: from facing to selection.** Homing owns facing while a
target is tagged; the wedge is evaluated from the camera; aiming at someone else selects them and
the body follows there. Steering is expressed as choosing rather than as fighting. This is also why
camera-frame evaluation is load-bearing rather than tidy — with body-frame evaluation the two would
be the same rule again.

### Left open

**No hysteresis.** Selection is re-evaluated every tick, so a camera parked between two near-equal
candidates can flip frame to frame and swing the body at up to `TurnRateDegrees`. Not built, because
nothing has felt it — and it becomes reachable exactly as the wedge widens, since two eligible
targets are rare at 20 degrees and routine at 90.

**The distance tiebreak is nearly decorative.** `FMath::IsNearlyEqual` defaults to 1e-4 degrees, so
it fires only on essentially exact ties. It covers the degenerate symmetric case where iteration
order would otherwise decide; the determinism claim should not be read as broader than that.

**Damage mostly resolves ties on its own**, noted by the user: melee is multi-target by construction
(`ResolveHits` walks every overlap), so two candidates close enough to tie are both inside a 60
degree wedge and both take the hit. What selection still decides is *travel* — which body the gate
stops you against, and therefore who you end up standing next to.

**The test is horizontal only.** Bearing ignores Z entirely, so a target above or below competes on
horizontal angle alone. Inert while everyone is on flat ground; live the moment the ramp or a jump
enters it.

## 2026-08-13 — The dodge stops reading displacement off its clips, and an anomaly is closed by removal rather than diagnosis

The dodge was the last system in the project taking a number from an animation. Authored wedges
took space off the art, authored durations took time, Lunge took the attack's displacement, and
this finishes it — raised by the user on 2026-08-12 in Lunge's own entry as *"whether the dodge
should be untethered from vendor root motion the same way"*, and triggered by a bug.

### What triggered it, and what we deliberately did not learn

Dodging off the test level's ramp produced, in the user's words, *"a different behavior every single
time — some kept coasting, some lost their momentum, consistently inconsistent."* The worst case was
a **left dodge off the top of the ramp falling forward, every time, at ninety degrees to its
intended direction.**

Two hypotheses were live. **Air control on unsuppressed input** — `GA_Dodge` has
`bLocksMovement = false` and `AirControl` is 0.35, so on the ground animation root motion masks the
player's input and the moment you go airborne it stops masking. That was **killed by experiment**:
the user dodged off the ramp holding nothing and the forward momentum persisted. The survivor was a
velocity handoff at the ground→air transition, and it was never confirmed.

**The user chose to stop investigating**, on the grounds that the cause was about to be deleted, and
that is recorded as a judgement rather than an omission: *"it just feels like a bit of a low-yield
rabbit hole."* Correct call — the failure lives in animation root motion, and this removes animation
root motion from the dodge entirely.

**So the anomaly is closed by removal and not by diagnosis, and that is the part worth carrying.**
If dodges still behave strangely off ledges after this, the transition problem is in root motion
*sources* too, and the attack lunge has been quietly carrying it — attacks deliberately keep running
when a lunge takes them off a ledge, so the same window exists there and nobody has looked.

The one lead never followed: the mesh carries `RelativeRotation.Yaw = -90`, which is the most likely
source of an exact ninety-degree error anywhere in this codebase.

### The instrument could not have caught it, and that generalises

`DODGE END` printed `travelled=Delta.Size2D()` — a **magnitude**. A dodge travelling its full 405 uu
at ninety degrees to its intended direction produced a line indistinguishable from a perfect one,
which is why the log looked clean through the entire investigation.

**Every number this system was ever tuned on had the same hole.** `MeasuredTravelCm`'s eight
calibration values were captured the same way, so eight scales corrected distances whose *direction*
nobody had ever checked. The trace now logs the local-space vector — forward, right, up — so a
direction error names itself.

The general form, and it is the filtered-view rule in a new costume: **an instrument that answers a
different question than the one being asked will answer it correctly forever.** "How far" was always
right. "Which way" was never asked.

### What replaced it

`UTDGameplayAbility::StartLunge` moved up from the melee ability so attacks and the dodge share one
displacement path, gaining a `YawOffsetDegrees` on `FTDRootMotionSource_FacingForce`. Attacks pass 0.
The dodge's eight offsets are **the direction enum's own order times 45** rather than a table —
`Fw, FR, R, BR, Bw, BL, L, FL` is already the compass, so a direction cannot be given the wrong angle
without also being in the wrong place in the enum.

The offset is applied to a direction still read from facing every tick, which keeps the netcode
property that made the source cheap: one float on the wire instead of a world-space vector that
would then have to agree with a rotation which already replicates.

**Deleted: `DodgeRootMotionScale`, `MeasuredTravelCm`, `ComputeRootMotionScale`, and the
constructor that seeded eight calibration entries.** All eight directions now travel
`DodgeTargetDistanceCm` because that is the number. **The trap asking for `MeasuredTravelCm` to be
re-measured whenever the montage is rebuilt goes with them** — there is nothing left to calibrate.

**The dodge passes standoff 0**, deliberately: Target Lock's gate belongs to attacks. An evade has to
travel *past* people, and gating it on pawns would break dodging through a crowd.

### The precondition, and it is the filed trap

Animation root motion suppresses root motion sources outright, so this only works if `AM_Dodge`
carries none. There are **no `_IP` dash clips** — all sixteen in the project, V1 and V3, are `_RM`.
The way through is that `_RM` names what is *baked into* a clip, not what is switched on:
`bEnableRootMotion` ships **false** and we enabled it. Turning it back off on the eight V3 `Dash_*`
clips restores the library default and needs no new content.

**If a dodge ever travels zero, check that flag first.** It is the same trap the attack montage has,
and the dodge has no equivalent of `StartAttackMontage`'s ungated warning to catch it.

**And clearing that flag alone is not enough, which cost a play session.** Disabling
`bEnableRootMotion` stops the movement component *consuming* the root motion; the displacement is
still in the root bone, so it renders. The mesh walks off the capsule and snaps back at the end —
reported as *"the animation goes about 4x as far as the dodge and is desynced… then resets its
position"*. The pair that works is `bEnableRootMotion = false` **and** `bForceRootLock = true`: the
first stops the double-move, the second pins the root bone so it cannot drift. **Setting only the
first is worse than setting neither.** Now recorded in `Docs/Animation-Library.md`, which had the
premise right — `_RM` names what is baked in, not what is switched on — without the corollary.

### Measured across all sixteen dodges, and direction was measured for the first time

Eight directions on flat ground and eight off the ramp. **Every direction is correct**: cardinals
hold their off-axis component within 0.1 cm (`L` reads `fwd=-0.0 right=-409.4`), and every diagonal
is symmetric within 0.3 cm. That has never been checked before in this project, because until this
session every number the dodge produced was a magnitude.

**The ramp is unremarkable now**, which is the result that matters: distances 405.5–412.5, the same
band as flat ground, with `up` picking up −49.9 to +1.3 for the slope and nothing else changing. No
coasting, no stalling, no per-direction inconsistency.

**Distances run 405.1 to 412.5 against an authored 405 — always over, never under.** That is a
systematic bias rather than noise and it is one movement tick: at 1012.5 cm/s the maximum excess of
7.5 cm is 7.4 ms of travel and the minimum is 0.1 cm, so the spread is exactly zero-to-one tick of
end overshoot. Left alone as sub-frame.

**For scale on what the rewrite bought:** the old system had a 90.6 cm spread between directions and
eight hand-measured constants to hide it. The new spread is 7.4 cm, it is explained, and there is no
calibration data at all.

## 2026-08-13 — The gate is per tick, and lunge duration is a designed quantity

Two corrections, both found by play within an hour of the clamp shipping, and both supersede claims
made the same day. They are one entry because they were found in one session and the second was only
visible once the first stopped dominating the feel.

### Pre-shortening bakes in a prediction, and predictions about a moving opponent expire

The clamp shipped as *"compute a shorter distance before the lunge starts"*. The user played it and
reported that **a target moving away during an attack could never be reached**. That is exactly
right and it is worse than it sounds: it is not a case the system fails to help with, it is a
**strict regression against having no system at all**, which would have travelled the full authored
distance and usually caught them.

The error is precise and worth naming, because it looks correct right up until something moves. The
requirement was always *"do not be inside a body"* — a **state**. Pre-shortening expresses it as a
**prediction**, evaluated at one instant, about a target with complete freedom to invalidate it.

So the standoff moved into `FTDRootMotionSource_FacingForce::PrepareRootMotion`, which already
re-reads facing every movement tick — the same idea applied to the other live input. Each tick asks
whether a body is within `StandoffCm` ahead and contributes nothing if so. **Time advances either
way**, so the gate can only subtract travel: the authored distance stays a hard ceiling and the
source still ends on schedule.

**It is still not homing**, which is the property whiff punish depends on. Homing changes a lunge's
direction or extends its distance; this changes neither. A target moving laterally still escapes,
and one backing off still escapes if it out-paces the authored travel. What it stops getting is the
escape handed to it for free by a stale prediction.

**Measured, and the difference is directly observable rather than inferred.** The gate should close
at 84 cm (capsule contact) + 40 cm (standoff) = 124. Commit distances across five attacks read
**118.3 / 121.8 / 119.4 / 121.1 / 121.1** — just inside 124, the expected one-tick overshoot at
667 cm/s. The previous build read **97.5**, because its single sweep was taken at 200 cm and found
nothing within the base lunge's 100 cm, so the base lunge was never gated at all. The per-tick
version gates a span the pre-computed one structurally could not see.

*This also retires a limitation the first version documented as known:* that the clamp was evaluated
against the facing at the start of the span, so a player turning mid-lunge followed a curve the
straight sweep never measured. Asking every tick removes it rather than mitigating it.

### Lunge duration was never a designed number, and the symptom was self-contradictory

The user reported the lunge felt **slower than it should be and further than it should be, at the
same time**, and could not see how both could hold. They hold because speed is distance over
duration: one duration being too long produces both readings at once.

| | Speed |
|---|---|
| Walking (`MaxWalkSpeed`) | 500 cm/s |
| Base lunge | 667 cm/s |
| Light's branch lunge | **1000 cm/s** |
| Dodge (405 cm ÷ 0.4 s) | **1012 cm/s** |

**The attack lunged at exactly dodge speed**, which is why it did not read as a burst. It was not one.

The cause is that lunge duration was not authored anywhere. The branch lunge ran commit → end of the
release window, so `ReleaseSeconds` set it; the base lunge ran press → `Branches[0].HoldUntilSeconds`,
so the light's input boundary set it. **Both are reactability and balance numbers with no
relationship to how long a character should be carried forward.** Welding them meant the lunge could
not be made snappier without shortening the hitbox, nor the hitbox lengthened without making the
lunge floatier.

The user identified it as a design flaw rather than an implementation one before the mechanism was
worked out, and that was the correct read.

**A consequence that was invisible while the durations were equal:** the avatar was moving for the
*entire* time its hitbox was live, so the damaging volume was dragged through space for its whole
existence and there was never a moment of planting and striking. A burst finishing early in the
release window is a different shape of attack, not merely a faster one.

So `FTDAttackBranch::LungeDurationSeconds` is authored, and `UTDMeleeAttackAbility::LungeDurationSeconds`
— which existed and was being overridden into irrelevance — is authored for the base lunge. The
boundary is now a **ceiling rather than the value**: the base lunge is clamped to
`Branches[0].HoldUntilSeconds` because it genuinely must finish before the branch lunge starts, or a
light would run two `Override` root motion sources at equal priority, where which wins is an
implementation detail rather than a design.

**This supersedes the claim that Lunge needed no timing values.** That entry recorded *"Lunge added
two distances and zero timing values, because the boundaries it needed already existed for other
reasons"* as a virtue. It was elegant and it was coupling, and the general form is worth keeping:
**a boundary that already exists is not the same as a boundary that means the right thing.** Reusing
one is free only when the question it answers is the question you are asking.

Note the trap file had already flagged the base-lunge half without recognising it as this: it
records that `Branches[0].HoldUntilSeconds` sets three things and calls that a coupling to watch.
This is it going off.

### Left open

Starting values are 0.12 s on every branch, which is a placeholder chosen to be clearly a burst
(1667 cm/s on the light) rather than a considered number — it has not been felt. The base lunge is
0.15, unchanged, because that is what it already was and the clamp leaves it there.

The tuning question underneath is deferred and should stay deferred: the user observed that a
**shorter, faster** lunge also returns control sooner, which is *not* true — movement is suppressed
for the whole ability, so time-without-control is governed by `RecoverySeconds`, not by lunge
duration. Both observations point at recovery, and recovery is in a knowingly flawed state until
chained lights exist, since the only case testable today is repeated whiffing. **Tuning the lunge
until serial whiffing feels good would optimise the state the design most wants to punish.**

## 2026-08-13 — Target Lock, and the bug it exists for is sliding rather than overshooting

A new item, raised by the user from play after Lunge shipped: attacks against anything but a
maximum-range target felt awkward, described as lunging past someone and *sliding off their capsule*
before the release window even began.

**The mechanism, once it was measured rather than guessed.** Two capsules in contact orbit at 84 cm
centre-to-centre, so the contact circle is 528 cm around. A lunge driving into a blocking body keeps
its tangential component, and that fraction approaches the whole speed as you slide toward their
side — so travel converts almost directly into arc. The 100 cm base lunge is 68° of it and a light's
full 300 cm is 205°, which is behind them. Facing freezes at commit and the wedge is authored in the
attacker's frame, so **the wedge does not follow you round.** At maximum range you never touch the
capsule, never slide, and never see any of it — which is exactly the shape of the complaint.

So this was not one bad number. It was three separately-correct decisions interacting: authored
travel that is large, capsules that block, and a wedge frozen to a body being pushed sideways.

### The governing rule, which is what makes it safe in a spacing game

**Target Lock may correct where you are pointed. It may never correct whether you were close
enough.** The clamp only shortens travel; the aim half only rotates. Neither can convert a spacing
miss into a hit, so whiff punish is untouched — and "barely outspacing an attack feels identical"
stops being something to tune toward and becomes arithmetic. It was the user's constraint and it
turned out to be the design's spine rather than an acceptance criterion.

### The clamp is geometric, and that is the load-bearing choice

The obvious build is to acquire a target and clamp toward it. **Rejected, because a selection test
has a boundary and a boundary is a cliff**: a candidate at 29° would clamp to nearly nothing while
one at 31° travelled the full authored distance, from the same input. That is precisely the
"imprecise" feel the system exists to avoid.

Instead the lunge sweeps the character's own capsule along its path and stops `LungeStandoffCm`
short of the first pawn. The sweep **is** the collision test rather than an approximation of it, so
anything it declines to clamp against is something the character would not have collided with. Three
properties fall out rather than being handled: no-op at maximum range, no-op with an empty path,
no-op on a whiff into air. It also cannot pick the wrong target in 1vX, because it clamps against
whatever is actually in the way, which is definitionally the thing that would have been slid off.

**It is not blind to i-framing targets, and that is deliberate.** `TDDodgeAbility` adds a tag and
never touches collision, so a dodging character is invulnerable and still solid. A clamp that
ignored them would drive into a body and slide off it — the original defect, returning in the one
case the player is least able to explain. The *aim* half skips them; the clamp cannot.

### Measured the day it landed

Six dummy attacks at the placed 200 cm. Commit and release samples read `dist=97.5 bearing=+0.0` and
`dist=97.5 bearing=+0.0` — identical, on every attack, which is the no-slide signature. The base
lunge ran in full because the sweep correctly found nothing within its 100 cm, and the branch lunge
clamped **200 → 0** because the attacker was already 13.5 cm from contact, inside a 40 cm standoff.

That last figure is the tuning question this hands forward: a light authoring 300 cm of travel now
performs 100 at this spacing. The clamp is doing its job; whether the authored numbers and the
standoff are right is the reach/travel/spacing pass, which now has something working to be judged
against.

### The name, and two that were dropped

**Target Lock**, chosen by the user. *Soft lock* was rejected because *softlock* is an established
term for an unrecoverable stuck state and would read as a bug class in a combat module. *Target
Assist* was rejected as vaguer: "assist" says nothing about mechanism, and **the lock is real** —
`CommitAttack` already calls `SetAbilityFacingLocked`, so the system names what it locks onto rather
than borrowing a metaphor. The distinction from lock-on is that a lock-on is persistent and
player-toggled, while this is evaluated once, discharged at commit, and never re-evaluated: a system
that cannot be held is not a lock-on.

### The aim half, designed and deliberately not built yet

Settled in discussion and left for the measurement to size, so the record is here rather than in
code that does not exist:

- **Post-commit only**, because the windup is the one window where the player is actively steering
  and correcting someone mid-aim is how assist comes to feel intrusive. After commit the player has
  no agency by design, so the correction takes nothing away.
- **A single correction at the commit instant, then frozen.** Continuous tracking is homing, and
  homing means a defender's movement cannot make you whiff.
- **It rotates the character, not the camera.** Forcing the camera is what would make an
  invisible system conspicuous.
- **Eligibility is an `FTDAttackHitbox`** — the same struct as the damage volume, authored longer in
  reach and notably narrower in arc. Reusing it inherits the subtended-angle widening, so an
  authored ±10° reads as ±14.8° at 500 cm and ±40° at contact: tight where the player is declaring
  intent, forgiving where they obviously meant the person they are standing on. **The wedge's width
  is the dial between the player declaring intent and the system inferring it.**
- **Selection is smallest bearing, ties broken by distance.** Angular priority because angle is
  expressed intent while distance is the player's own responsibility — and it is what lets a player
  deliberately take the *further* of two targets. Distance-weighted selection was rejected as
  producing the more offensive failure ("I aimed at the far one and it grabbed the near one"). The
  tiebreak exists for determinism rather than for occlusion: unstable ordering would let client and
  server pick different targets and rotate the attack to two different places.
- **It skips `TargetImmunityTags` holders**, using the existing property rather than a second
  immunity list.
- **Correction is the minimum sufficient angle, not a snap to centre.** Already inside the damage
  wedge means rotate zero. Two reasons, and the second is the stronger: it is less intrusive, and a
  snap-to-centre would **actively undo a lead** — a player aiming where a moving target will be,
  rotated back onto where it is, by the system meant to help them.
- **An occlusion test was proposed and dropped.** Co-linear targets produce near-identical
  corrections, and the clamp stops at the first body regardless of selection, so the outcome barely
  depends on which was picked. Where selection *does* change the outcome the bearings differ, which
  means it is two distinct targets rather than occlusion.

**The chain chomp is the acceptance case**, and it falls out rather than being built: the clamp
lengthens monotonically with target distance until it saturates at the authored value, and it
saturates exactly where contact stops being reachable. So a target just past that boundary leaves
the attacker rotated onto them, travelling full distance, and arriving short — locked on, committed,
out of range. It only happens because the assist wedge reaches further than the damage wedge, so
"longer reach, narrower arc" and the chain chomp are one decision.

### Left open, and one idea recorded rather than decided

The clamp is evaluated **once per span, against the facing at its start**, while the lunge re-reads
facing every movement tick. A player turning hard mid-lunge therefore follows a curve the straight
sweep did not measure — turning away only under-travels, turning toward can still drive into a body.
The base lunge is the exposed one, being the only span where facing is free. Left as a known
approximation rather than solved with a per-tick re-clamp, which would be more correct and
considerably more machinery.

Also open: whether a fast-moving target becomes unhittable without homing. The real quantity is the
**50 ms between commit and the hitbox opening**, identical on all three tiers, which is the whole
leading window. At today's only speed a target crosses 25 cm in it, comfortably inside the wedge, so
the case does not currently exist. The knobs if it ever does are move speed, that 50 ms, or the
damage wedge's arc — and only the last belongs to Target Lock.

**Dodge intangibility, raised by the user and deliberately deferred**: making dodgers pass through
other capsules. Worth trying only after the clamp has been felt, on the reasoning that much of what
makes bodies feel obstructive is attacks driving into them, so the clamp may dissolve the case. It
has one genuinely elegant consequence — an attacker lunging at an intangible dodger sails *through*
and past them, landing in recovery with their back turned, which converts a successful dodge into a
positional punish for free. And two real costs: it is a strict buff to an option that already gets
400 ms of i-frames for 50 stamina, and **exit depenetration needs a rule**, since ending a dodge
inside someone resolves as either a shove or a teleport. In PvP that is a position disagreement,
which is worse to reconcile than a damage one.

## 2026-08-13 — The camera boom collided with corpses because a collision profile is not a merge

`ATheDreamCharacter`'s constructor exempts the capsule and the mesh from `ECC_Camera` so the spring
arm does not treat a combatant as an obstruction. `ATDCombatCharacter::StartRagdoll` then called
`SetCollisionProfileName("Ragdoll")` on the mesh — and **a profile replaces the whole response table
rather than merging into it**, so the exemption was silently dropped and the boom started colliding
with the body.

Found by reading, from a user report of "jarring artifacts" around dead players. Worth recording for
the second-order half, which nobody had observed yet: `StopRagdoll` sets the profile back to
`CharacterMesh` and **also** does not restore the exemption, so a revived character's mesh blocks the
camera permanently from then on. The bug outlives the ragdoll, and the debug revive makes it
reachable.

The fix is `ATheDreamCharacter::ApplyCameraCollisionExemption`, called by the constructor and after
both profile sets. It is a function rather than three copies of two lines specifically so the next
profile set has somewhere obvious to call — **the general form being that a per-channel override and
a profile assignment are not composable, and the profile always wins.**

Verified as far as reading and reflection go: the exemption reads back on a live mesh after the
refactor. **The artefact itself — a camera that no longer collides with a corpse — has not been
observed**, because it needs a death, and the automated run does not produce one.

## 2026-08-12 — An attack owns your movement, and the rule existed only in the designer's head

Found in play, immediately after Lunge shipped: movement input works during an attack. WASD walks
you sideways through your own windup, and jump launches you out of a recovery you are supposed to be
committed to. **This was never a decision that got reversed — it was an assumption that had never
been written down**, and so had never been implemented or questioned. The user's own framing, and
the reason it belongs in `CLAUDE.md` rather than only here: a gap between the designer's brain and
the documentation.

Three rules, all now stated in the Offense section:

- Movement input — WASD *and* jump — is suppressed for the ability's whole lifetime, windup through
  recovery.
- Attacks cannot be *started* while airborne.
- The ability's own displacement is unaffected. The lunge still moves you.

**Why "the lunge already overrides movement" was a false comfort.** It is true during the lunge:
the root motion source runs in Override mode, so input genuinely does nothing. But the coil carries
no lunge, and neither does recovery — and those are 550 ms and 600 ms respectively on a charged.
The half of the attack that looked covered was the half nobody could walk out of anyway.

### Input, not movement — and the distinction is the whole design

The lock suppresses `DoMove` and `Jump` rather than disabling the movement component. Disabling
movement would also stop the lunge, the dodge's dash and any future knockback, because all of those
run *through* CMC. The rule being expressed is **"you may not move yourself"**, not "you may not
move", and only one of those is implementable by turning movement off.

### Two places it could have gone, and why it went on the shared base

`bLocksMovement` sits on `UTDGameplayAbility` beside `bBlockedWhileAirborne`, applied in that
class's `ActivateAbility` and released in its `EndAbility`. The alternative — having the attack
ability call a lock directly, as it does for facing — was rejected because **opting in should not be
separable from opting into the release.** A checkbox cannot be half-implemented.

That choice creates one hazard, which is why the release is guarded on a per-instance
`bTookMovementLock` rather than on the property: `EndAbility` runs on the shared base for *every*
ability, so an unguarded release would let any ability's ending hand movement back while a different
one still owned it. Guarding on `bLocksMovement` instead would strand the lock if the flag were ever
toggled off mid-run. Only "did **I** take this" answers the real question. Same shape as
`bAttackCommitted` guarding the committed tag's removal.

**Deliberately deferred to the structure audit, at the user's suggestion:** making jump and crouch
into abilities. `ATDCombatCharacter::Jump()` now restates its *third* lockout — dead, exhausted, and
movement-locked — and its own comment already called itself "the one place that rule is not
centralised". That is the argument for the conversion, recorded rather than acted on. When it
happens, the seam is this same flag or a `State.MovementLocked` tag in their `ActivationBlockedTags`;
neither requires undoing this.

### What this closes and what it opens

**Air attacks are out, and that is a design decision rather than a technical limit** — the flag was
built with the opposite case in mind and its own comment says an air attack is a legitimate thing to
want later. Turning them back on is a checkbox.

Two consequences inherited from rules decided elsewhere, both correct and both worth expecting:
the refusal **gates activation, not continuation**, so a lunge that carries you off a ledge does not
interrupt the swing; and the airborne refusal is **deliberately not buffered**, so an attack pressed
in the air is dropped rather than replayed on the touchdown frame.

**Verified by measurement:** `REFUSED GA_Attack_C_0: airborne (mode=3)` with the dummy dropped from
4000 units, and attacks resuming on landing — so the gate is transient rather than sticky. The
movement and jump suppression is implemented and its enabling flag is proven live by that same CDO
write, but **input suppression itself has not been confirmed by a keypress**; that needs a human.

## 2026-08-12 — Lunge is two authored distances, and the boundaries were already there

Displacement is now authored in centimetres, per attack, on the same terms space and time already
were. This supersedes the two-multiplier design in "Attack displacement is two scales" and settles
everything "Root motion scaling is not enough control" left open. `UTDMeleeAttackAbility::RootMotionScale`
and `FTDAttackBranch::RootMotionScale` are both deleted.

**Two spans, and neither needed a new timing knob** — which is the part worth recording, because
the first design did:

| | Span | Scope |
|---|---|---|
| `UTDMeleeAttackAbility::LungeDistanceCm` | press → `Branches[0].HoldUntilSeconds` | shared by every tier |
| `FTDAttackBranch::LungeDistanceCm` | commit → end of the release window | that branch only |

Both durations are *derived* — the base from the first branch's boundary, the branch's as
`(ReleaseAtSeconds + ReleaseSeconds) − elapsed`. Lunge added two distances and **zero** timing
values, because the boundaries it needs already existed for other reasons.

**The coil gets no lunge, and that falls out rather than being ruled.** The base span ends where the
tiers become distinguishable; the branch span starts at commit; the coil is the gap between them.
That matters more than it looks: the coil is the *only* phase whose duration differs between tiers,
so excluding it is what makes wall-clock displacement safe. The earlier entry worried that a GAS
root motion source runs on wall time and would therefore carry a charged 4.7× further than a light —
true, and it never arises, because nothing lunges during the span that differs.

**The user's framing produced this, and it is simpler than the one it replaced.** The proposal on
the table was an approach lunge, an explicit rule excluding the coil, and a strike lunge. Theirs was
"a standard lunge covering the first 150 ms, and the authored one active after commit" — same
result, one fewer rule, and the exclusion is a consequence instead of an instruction.

### The base lunge follows facing, and play reversed the argument for fixing it

Shipped world-fixed, on the reasoning that only the post-commit half can be steered safely. Play
rejected it: the user wanted it aimable and said so after testing.

**Why fixing it was wrong, stated properly now that it is settled.** Animation root motion is
applied in the actor's local frame, so attack travel has *always* turned with the player — a
world-fixed lunge was a regression, not a missing feature. Worse, it cannot be made safe by
freezing rotation, because rotation during that window is exactly what the aim guarantee is:
`TurnRateDegrees × Branches[0].HoldUntilSeconds` = 180°, the whole worst-case gap. So the choice was
never "fixed or steerable"; it was **"steerable, or the attack points wherever the body got to"** —
which is the defect measured at 71% of flick-attacks, reintroduced deliberately.

The mitigation that made a fixed lunge tolerable was to keep it under the capsule radius (42 cm) so
a full 180° divergence displaced less than the character's own width. That ceiling **retires with
the fixed direction**: divergence is impossible when movement follows the body, so the base lunge is
free to be as large as it feels right. The user's first tuned value was 200 cm.

**It is not hand-rolled movement, which was the objection it had to answer.** `FTDRootMotionSource_FacingForce`
is a `FRootMotionSource` subclass — the same first-class extension point every stock source uses,
with `Clone`, `Matches`, `NetSerialize`, `GetScriptStruct` and `WithNetSerializer` traits — so it is
predicted and replayed by the machinery that made animation root motion safe. It stores a *speed*
and builds direction from `GetActorForwardVector()` each `PrepareRootMotion`, which is also why it
is cheaper on the wire than the stock source: the direction never travels, because each machine
reads it from a rotation that already replicates. `SetActorLocation`, `AddMovementInput` and
`LaunchCharacter` remain the versions the netcode audit was right to fear.

Reading facing during `PrepareRootMotion` should replay correctly under prediction, since rotation
is saved-move data. **That is mechanism-level reasoning, not a measurement** — nothing here has run
two machines.

**One source serves both lunges, and that is a consequence rather than a convenience.** Facing is
frozen from commit to `EndAbility`, so a facing-following source and a fixed one are identical
there. Steerability became a property of the phase instead of a setting, which deleted a branch.

### Three proposals rejected, each for a different reason

**Freezing rotation during the base lunge** — rejected. It would retroactively break
`IdleTurnRateDegrees`, which is classified as free-to-tune *only* because the fast rate resumes at
the press and closes any drift; with no rotation there, attacking out of a slow idle turn commits to
wherever the idle rate had reached. A cosmetic knob would have become an aim-breaking one at a
distance, which is the failure shape this file exists to catch.

**A separate turn-rate cap for the base lunge**, to prevent "drillbit" spins — rejected as
unnecessary and unsafe. The maximum rotation across that span is already exactly 180° by
construction, so a corkscrew cannot happen; and any value below the derived rate breaks the
guarantee to solve a problem that does not exist.

**Delaying the facing lock from commit to the start of release**, to give heavy and charged a
visible "settle into aim" — rejected, and the arithmetic is why. Aim is already closed by the first
150 ms, so that window buys *only* tracking of a defender who has had time to react, in the most
valuable position possible: immediately before the hitbox. It is a pure power increase on the two
moves the design most wants punishable, with no correctness case behind it.

What survived from that proposal is the coil rate. `CoilTurnRateDegrees` (600, the user's value)
slows facing during the coil, and it is safe at **any** value including zero for the same reason the
lock delay was rejected: the guarantee is discharged before the coil begins, so everything after it
is tracking rather than aiming. It joins `IdleTurnRateDegrees` on the tune-by-feel side of the split.

### The curve contract, and what is still open

A `StrengthOverTime` curve multiplies the force each tick, so **it must average 1.0 across its range
or the authored distance is silently wrong**. For a straight-line curve that means the two endpoints
sum to 2. Continuity across the commit seam is a separate matter and needs no curve at all: speed is
`Distance ÷ Duration`, so the seam vanishes when `D_branch = D_base × (T_branch ÷ T_base)`.

Open, and deliberately not decided here: the distances themselves, whether the base lunge wants an
ease-out curve, and whether the dodge should be untethered from vendor root motion the same way.
The last is a separate item and was raised by the user on the strength of this pattern working
three times now — authored wedges took space off the art, authored durations took time, Lunge took
displacement, and the dodge is the last system still reading a number off a clip.

## 2026-08-12 — Recovery's first authored values pass play, and expose the input buffer

Two PIE sessions by the user, one on the defaults and one on authored values. Verdict on the
second: *"it all felt very good and expected"*. The values that earned it, and the first time the
asset has agreed with the spec's *charged has heavy endlag*:

| | Light | Heavy | Charged |
|---|---|---|---|
| `RecoverySeconds` | 0.40 | 0.50 | 0.60 |

Honoured within 8 ms at every value (light 0.401–0.407, charged 0.602–0.608, heavy confirmed
through its total), with no warning of any kind across either session. **Heavy was thrown ~40
times**, which closes the one branch the implementation pass never exercised.

**The interesting part is what it cost the input buffer.** Exactly one input was dropped in the
whole log:

> `[5.917] BUFFER InputTag.Attack: released after 52ms held`
> `[6.124] BUFFER InputTag.Attack: expired, 260ms after press`

Chaining light taps — 70, 64, 62, 52 ms in about two seconds — the fourth expired unfired. The
arithmetic: a light's lifetime is now 0.20 + 0.15 + **0.40** = 0.75 s and `InputBufferSeconds` is
**0.20**, so tapping faster than a swing buffers a press that cannot be honoured in time. Session
one had ~20 taps and zero expiries; session two had one in forty-five seconds. Held presses were
unaffected and two fired 333 ms late — **a held button never expires, so it is specifically taps
that this exposes.**

**Nothing was changed, and that is the decision.** Asked whether it was felt, the user reported
nothing beyond what they would *"chalk up to skill issue"*. Play wins over the prediction, but the
evidence is weak in a specific way worth naming: *an observer who knows what they are testing for
is a poor detector of a subtle drop*, so this is "not felt once" rather than "does not matter". It
is recorded with its number so the next person can act without re-deriving it.

**What would change the answer**: recovery being tuned longer, a light string that makes rapid
tapping the primary input pattern, or anyone reporting a dropped input in normal play. The knob is
`InputBufferSeconds` and the trace tells you which case you are in — `expired` is a window
question, no line at all means the press never reached the character.

**The tension to know before reaching for it:** a buffer long enough never to drop a tap during a
0.75 s swing is a buffer that queues an attack most of a swing ahead, which is a different and
worse feel. There is no value that is simply correct.

## 2026-08-12 — Recovery is authored, and the blend-out boundary moves with the play rate

Recovery becomes `FTDAttackBranch::RecoverySeconds`, authored per branch in absolute seconds with
the play rate derived — the same shape windup and release already had. An attack is now three
authored durations and the animation is fitted to all three. `RecoveryPlayRate` is gone.

**The resolution chosen, from the two the trap offered: recovery ends at blend-out.** The user's
call. The alternative was making the ability wait for `OnCompleted`, which would have made the
blend real committed time and lengthened every punish window by 0.25 s. The argument for this side
is that recovery *is* the punish window and what gates the attacker's next action is the ability
ending, so the authored number should be the one that cannot disagree with the mechanic. The cost
accepted knowingly: **a spectator sees about a quarter second more attack than the attacker is
committed to.** That gap is now a design position rather than an accident.

**It is not quite a no-op, and the reason is the interesting part.** Recovery used to be whatever
montage remained, played at rate 1.0 — so it silently inherited the release notify's jitter, which
closes a frame or two late (measured 0.462–0.476 against an authored 0.450). Recovery was therefore
~0.252 s and varied. Authored at 0.2667 it is now 0.268–0.271 s and does not vary. The light's
lifetime grew by roughly 0.03 s, which is the jitter being removed rather than a tuning change.

### The blend-out boundary is not a fixed position

The defect worth recording, because it was invisible in the first test and obvious in the second.

`BlendOutTriggerTime` is negative on `AM_Attack`, which means "blend so it finishes as the montage
does". The engine implements that in **time, not position**: it begins blending once the montage's
remaining duration *at the current play rate* falls below the blend time. So the boundary sits at
`Length - BlendTime * Rate`, and slowing recovery pushes it later into the clip.

The first implementation treated it as the fixed `Length - BlendTime`. At the light's derived rate
of 0.94 that produced 0.282 s against an authored 0.267 — a 6% error indistinguishable from frame
jitter, and it passed. Authoring a charged at 0.500 s dropped the rate to ~0.5 and the same bug
produced **0.744 s, 49% long.** Correct solution:

>     (Length - BlendTime*R - Position) / R = RecoverySeconds
>  => R = (Length - Position) / (RecoverySeconds + BlendTime)

The blend cancels out of the position but not out of the time, which is exactly why the naive form
is wrong and why it is wrong by more the slower recovery is authored.

**The general form, and the reason this is an entry rather than a comment: a derivation verified at
one value is not verified.** The error scaled with the very quantity being introduced, so the test
that would catch it is the one that uses the feature for its purpose — authoring *different*
recoveries per tier — rather than the one that proves the code runs. The first test used identical
values on all three branches and confirmed nothing except that nothing had broken.

Measured after the fix: light 0.268/0.270/0.271 s against 0.2667, charged 0.507/0.505/0.504 s
against 0.500. Within about a frame, biased late, as everything else here is.

## 2026-08-12 — Items get names, Recovery moves up to join Lunge, and the audit gets a trigger

Three changes to the plan, made together because scrutinising the order is what exposed all three.
The mechanical details are in `CLAUDE.md` and in the retired-item-numbers table above; what is
recorded here is what was rejected.

**Numbers → names.** The numbers were introduced as *stable identifiers, not sequence*, and that
part worked — the order changed repeatedly and nothing broke. What they never did was mean
anything, so every reference cost a lookup. The evidence that the scheme had stopped paying was
already in this file: the traps had spontaneously begun writing *"before block (item 7)"* and
*"before recovery and punish windows (item 12)"*, appending the name because the number would not
carry the reader. The three things that were **never** numbered — the hover bug, the facing pass,
Slices A and B — are the ones anybody can recall unaided.

Two alternatives were rejected. **Rewriting the archive's 39 references** would violate the rule
that dated entries are never rewritten, which exists for better reasons than this one problem;
the bridge table costs one screen and breaks nothing. **Keeping the number as a hidden ID beside
the name** was rejected as two identifiers for one thing, which is the duplication rule this
project keeps relearning — a second copy is not reinforcement, it is something nobody reviews.

The cost being accepted knowingly: **a name can be wrong in a way a number cannot.** A number never
claims anything, so it can never drift; `Lunge` will be a lie if the mechanic outgrows it. That is
the same exposure any renamed symbol carries here, and it gets the same treatment — a row in the
table above, exactly as `StationaryTurnRateDegrees` got when it stopped being stationary-only.

**Recovery moves up four places to ship with Lunge.** It sat second-to-last among the gameplay
items. The argument that moved it is the one that had already moved Lunge ahead of Block, applied
one step further: advantage on block is *blockstun minus recovery*, two numbers and one felt
quantity, so authoring blockstun against a recovery nobody chose is the same error as measuring
spacing against travel nobody chose. Recovery also feeds the Light String's endlag, how long facing
stays committed, and `InputBufferSeconds` — four consumers, all of which would otherwise be tuned
against whatever is left of a montage.

**What settled it was noticing which item was already decided.** Recovery's shape was fixed on
2026-08-12 — an authored `RecoverySeconds` with a derived play rate — while Block, Light String,
Parry and Stun all still carry open design questions. An implementation item with a settled shape,
queued behind four unsettled ones and feeding numbers into all of them, is backwards regardless of
the rest of the argument.

**Merging it with Lunge rather than merely reordering it** is the smaller point and the more
practical one: both author a phase of the attack that the animation currently decides by default,
and both are named in the coil trap as the moment bespoke per-tier clips would land. Run apart,
the attack gets torn open twice against two different clip sets.

**Structure Audit is widened, and given a trigger instead of a position.** The user's call on both.
It was an audit of designer-facing property categories; it is now the project's structure entire.
*Last* was rejected as a schedule: an audit parked at the end of a list that has grown every session
it has existed is one that never runs, and "deferred until systems settle" is a condition, not a
position, so it should be written as one. **The trigger is the combat model being verified good in
play** — the prototype's actual finish line, and the first moment reorganising stops being wasted.

**It looked like it had orphaned something, and the correction is the part worth carrying.**
Facing interpolation had been parked against "the polish audit, item 14" in two header comments and
the tuning map. Item 14 was never a polish slice in `CLAUDE.md` — it was structural there and polish
in the C++ comments, one item quietly carrying two jobs, which only became visible when the item was
renamed.

**Corrected inline the same day, on the user's challenge: the work is not orphaned, it is done.**
This paragraph originally recorded it as unowned in four places, and that was wrong — a factual
error rather than a reversal, so it is fixed here rather than superseded. The interp shipped as
something better than an interp: the lock running to `EndAbility` plus `IdleTurnRateDegrees`. The
commit that did it says so explicitly — *"an interp window at the release boundary was considered
and proved unnecessary."*

**How the error was possible is the useful part.** The tuning map carried **three rows for one
complaint**, added in three consecutive commits on 2026-08-12: `f57ce1c` named `FacingLockFadeSeconds`,
`6bfbb73` said the work was deferred, and `801df58` said it was solved and that an interp is the
wrong fix. Only the last is current; the first names a deleted property. Reading the middle row and
believing it was reasonable and produced a false claim in five places, which is precisely the
duplication failure the 2026-08-12 audit catalogued — **restated here in its most instructive form,
because the duplicates were three answers to one question rather than two copies of one answer.**
A stale row does not announce itself, and a *newer* row elsewhere in the same table does not correct
it. Both stale rows are now gone.

The general form worth keeping: *renaming a thing forces you to say what it is, and that is when you
find out it was two things* — **and then to check whether the second thing is still true**, which is
the step that was skipped here.

## 2026-08-12 — Facing becomes one rate, and the rate is derived from the light's commit

**Facing had two modes: snap to the camera whenever there was movement input, turn smoothly at
500°/s otherwise. It now has one, 1200°/s, in both states.** Asked for as visual polish — the snap
"looked like a bug rather than a feature" — and it turned out to be sitting on a live gameplay
defect nobody had looked for.

### The defect, measured before anything was changed

An attack's wedge is authored in the actor's frame and freezes at the commit checkpoint. So
whatever facing has managed to turn by then *is* where the attack points. At 500°/s the light's
150 ms commit funds **75°** of turn, which is less than a 90° flick.

28 stationary flick-then-attack reps at the original settings:

| | Value |
|---|---|
| Mean absolute error at commit | **48.4°** |
| Max | 99.8° |
| Errors > 30°, i.e. outside the attack's own 60° wedge | **20 of 28 (71%)** |

**Seven of every ten flick-attacks could not have hit what the player was pointing at.** This was
live, in the stationary case — the stance a spacing game attacks from most — and it had never been
examined because the snap made the *moving* case exact and nothing displayed the difference.

### 1200 is derived, not chosen

> rate = 180° ÷ the light's `HoldUntilSeconds` → 180 / 0.15 = **1200°/s**

180° is the largest yaw error that can exist, since the delta normalises to ±180. So 1200 is the
slowest rate that can always close the gap before the wedge freezes, **from any orientation, for
any input where aim was settled before the press.** Below it, some flicks cannot finish in time;
above it buys nothing, because the camera has run out of angle to gain.

Measured: at 1000°/s, 92% of stationary attacks committed with error exactly 0. The user's own
test at 1200 confirmed the remaining cases are all continuous spinning.

**This couples the rate to the ladder.** If the light's commit moves, the guarantee lapses silently
— the same unenforced two-file coupling as `MaxWalkSpeed` and the locomotion blendspace. It is
stated in the header and in the tuning map, and that is all that holds it.

### What the snap actually bought, and what parity costs

The snap assigned yaw from the controller every frame, so its aim error was **identically zero,
always** — confirmed across 18 attacks reading `err=0.0` without exception. That is what was given
up, and it is not nothing:

| While moving | Aim error at commit |
|---|---|
| Snap | exactly 0.0°, always |
| Smooth @1200 | 86% exactly 0, mean 5.7°, thin tail under provocation |

Against a ±30° half-arc a 5.7° mean is noise, and hits are measured to the target's body with the
arc widened by the capsule it subtends — so even the 30° worst case for a completed flick still
connects. **Play preferred parity and the cost is recorded rather than denied.**

### The old objection was real about the pop and wrong about the dodge

The tuning map forbade exactly this change, on the grounds that always-smooth "reintroduces stale
facing on the first frame of input and sends dodges sideways." **That half is false, by reading and
then by play.** `ResolveDodgeDirection` resolves the direction *relative to facing* and the montage
travels relative to that *same* facing, so lag cancels out of the result entirely; only the 45°
quantisation survives. Dodges went the right way on every test.

Worth keeping as a pattern: **a warning can be right about the symptom it was written for and wrong
about the mechanism it blames.** The pop was real. The dodge reasoning was never tested.

### Decoupling the visual from the wedge was raised and rejected

The user asked whether facing and hitbox placement could be decoupled — visually smooth, mechanically
exact. Not heresy: it is the third axis of what this project already did to space (authored wedges)
and time (authored durations). Rejected on two grounds.

**Benefit and cost scale together.** Decoupling only does anything when the two diverge, and
divergence is exactly when it is harmful — a 180° flick would leave the body 100° from the hitbox.
Where it is safe it is unnecessary; where it is necessary it is unsafe.

**And it would break the tell.** This spec states that reactability is measured from the tell, not
the press. The defender reads your body. A hitbox that disagrees with the body does not make an
attack unreadable — it makes it *misreadable*, so the defender who reads correctly and steps out of
the visible arc is hit anyway. That objection is specific to PvP; single-player it would be fine.

### And then a second rate, which does not undercut the first

**Added within the hour, at the user's request: `IdleTurnRateDegrees`, 300°/s, while the character
is doing nothing at all.** 1200 is right everywhere it matters and reads badly in exactly one place
— a looping idle, spun on the spot by the camera, with no turn-in-place clip in the library to cover
it. Everywhere else it looks like intent; there it looks like a prop being rotated.

**This looks like it should break the aim guarantee and cannot**, for a reason worth stating because
it was luck turned into design: the guarantee is derived from the **worst possible** gap of 180°,
not from observed flick sizes. So however far facing drifts while idling, the windup still closes
it. What makes that true in practice is that the fast rate resumes at the **press** — `State.Attacking`
goes on at activation, so the entire 150 ms windup runs at 1200 regardless of what preceded it.
Had 1200 been fitted to the measured distribution instead of the ceiling, this split would have
broken it silently. **Derive from the bound, not from the data, and later requests come free.**

Verified rather than assumed: ~40 facing locks across deliberate edge cases, including attacking out
of a slow idle turn, produced two non-zero readings (−21.6° and +3.6°) and no error outside the
wedge at all.

**Idle means zero button presses of any kind** — the user's formulation, and a better rule than the
"stationary and not attacking" first proposed. A list of exceptions needs extending by every slice
that adds an action and is wrong in between; block, parry and every stun state cost nothing under
this one. Implemented as a virtual `IsIdle()` following `IsFacingLocked()`'s precedent: the Core
character answers only what it can see (no movement input, feet on the ground) and `ATDCombatCharacter`
ANDs in "no ability active and no buffered press". Keyed on *any* active ability rather than state
tags, so it never needs revisiting.

**The two rates are now split by whether they are allowed to be tuned by feel**, which is the more
useful distinction than fast-versus-slow: `TurnRateDegrees` is derived and moving it silently breaks
aim; `IdleTurnRateDegrees` cannot affect aim by construction and is pure taste.

### And the facing lock then grew to cover recovery, by deleting code

**Same day, once the rates were settled: the attack's facing lock now runs from the commit
checkpoint to the end of the ability rather than to the end of the release window.** Raised by the
user, who found the handback at the release window's close read as unnatural and suspected it was
their own earlier design call. It was.

**This is a combat change, not polish, and is recorded as chosen.** Recovery stops being steerable,
which is a real defensive nerf: recovery *is* the punish window, and you are now committed to a
direction throughout it. The justification is that this makes the commitment consistent — recovery
already imposes a temporal commitment, and this makes it spatial too, exactly as commit does for the
release window.

**It cost nothing to the aim guarantee, and the reason is worth keeping**: the guarantee lives
entirely between the press and the commit checkpoint, so where the lock *ends* cannot touch it. What
does change is that a chained attack begins its windup with whatever gap accumulated across the
entire previous attack, rather than a gap mostly closed during a free recovery. That is fine because
the windup is sized against the 180° ceiling rather than against typical gaps — the same property
that made the idle rate free. **Deriving from the bound keeps paying.**

An initial worry that this "spends the safety margin" was overstated and is retracted: the failure
is graceful, not a cliff. Missing a 180° close by 2° of frame jitter yields a 2° error against a
±30° half-arc.

**The implementation is a deletion.** `EndAbility` was already the guaranteed restore path — the one
every exit converges on, including cancel and death — so extending the lock meant removing the
early unlock and letting the safety net become the mechanism. An interp window at the release
boundary was considered and proved unnecessary, because **the idle rate added an hour earlier
already covers the case it would have smoothed**: a player who attacks and then does nothing is
idle, so the catch-up runs at 300°/s and eases round; a player who chains or moves is not idle, and
1200°/s is what they want. Two rates that were added for an unrelated reason turned out to make a
third change simpler.

**Two consequences to expect rather than discover.** "End of recovery" mechanically means montage
**blend-out**, ~0.25 s before the swing visibly finishes, so a sliver of visible tail still has free
facing — the item-12 trap, again. And **item 12's `RecoverySeconds` now has two jobs**: it will set
how long you are committed to a direction as well as how long you are punishable. Those are both
commitment and plausibly want the same number, but it should be a knowing choice when that slice
lands.

### Two things that came free

**The smooth branch is a turn cap.** Rate-limiting facing is exactly the mechanism that would bound
fast-spin abuse, and it arrived by deleting the branch that had no rate at all. The "drillbit spin"
of a character whipping round with the camera is gone, and it cannot return while this is the only
rotation path.

**The error is now instrumented.** `FACING LOCK` on `LogTDCombatTiming` samples the yaw error at the
instant an ability takes facing, and the debug HUD shows it live and held. Kept past the pass that
built it, because the rate is derived from a commit time that items 12 and 13 both move, and because
this class of defect is invisible without it. It is also the reason any of the above are numbers:
reading a figure off a HUD while flicking the camera and pressing attack is not something a person
can do, which was established by trying.

### Left open

**~~A buffered light's release timing reads as ambiguous.~~ Closed the same day, as accepted
behaviour rather than a defect** — the user's call, on the grounds that it was only bad while
spinning the camera and the alternative was unappealing. Recorded rather than dropped because the
two contributing effects are worth knowing: the inherent one, that a buffered press waits before
firing so press-to-hit varies across the buffer window; and the incidental one, that the ability
ends at montage **blend-out** ~0.25 s before the swing visibly finishes, so the animation a player
calibrates against is late relative to when the buffer actually fires.

**Expect it to change anyway when item 12 lands.** Authoring a real `RecoverySeconds` has to decide
whether recovery ends at blend-out or at the montage's true end, and either answer moves this. It
is closed as "not worth acting on", not as "settled forever" — which is the distinction that stops
someone reopening it as a surprise regression.

**Dodge clip selection reads as arbitrary sometimes**, and is correct. Eight 45° buckets mean the
clip can be up to 22.5° from the stick. Facing lag now shifts the bucket boundaries slightly, which
is likely why it became noticeable. Direction is always right. Fixing it means blending adjacent
dashes, which is real work for a cosmetic gain.

## 2026-08-12 — The hover was six centimetres of mesh offset, and foot IK had been hiding it

**The answer, after four wrong hypotheses across two sessions.** `ATheDreamCharacter` sets
`InitCapsuleSize(42, 96)`. `ACharacter` defaults the mesh component to Z **−90**. Nothing ever
reconciled the two — they lived in different files with no stated relationship — so the feet sat
exactly **6 cm** above the capsule bottom, in every pose, on both characters. `SKM_Manny`'s
reference pose puts its lowest point at Z = −0.02, so the mesh origin *is* the feet and the 6 cm
was the whole discrepancy.

**Why it looked like an animation bug for two sessions.** `ABP_Combat`'s foot-IK Control Rig plants
feet on the ground, so it spent 6 cm of correction every frame absorbing the offset. It was
therefore invisible everywhere the IK runs, and visible everywhere it does not — which, thanks to
Epic's template wiring (`IsSlotActive("DefaultSlot")` → `SelectFloat(0, 1)` → the rig's `Alpha`),
meant **exactly and only while a montage played.** Attacks hovered, dodges hovered, locomotion did
not. That correlation is perfect, entirely real, and points at the wrong system.

**This supersedes the 2026-08-12 entry blaming the montage's skeleton, in full.**

### What was rejected, and the mechanism by which each failed

- **The montage bound to Epic's skeleton.** Refuted by rebuilding `AM_Attack` on GDHBundle's — the
  hover survived — and independently by `AM_Dodge`, always on GDH's skeleton and hovering too.
- **`RootMotionRootLock = RefPose` on the clips.** Refuted by manipulation: set to `AnimFirstFrame`
  on the attack clip, hover unchanged. Note this hypothesis had been killed *incorrectly* a session
  earlier by an assumed control, then correctly resurrected, then correctly killed.
- **Root motion itself.** Refuted by setting `bEnableRootMotion` false: the montage still played,
  the character provably stopped travelling (X held at 200.00 across three mid-attack samples where
  it had read 156.8 / 132.3 / 134.2), and it still hovered.
- **The Slot node bypassing something upstream.** Refuted by reading the graph: the Control Rig sits
  *downstream* of the slot, so montages cannot bypass it.

**Every one of those was a sufficient explanation.** Each would have produced the symptom; none
did. The only thing that separates a sufficient explanation from the actual one is manipulating it
and watching the symptom refuse to move.

### The two methods that actually cracked it

**The crossover.** Put a clip known *not* to hover — `Idle1_IP`, which the graph's Idle state plays
via a plain SequencePlayer — through the suspect path by pointing `AM_Attack`'s segment at it. It
hovered. Same clip, same skeleton, two playback paths, opposite results: that eliminated every
clip-level explanation at once, where testing clip properties one at a time never could. **Run the
known-good input through the suspect path, rather than more variants of the suspect input.**

**A write-path control before trusting a null result.** `Docs/Working-In-Unreal.md` records that
`set_properties` can silently fail to reach the running game, which makes "I changed it and nothing
happened" ambiguous between *hypothesis wrong* and *write never landed*. So the first write was one
whose effect was **numerically measurable** — disabling root motion, which visibly stops the actor
travelling. Once that proved the write path was live, the later null results were real refutations
rather than possible tooling failures. **When a null result is the evidence, prove the instrument
first.**

### Foot IK now runs during montages, as a deliberate choice rather than a fix

The `IsSlotActive` → `SelectFloat` pair is deleted and the rig's `Alpha` is a literal 1.0, so IK
runs during montages too. **This is polish, not the fix, and the distinction is load-bearing**: with
the mesh offset corrected the raw pose is already right, confirmed by the level viewport, where no
IK runs at all. Nothing depends on the Alpha change.

It is kept because play found it better: attacking on `L_CombatTest`'s ramp adapts correctly instead
of standing at flat-ground angles. Epic disables IK during montages on the assumption that an
authored montage pose should not be second-guessed by a ground trace; that reasoning is weaker here,
where durations are authored and clips are warped to fit them anyway. The cost is that a ground
trace can move feet during a release window, which is cosmetic only — hitboxes are authored in the
actor's frame and cannot be moved by a pose.

### The offset lives in three places and only one of them is the home

Setting it in C++ was not enough. `BP_PlayerCharacter` and `BP_TrainingDummy` both carried a
serialized **−90** override on their inherited mesh component, and the placed dummy in
`L_CombatTest` carried a third copy — so the C++ default reached nothing until all three were
written. All are now −96, which should stop the Blueprints serialising an override at all, since a
CDO records only deltas from its parent. *Expected, not verified* — proving it needs a second C++
change to watch propagate.

The value belongs in `ATheDreamCharacter`'s constructor regardless, directly beside
`InitCapsuleSize`, because the two numbers must agree and adjacency is the only thing that keeps
them agreeing. Same failure shape as the `MaxWalkSpeed` ↔ blendspace coupling already in the traps
list: two numbers, two files, no enforcement.

### The ledge dip was not a bug at all, and the way it was nearly recorded as one is the point

**Walking off the ramp's edge appeared to dip the character slightly before the fall started.** It
was filed here as a foot-IK artifact — the trace finding distant ground and pulling the feet toward
it — and an argument was built on top of that reading: that the alignment fix had to land *first*,
because while the mesh was 6 cm out the IK owed a constant correction every frame and any clamp on
its displacement would have had to permit at least that much.

**The user then looked properly and there is no dip.** The ramp's edge has thickness, the resulting
face is very steep, and the character is simply *able to stand on it* — so walking off the ramp
walks briefly down that steep face before free fall begins. Nothing is pulling anything.

Two things worth keeping from that:

**A correct decision can rest partly on a wrong reason, and the reason still has to be retracted.**
Fixing the alignment before touching IK displacement was right, and it was right on its own merits —
a static 6 cm offset is a defect whatever else is true. The clamp-tunability argument was extra
justification invented for a symptom that did not exist. Had the alignment fix been *contingent* on
that argument, it would have been a correct action taken for a reason that evaporated.

**"Slightly dips" and "walks down a steep surface" are the same picture at speed.** The
distinguishing question was never about IK — it was whether the character was in a walking or a
falling state at that moment, which is one glance at a debug readout. Reaching for the subsystem
recently under suspicion is exactly the bias this bug punished four times already.

### Left open

**Whether the character should be able to perch on a face that steep is a real question, and
unexamined.** It is `MaxWalkableFloorAngle` on the movement component, and nobody has read the
value, let alone chosen it. Raised here rather than in the traps list because it is a design call
about movement rather than a latent defect — a spacing-first combat game may well not want players
standing on near-vertical geometry, but nothing about the current behaviour is broken.

## 2026-08-12 — A hitbox is authored, not traced; and facing is the price it charges

**The last dimension the animation controlled.** Windup, release and recovery are authored
durations with derived play rates — the clip warps to fit the mechanic. Space was still taken
from the art: reach was wherever the vendor's animator put the blade. It no longer is.
`FTDAttackHitbox` is six numbers in the attacker's own frame, and `MaxReachCm` is the attack's
range.

**What forced it was not tuning, it was expressiveness.** The blade trace worked. It could not
describe three of the 23 shortlisted attacks at all: `Attack4_Stage3_Complete` and
`Attack7_Stage2_Complete` are **shield bashes** and `Attack1_Stage2_Complete` leads with the
shield, so a `Sword`-socket trace follows the wrong object, and `Attack2_Stage2_Complete` is a
360° spin that a forward-facing volume cannot cover. Each would have needed a second trace source
and a per-attack switch. **A system that needs a special case for a fifth of its content is
describing the wrong thing.**

**Boxes were chosen first, then reversed to wedges before any code was written.** The user's
first call was boxes, on comprehensibility — *"I don't feel confident I can wrap my head around
your logic for the wedges"* — which is the correct instinct: an authoring primitive you cannot
picture produces numbers nobody can defend. The reversal came when the comparison was made
concrete rather than argued, and it is worth recording *what* made it concrete, because the
first explanation failed:

- A box's reach varies with angle. Measured on a realistic one — half-extent (55, 30, 60) at
  offset 110 — that is 165 cm ahead against 167.7 cm at the corner. **1.7%**, i.e. nothing.
- The same box at a wide arc over-reaches by **41%** at its corners.
- So the wedge's advantage is real but *only* on wide attacks, and its cost is zero on narrow
  ones, where the two are interchangeable.

A wedge additionally makes reach **radially constant**, so `MaxReachCm` answers "what is my
range?" in every covered direction rather than in one. That is the property a spacing-first design
wants, and it speaks the vocabulary the spec already uses for defense — block is "180° forward",
parry is "360° coverage". Offense and defense had been describing space in two different
languages and now do not.

**The honest summary of the exchange: the first pitch of wedges was worse than the idea.** The
user reversed on the *same* proposal once it was quantified. Quantify before asking someone to
accept an abstraction.

**Not an `FCollisionShape`.** The engine offers sphere, capsule and box, so a wedge is one
broad-phase sphere overlap plus an exact filter. That is *cheaper* than the four capsule sweeps it
replaced, not a compromise for it.

**Sweeping went away with the blade, and that is not an oversight.** The old trace swept
previous-to-current because a blade is thin and a thin volume tunnels past a target between
frames. A wedge is tens of cm deep and a target covers at most ~8 cm per frame at `MaxWalkSpeed`
500. The gap the sweep existed to close no longer exists; keeping it would have been cargo cult.

**Hits are measured to the target's body, not its origin.** A capsule whose near edge falls inside
`MaxReachCm` is struck, and the arc is widened at test time by the angle that capsule subtends.
Without both, reach would mean "to your centre" and the arc would compare a bearing against a
cylinder — edge hits would fail for no reason the player could see. **Reach is only a number a
player can learn if it is measured to the thing they can see.**

### Facing is the price, and it is charged at commit

An actor-frame volume needs a stable actor frame. `UpdateCameraRelativeFacing` gives none: with
movement input it sets `bUseControllerRotationYaw`, which **snaps** yaw to the camera every frame
— rotation between two frames is unbounded — and without input it turns at
`StationaryTurnRateDegrees` 500°/s, which is 75° across a 150 ms release. A player could sweep
their own hitbox through an arc the design never authored.

**So facing locks, for the release window only.** The user's call, and the reasoning holds up:
steering during windup is *good* game feel, recovery is benign, and release is the only phase
where the volume's frame is load-bearing. This deliberately keeps the whole cancellable portion of
an attack steerable.

**The lock fades in over the commit→release gap rather than snapping on.** Proposed by the user
explicitly as a hedge against prototype feel, and it is cheap: `FacingTurnScale` scales
`RotationRate.Yaw`, and a yaw rate of exactly 0 already means *no rotation* to the movement
component, so the fade's endpoint and a hard lock are the same state with no special case. It
fades back out into recovery, mirrored, for the same reason.

**Two things the fade required that were not obvious:**

- **The snap branch had to be disabled below full authority.** It ignores `RotationRate`
  entirely, so left available it would have teleported facing to the camera on any frame with
  movement input, however deep into the lock — and the fade would have been decorative while
  looking implemented.
- **The runway is 50 ms and that is all there is.** Commit fires at `HoldUntilSeconds` and release
  at `ReleaseAtSeconds`: 0.15/0.20, 0.45/0.50, 0.70/0.75. A linear ramp from 500°/s over 50 ms
  bleeds off about **12° of turn** — enough to remove the clunk, not enough to see. Widening it
  means moving the input boundary below 150 ms or release above 200 ms, which is a ladder
  decision, not a free one. The fade is clamped to the gap rather than allowed to overrun it,
  because facing still moving on the frame the hitbox appears is the whole failure being avoided.

**Restoration lives in `EndAbility`, not on the montage delegates.** Every exit funnels there —
completed, blended out, interrupted, cancelled, and death's `CancelAllAbilities` — so it cannot
miss a path. A stranded lock is a character who can never turn again with nothing to announce it,
and this project has already shipped one bug of exactly that shape (`bJumpRegenPauseActive`
stranded by dying airborne). **Restore where the paths converge, never on each path.**

### Two deliberate holes, named

**An empty `Hitboxes` array means an attack that cannot hit**, and it is legal. A swing with no
damaging volume is a coherent thing to author. But removing the per-branch `TraceRadius` left
every existing branch deserialising with an empty array, so the per-branch lookup **falls back to
the ability's set** rather than to nothing — without that, every attack in the project would have
gone silently damage-less between the rebuild and the content pass.

**`HeightMinCm`/`HeightMaxCm` are nearly inert today.** Everyone is the same standing capsule, so
the band only discriminates on a slope or against a jump. Authored generously and worth revisiting
when there are crouch or knockdown states worth cutting under.

**Nothing here has been played.** The starting wedges are a guess: nobody has measured where the
light's blade actually is at its impact frame, and the numbers that preceded them were tuned
against a fist.

## 2026-08-12 — The real cause was an engine default, and the facing fade was a red herring

**The bug:** standing still, turn the camera 180°, chain light attacks, and each swing lands a
little nearer to where you are looking without ever arriving. Sluggish, and the opposite of the
precision the design is for.

**The cause, and it was never ours:** `bAllowPhysicsRotationDuringAnimRootMotion` defaults to
**false** in `UCharacterMovementComponent`. It gates `PhysicsRotation()`, which is what implements
`bUseControllerDesiredRotation` — our **smooth** turn. So for the entire duration of any montage
carrying root motion, a stationary character cannot turn at all. Attacks have root motion, so this
has been true since attacks existed. Turning only happened in the gaps between montages, which is
exactly what "a little nearer each time" looks like.

The **snap** branch was unaffected throughout, because `bUseControllerRotationYaw` is applied by
`APawn::FaceRotation` through the Controller rather than by CMC. That asymmetry is the whole
puzzle: **the bug existed only while stationary**, and any test that held a movement key would
have shown a perfectly responsive character.

**Two wrong diagnoses preceded the right one, and the pattern in them is worth more than the fix.**

*First:* the facing fade was blamed, on the grounds that any scale below 1 disables the snap. That
is **true**, and it was not the cause. A *sufficient* explanation was mistaken for the *actual*
one, and the fade was deleted without the bug moving. **A mechanism that would produce the symptom
is not evidence that it did.**

*Second:* the hover bug's first hypothesis was `RootMotionRootLock` on the attack clip,
disconfirmed by checking the dodge — same setting, same skeleton, no hover.

Both were caught the same way, and it is the transferable part: **find a case where the system
works and compare.** The dodge disconfirmed the hover hypothesis; standing-versus-moving
disconfirmed the fade hypothesis and located the real one in a single test that changed no code.
*A working case is the cheapest instrument available, and neither of these needed a build.*

**Enabling the flag then broke the dodge, which is the interesting half.** Dodges became steerable
mid-animation — too much control for a move costing half the stamina bar. The reason is worth
stating precisely: **the dodge never asked for a committed direction, it inherited one** from a
suppression that was never about dodges at all. Root motion carries a character along the
montage's authored path *relative to its facing*, so a character free to turn steers the dodge
itself.

So the dodge now locks facing explicitly, through the same `bAbilityFacingLocked` the attack uses,
and the property is renamed from `bAttackFacingLocked` because it stopped being an attack concept
the moment a second ability needed it. **The general form: when a behaviour turns out to have been
free, someone has been relying on it without declaring it — and the fix is to declare it, not to
restore the accident.** This project has the same shape recorded twice already: root motion
replicating for free, and CMC's missing-Controller early-out keeping an unguarded facing update
harmless.

**The flag lives in `ATheDreamCharacter`'s constructor, beside the movement settings it belongs
with**, and the Blueprint override that first proved it was cleared. A Blueprint override silently
shadows a C++ default — identical values today, and a trap the moment anyone changes the C++ one.

## 2026-08-12 — Two facing-fade bugs, and how both got past a green regression pass

The fade shipped broken in both directions. Recorded because neither failure was in the code's
logic — one was in a *comment*, and one was in an assumption about two systems being continuous.

**The hard lock never arrived, because `FInterpConstantTo` does not snap on a zero rate.** Its
step is `InterpSpeed * DeltaTime`, so a rate of 0 gives a step of 0 and it returns the current
value unchanged — every frame, permanently. `SetFacingAuthority(0, 0)` at the release window's
opening therefore *froze* the scale wherever the 50 ms commit fade had reached rather than forcing
it to zero, and 50 ms is three frames, so it routinely had not arrived. The character stayed
partly steerable through the frames its hitbox was live — the exact condition the lock exists to
prevent.

**The code carried a comment asserting the opposite**, written at the same time as the call that
depended on it. **An assumption stated confidently in a comment is not evidence, and it is worse
than no comment**, because it stops the next reader checking. Nothing verified it and it was
wrong.

**The unlock ended in a jump-cut, because the two rotation modes are not continuous.** The smooth
branch turns at a rate; the snap branch teleports. The fade eased the rate from 0 up to full and
then, the instant the scale reached 1, handed over to the snap — so **the last step of a blend was
the largest one**, covering whatever yaw error had accumulated while the camera moved and the
character could not. Fading a parameter across a boundary does not smooth the boundary. Fixed with
`bFacingHandoffPending`, which keeps the smooth branch in charge until the gap is under about one
frame of turn, and latches so ordinary locomotion is untouched.

**How both got past a regression pass that genuinely was green.** The pass verified the build
*before* the recovery-length unlock existed. The unlock was then written, compiled, and committed
in the same batch — under a message whose first line reported the regression pass as clean. **It
was clean, about different code.** The rule is that pending *correctness* verification blocks a
push, and this violated it in the way that is hardest to notice: not by skipping verification, but
by bundling unverified work into a commit whose message described verifying something else.

The user caught it in play and named the reason it survived their own testing too — 50 ms is at
the edge of perceptibility, so *"I wasn't sure if I could even perceive 50ms, so I didn't
scrutinize it enough."* **A change small enough to be hard to perceive is not a change small
enough to skip verifying**; it is one that needs a deliberate test rather than a glance.

> **Wrong, confirmed in play 2026-08-12.** `AM_Attack` was rebuilt on GDHBundle's skeleton and
> **the hover survived it.** The reasoning below is a second disconfirmed hypothesis, kept because
> the *method* it argues for is the one that eventually matters and because two wrong diagnoses on
> one bug is itself the lesson. The rebuild was still correct on its own terms — it removed a real
> `CompatibleSkeletons` dependency — it simply was not this. The live bug and the next experiment
> are in the known-traps section.
>
> The error worth naming: the skeletons *did* differ, and that difference *was* real, so a true
> observation was promoted to a cause because it was the only difference anyone had found. **A
> difference that explains the symptom is not the same as the difference that causes it**, which
> is the identical mistake made about the facing fade an hour earlier — twice in one session, on
> two unrelated bugs.

## 2026-08-12 — The attack montage hovers because it is bound to the wrong skeleton

Diagnosed, not yet fixed — the fix is the user's call. Filed here because the *method* mattered
more than the finding, and because the first hypothesis was wrong.

**Symptom, present for some time and only raised once it started to matter:** the character floats
just above the ground while attacking, and not during locomotion.

**The first hypothesis was `RootMotionRootLock = RefPose` on the attack clip, and it was
disconfirmed by checking the dodge**, which is also root motion and does not hover. Both clips
carry the identical setting, and both sit on the identical skeleton. *A property that is the same
on the working case and the broken case explains neither.*

**What actually differs is the montage.** `AM_Dodge` is built on GDHBundle's `SK_Mannequin`;
`AM_LightAttack_01` is built on **Epic's**, at `/Game/Characters/Mannequins/`. `RefPose` locks the
root bone to the *reference pose* — resolved against the montage's skeleton — so the attack pins a
GDH-authored clip's root to Epic's ref pose height, and the two mannequins disagree about it. Root
motion is what makes the lock active, which is why locomotion is unaffected.

**This is a second symptom of a fragility already on the regression checklist.** That entry
records the Epic-skeleton binding as load-bearing for the reverse `CompatibleSkeletons` entry, and
warns that losing it makes the attack stop playing *silently*. Nobody had connected the same
binding to a visible-but-tolerated art bug sitting next to it. **A known-fragile arrangement is
worth re-reading whenever anything nearby looks wrong**, because the second consequence of a
compromise rarely announces that it shares a cause with the first.

Two candidate fixes, deliberately not chosen here: retarget the clip's `RootMotionRootLock` to
`AnimFirstFrame`, which is one property write but edits vendor content; or rebuild
`AM_LightAttack_01` on GDH's skeleton, which addresses the root cause and drops the
`CompatibleSkeletons` dependency, at the cost of re-placing the `Release Window` notify by hand
since montages are not scriptable.

## 2026-08-12 — The facing unlock is asymmetrical with the lock, and bodies stop blocking the camera

Two results from item 6's regression pass. Both are polish, and both are recorded because the
shape is arguable rather than obvious.

**The unlock spreads across half of recovery; the lock still takes 50 ms.** Play's verdict on the
symmetrical version was that it read *"unpolished"* and snappy — and the asymmetry is not a
compromise, it is the two ends having genuinely different constraints. **Going in, the fade is
boxed in by the commit-to-release gap**: anything longer would still be moving when the hitbox
appears, which is the failure the lock exists to prevent. **Coming out there is no deadline at
all**, and the swing is settling anyway, so the fade has the whole tail to live in. Symmetry was
the assumption, not the requirement, and nothing had tested it.

**Authored as a fraction of recovery, which contradicts the tuning map's own advice one row
above, deliberately.** That row says dodge recovery must be absolute seconds rather than a
fraction, because it is a *punish window* judged against an attacker's startup and a fraction
would silently shrink it below usable on any retune. **The distinguishing question is whether an
opponent is on the other side of the number.** Nobody is punished by a facing ease; it is polish
on an animation, and it *should* stretch when the animation does. Recorded because the two rules
look contradictory side by side and the next reader deserves the criterion rather than the verdict.

**Restoration had to learn to leave an in-flight fade alone.** `EndAbility` fires at the montage's
*blend-out*, which is part-way through a recovery-length fade, so restoring unconditionally there
would re-time a fade already going the right way and snap exactly the tail this change exists to
smooth. `EnsureFacingRestored` acts only when facing is still held. **The general form: a
guaranteed-cleanup path and a gradual-transition path want opposite things from the same call**,
and the cleanup one is the one that must yield, because it is the one that cannot know why it ran.

Note the fade runs on the *character's* tick rather than the ability's, so it completes after the
ability has ended. That is what makes measuring against the montage's true end honest instead of
against blend-out — the fade is chasing the animation the player watches, not the ability's
lifetime. Anything wanting *mechanical* recovery must still subtract the blend, which is item 12's.

**Combatants no longer block the camera boom's collision probe.** The spring arm sweeps on
`ECC_Camera`, and characters blocked it, so an opponent at melee range yanked the camera forward —
*melee spends all of its time at exactly the distance that triggers this*, which is why it read as
a bug rather than as the camera doing its job. Fixed by making capsule and mesh ignore that one
channel, deliberately rather than by switching off `bDoCollisionTest`: level geometry must still
push the camera in or it ends up inside a wall. The accepted cost is the camera passing through an
opponent at very close range, which is the conventional trade and far less disruptive.

## 2026-08-12 — Root motion scaling is not enough control; Lunge becomes its own mechanic

**Play verdict, from the user, after tuning `RootMotionScale` to 3.0:** it *"feels nice, roughly,
but it's still a bit too animation-driven for my tastes."* **This supersedes the preference stated
in "2026-08-11 — The light is reactable at 250 ms", consequence two,** which said to prefer
scaling root motion over code-driven movement and to treat code as the exception.

**That entry named this exact outcome as the trigger for reversing it** — *"a clip that is
stationary during the window that needs travel is the case that genuinely forces code. Pay that
knowingly rather than by default."* What it did not anticipate is that the failure would be one of
*shape* rather than *absence*. Scaling multiplies whatever curve the animator drew, so it
faithfully reproduces their acceleration, their pauses and their stop, three times larger. The
travel is not missing; it is someone else's, amplified. That is the general form worth keeping:
**a multiplier cannot decouple you from an authored curve, it can only make the curve louder.**

**The mechanic is named Lunge** and is authored without reference to the clip — distance and
timing chosen by a designer, the animation left to disagree if it must, on the same terms the
wedge already accepts. It is the spatial twin of what the hitbox change did: the attack decides
what happens and the art is fitted to it, not consulted about it.

**The netcode objection, and why it is answerable rather than fatal.** The 2026-08-11 preference
was not aesthetic. Root motion is replicated by CMC for free, and the netcode audit called that
*"the single most valuable accident in the codebase"* precisely because a hand-rolled displacement
system would have to be rewritten to network. Naively, Lunge spends that accident.

It does not have to. **GAS ships network-predicted movement in
`UAbilityTask_ApplyRootMotion*`** — `ConstantForce`, `MoveToLocation`, `MoveToActorForce` — which
drive CMC's *root motion source* system rather than the animation's. They are fully authored
(distance, duration, easing curve, whether it overrides or adds to other movement), completely
independent of the clip, **and predicted and replicated by the same machinery that makes animation
root motion safe.** So the honest statement is not "code-driven movement costs us the network
property"; it is **"hand-rolled code-driven movement would, and GAS's does not."** Lunge must be
built on a root motion source. `SetActorLocation`, `AddMovementInput` and `LaunchCharacter` are
all the version the audit was right to fear.

**Two constraints carry over from the scale it replaces, unchanged.**

*Lunge during the windup may not differ by tier*, for the reason the two-scale split exists: all
three tiers share one windup so the light carries no distinguishing tell, and a charged that
lunged further from the press would announce itself from frame one. Whatever Lunge's authored
shape turns out to be, it inherits this.

*And it must decide what happens to the clip's own root motion.* Left alone the two add, so the
authored distance would be a lie by whatever the animation contributes. The likely answer is that
Lunge takes `RootMotionScale` to 0 and owns displacement outright — which is the *decoupling* the
user asked for, stated precisely.

**Deliberately left open**, because none of it can be settled without play: whether Lunge is
authored as a distance plus a duration or as a distance plus a curve, whether its window is
phase-relative (windup / release / recovery) or absolute seconds, and whether it is per branch or
per attack. Recorded as an item rather than designed here.

## 2026-08-12 — The three wedges stay uniform until each attack has its own animation

The user's call, made while tuning: all three tiers currently carry the same authored wedge, and
that is deliberate rather than unfinished. Re-authoring waits until each attack has a bespoke
animation, at which point reach and Lunge get tuned together.

Worth an entry because the *state looks like an omission* — the spec says heavy has higher range
and charged the highest, and the asset presently disagrees with the spec. Anyone reading
`GA_Attack` without this would reasonably assume the per-branch differentiation was forgotten and
"fix" it against a light that is the only tier whose animation has been chosen.

The reasoning: reach and travel are one felt quantity, and the ladder cannot be differentiated
honestly while two of its three tiers are still playing the light's clip. Differentiating now
would tune heavy and charged against an animation neither will ship with.

## 2026-08-12 — Attack displacement is two scales, because the windup may not differ by tier

Item 6's forward motion. The 2026-08-11 decision already said displacement is authored per attack
and that scaling root motion beats driving movement in code. What that entry did not anticipate is
**where the scale is allowed to change**, and the answer is not a matter of taste.

**Travel during the windup must be identical across all three tiers.** The ladder is built on the
tiers sharing one windup at one play rate, which is what leaves the light with no tell that
distinguishes it from a heavy. A charged that pulled further forward from the press would announce
itself from frame one — *the same failure as moving the coil earlier, arriving through the
movement system instead of the animation one.* Nothing in the code would have stopped a per-branch
scale being applied at activation, and it would have quietly cost the property the whole model
exists to protect.

So displacement is two numbers that multiply:

| Knob | Applies from | Scope |
|---|---|---|
| `UTDMeleeAttackAbility::RootMotionScale` | the press | shared by every tier |
| `FTDAttackBranch::RootMotionScale` | the commit checkpoint | that branch only |

**The consequence to expect rather than discover: a branch can only differentiate the travel its
clip performs after commit.** For the light that is 50 ms before the hitbox appears. If a tier
needs to cover meaningfully more ground than that allows, the answer is **its own clip** — which
the charged is already argued to want on two independent grounds — and not a larger number here.
Whoever finds the charged's lunge unresponsive to `RootMotionScale` should read this before
concluding the knob is broken.

Applied through `ACharacter::SetAnimRootMotionTranslationScale` behind the same role gate
`UAbilityTask_PlayMontageAndWait` uses, so a simulated proxy never scales movement the server has
already accounted for. The task resets the scale to 1 when it is destroyed, which covers cancel,
interrupt and death without a second cleanup path — the same reasoning that put the facing
restore in `EndAbility`.

**Deliberately not built: the dodge's target-distance-and-measurement shape.** `DodgeTargetDistanceCm`
with per-direction `MeasuredTravelCm` exists because eight clips disagreed by 90.6 uu. All three
attack tiers currently share **one** montage, so there is nothing to disagree — a plain scale is
the cheaper experiment and it runs first, exactly as the dodge's did. **The moment the charged
gets its own clip, this becomes the wrong shape**, because a scale authored against one clip's
travel means nothing against another's. That is the trigger to switch, and it is foreseeable now
rather than discoverable later.

## 2026-08-12 — The coil may not survive the faster light, and heavy would then blend from it

**Noted, not decided, and nothing is built for it.** Raised by the user while settling hitboxes,
and recorded because it would dissolve a mechanism three other documents currently treat as
fixed — the sort of thing that is expensive to reconstruct from memory two slices later.

The claim: the freedom won by admitting the light is reactable and moving it to 200 ms may remove
the need for the shared-windup-plus-coil structure entirely. Instead of one windup whose tier is
decided by a hold, and a coil that exists to tell the defender which tier arrived, **the light
would blend out into bespoke authored heavy and charged attacks, on reaction.** Heavy would then
have to blend into charged the same way.

What it would dissolve, so the scale is visible: `CoilEndSeconds` and the derived coil rate, the
escalation checkpoint chain in `UTDChargedAttackAbility`, and — most consequentially — the
**windup-compatibility criterion** that currently governs clip selection in
`Docs/Animation-Library.md`. That criterion exists *only* because all three tiers share one
windup; attacks authored per tier can look like whatever they are. It would also retire the
2026-08-12 finding that the coil has no room on a short clip, by removing the coil rather than
solving it.

Two things pull in its favour that were decided independently: per-branch hitboxes already exist,
so a tier that becomes its own attack needs no new spatial plumbing, and the charged was already
argued to want its own clip on mechanism.

Not costed, not scheduled, and it interacts with item 9's string and item 12's punish maths. Like
the chain-rules fork, it is cheapest to settle before those and expensive after.

## 2026-08-12 — Recovery stays at 1.0 although shorter felt better, and becomes an authored duration

**Two decisions about the attack tail, one of which looks like it breaks this project's own rule
and does not.**

**`RecoveryPlayRate` stays at 1.0.** Shorter recovery was tried and *felt good* — and was rejected
anyway. The reason: the good feeling comes from **removing a cost the design wants to exist.**
Avoiding recovery is supposed to require *choosing to combo into another light*; whiffing, or
declining to continue, is supposed to leave you punishable. A globally shorter tail hands that
safety out for free and deletes the decision.

**This is not "play wins" being overruled.** That rule is about a *designed distinction* not
surviving contact with feel — an argued position losing to evidence. This is the opposite shape: a
mechanic whose whole job is to impose a cost, feeling better with the cost reduced. Of course it
does. "Infinite stamina feels good" is the same sentence. The test is not *is this pleasant*, it
is *does the pleasantness come from the mechanic working or from it not working*.

Deliberately deferred rather than settled: **re-evaluate once lights can actually chain**, because
until item 9 exists the combo escape hatch is unavailable and recovery is being judged with half
its context missing.

**Recovery becomes an authored duration, matching windup and release.** Requested by the user, and
it generalises what two thirds of the model already does:

| Phase | Authored as | Rate derived from |
|---|---|---|
| Windup | `ReleaseAtSeconds` | `ReleaseStartSeconds ÷ ReleaseAtSeconds` |
| Release | `ReleaseSeconds` | notify width ÷ `ReleaseSeconds` |
| **Recovery** | **`RecoverySeconds`** *(to build)* | remaining montage ÷ `RecoverySeconds` |

**The notify denotes release; windup is whatever precedes it and recovery whatever follows.** So an
attack is specified as three durations and the animation warps to fit — which is the same
principle already stated as *"mechanics decide how long, the animation gets all of that time,"*
applied to the one phase that never got it. `RecoveryPlayRate` becomes the derived value it should
always have been rather than a knob.

**The complication to solve rather than discover:** the montage's blend-out eats the end of
recovery, because the ability ends when blending *starts* — which is the trap already filed against
item 12. Authoring a recovery duration means deciding whether that duration ends at blend-out or at
the montage's true end, and deriving the rate accordingly. Getting it wrong makes every authored
recovery silently shorter than its number, which is precisely the failure the trap describes.

## 2026-08-12 — The coil has no room on a short clip, and the light's floor is not `HoldUntilSeconds`

Found while swapping the punch for a real sword clip. Three findings, in ascending order of how
much they constrain the design.

**The release window's authored width and its mechanical duration are separate numbers, and their
ratio is a play rate nobody chose.** The notify on `AM_LightAttack_01` is **0.2952** of montage
wide; `ReleaseSeconds` is **0.09**. The ability stretches one to the other, so the swing plays its
damaging frames at **3.28×** — reported by play as the release passing "in what feels like a
microsecond". Neither number is wrong on its own. **When they match, the strike plays at 1.0×
through its own active frames**, which is the only setting where the animation and the mechanic
agree. Author the notify to the frames where the blade genuinely sweeps, then choose
`ReleaseSeconds` as a gameplay call, and expect to reconcile them.

**The light's floor is `HoldUntilSeconds` *plus frame jitter*, not `HoldUntilSeconds`.** I claimed
`ReleaseAtSeconds` 0.22 was reachable against a 0.2 hold boundary. On paper it is; in practice
20 ms is about one frame at 60 fps. The escalation timer actually fired at **0.212–0.224 s**, and
at the resulting rate the montage reached the damage point at **0.220 s** — so the strike went
live at the same instant the game decided whether it was a light or a heavy. The coil was skipped
seven times out of seven with a **negative** distance, and the heavy played as *"a slow light
attack"*, which is exactly what an uncoiled heavy is. **Leave several frames of margin**; 0.25
against a 0.2 boundary is comfortable, 0.24 is the practical floor.

**The structural finding, which no tuning fixes.** The coil must hold the montage between where
escalation fires and `ReleaseStartSeconds`. On this clip that gap is **0.016–0.028 of montage**,
and it has to be spread across the wait for the charged's commit at 0.7 s — a coil play rate
around **0.03–0.05×**, which is a freeze rather than a slowdown.

The cause is a ratio, not a value: **the damage point sits at 27.6% of a 0.967 s clip, while the
charged waits 750 ms.** The windup rate is derived from the *light's* timing, so it carries the
montage to the damage point quickly and everything after must be stalled in what little montage
remains. Moving the notify later buys coil room and spends it on a fast-forwarded windup; moving
it earlier does the reverse. There is no setting that gives both.

**This is the real argument for the charged branching to its own clip** — previously made on
appearance, and now on mechanism: a longer clip with a later damage point is the only thing that
gives the coil somewhere to live. It does **not** rescue the heavy, which shares the light's clip
and has the same squeeze. If the heavy reads as frozen rather than held, the heavy needs its own
clip too, and the "one animation, three tiers" economy is gone.

Worth noting the two ungated warnings both earned their keep here. *Coil skipped* named the
second problem outright, and *Release Window opened at X but ReleaseStartSeconds is Y* is what
made the first diagnosable at all. Both were written speculatively; both were the first thing to
say something true.

## 2026-08-11 — The light is reactable at 250 ms, and three consequences of admitting it

Found in play while judging attack candidates. **This supersedes the offense model's claim that
the light is "unreactable — it never coils, so there is no tell at all."**

**The claim conflated two things.** The light has no *distinguishing* tell — you cannot tell it
from a heavy before the coil, which is true and is the property the ladder is built on. But the
montage starts on the press, so **the windup is a tell from frame one**, and 250 ms of visible
windup sits at simple human reaction time. That is the same edge the docs already flag for the
heavy's ~240 ms coil→damaging window; nobody had noticed the light was standing on it too.

The user's report: *"250 ms is reactable for me in PIE when I'm focused."*

**I hedged this and the hedge was wrong.** The original entry argued that PIE against a
stationary dummy is the easiest possible case, so the finding meant *"250 ms is closer to the edge
than assumed"* rather than *"250 ms is reactable in a match."* The user corrected it 2026-08-12
with evidence I had no way to derive: they can react to a 250 ms light **every time, with zero
practice**, and they reached top-25 nationally in both New World and Divine Knockout — where the
same phenomenon was reported and the developers never acknowledged it.

**The load-bearing point is about who the game is for.** Strong players manage their mental stack
well enough to *simulate the vacuum* in the middle of chaos, so "it is harder in a real match" is
not the reassurance it sounds like. It is precisely the reasoning that ships combat only the
developers enjoy. Where a design targets players who will pick it apart, the vacuum number is the
real number.

**So 250 ms is reactable, full stop**, and the light moves to hitting at 200 ms with its input
boundary at 150 ms — that boundary being the measured floor for *trivially consistent* inputs from
the same prior focus testing, not a guess.

**Consequence one: lights get faster.** Two separate knobs, and they are easy to confuse. The
*mechanical* release time is `ReleaseAtSeconds` (250 ms). *Where the strike visually lands in the
clip* is a property of the animation and of `ReleaseStartSeconds`. A clip whose impact frame sits
late reads as slow even at a correct 250 ms, which is what the user observed — *"this particular
animation just doesn't get to where I think its release should be as soon."* Move the second
before the first.

**Consequence two: displacement is authored per attack, not taken from the clips.** Every
candidate needs several times more forward travel than it ships with, and foot sliding is
accepted. **Prefer scaling root motion over code-driven movement** — scaling several times over
is still root motion, so CMC still replicates it, and the netcode audit's "most valuable accident"
survives. Scaling cannot reshape *when* motion happens, only how much, so a clip that is
stationary during the window that needs travel is the case that genuinely forces code. Pay that
knowingly rather than by default.

**Consequence three: attacks are chosen per slot, not taken as vendor strings.** This reverses
the reasoning that a whole authored family should be preferred for guaranteed inter-hit flow. The
trade, stated by the user and accepted with eyes open: **more precision in feel, in exchange for
more aesthetic jank** — every transition becomes one we author and blend rather than one the
vendor authored. Given that lights need speeding, displacement needs authoring, and the charged
branches to its own clip anyway, the vendor's flow was already being broken in three places; the
coherence it bought was mostly notional.

## 2026-08-11 — Chain rules come as a package: the light guarantee and heavy→light are coupled

**Noted, not decided.** Recorded because the coupling is invisible in the rules as written, and
someone will eventually propose relaxing half of it.

`CLAUDE.md` states two chain rules that look independent: *"Any hit in the string guarantees the
rest"* and *"Some heavies can chain into further heavies; never into lights."* They are the same
decision seen twice. **The guarantee is why heavy→light must be forbidden** — allow both and a
heavy becomes an opener into guaranteed damage, which is a confirm off a move that is already
safe on block and knocks down.

So there are two coherent models, and the project is currently in the first:

| | Lights guarantee follow-ups | Heavy → light |
|---|---|---|
| **DKO-shaped — current** | yes | **forbidden** |
| **New World-shaped** | **no** | allowed |

The user raised the second while looking at animations, and named the constraint that keeps it
honest: going New World means *"lights do not guarantee follow-up lights."* Either package is
defensible. Mixing them is not — take the guarantee from one and the chaining freedom from the
other and light offense becomes a confirm off everything.

Kept as an open fork rather than a ruling, because nothing has been played that bears on it yet.
What it changes if revisited: item 9's string, blockstun values in item 7, and the punish maths in
item 12 — so it is cheapest to settle before item 9 and expensive after.

Also settled in passing, by feel rather than argument: **heavy into heavy stays**, which is what
the rule already said, and **charged does not chain out of** — consistent with the heavy endlag
its design already carries.

## 2026-08-11 — V3 becomes the base stance, and one uniform dodge scale does not survive the swap

Two reversals, both driven by play, both superseding entries that reasoned well from what was
known at the time.

**V3 replaces V1 for locomotion, idle and the dodge.** This supersedes the pack choice in
"2026-08-10 — Facing is camera-relative", which picked V1 to match `AM_Dodge`'s `Dash` clips on
the grounds that *"mixing packs risks two stances reading as two characters."*

**That argument was about internal consistency and never about how V1 looked** — nobody had
compared the packs on appearance, because nobody had thought to. The user did, in play: V1's
whole set reads as though the character is permanently blocking with the shield. V3 is neutral.
Confirmed against the assets before the swap, and again in play after it.

**The no-mixing principle it rested on was unachievable from the start, and this is the part
worth carrying.** `SwordAndShieldAnimV1` contains **no `Hit` clips and no `Death` clips at all**.
Item 11 was always going to force a cross-pack mix; the only question was where the seam fell.
Choosing V1 for attacks would have put it on the swing-and-react interaction, which is the most
observed exchange in the game. So the principle that justified V1 could not have been honoured by
V1. *A constraint nobody re-checked outlived the facts that made it true.*

**V1 is kept, for the one thing it is better at.** It owns the held guard — `DefenseStart` /
`Defense_Loop` / `DefenseEnd` plus four directional `Defense_Hit` and four die-while-blocking
variants, against V3's single non-directional block impact and no exit clip. So item 7 stays on
V1, and the user's proposal to use V1 as a *blocking locomotion set* gets stronger with this
result rather than weaker: V1 turns out to be specifically guard-shaped, which is a defect as a
neutral stance and exactly right as a guard.

**Second reversal: item 5's finding that a single uniform `DodgeRootMotionScale` is the right
shape was correct for V1 and did not survive.** That entry measured all eight directions
(mean 404.9, spread 31.5 uu), found them in agreement, and concluded no per-direction data was
needed — while explicitly naming the condition that would change it. V3's clips meet that
condition: **spread 90.6 uu**, 371.9 left against 462.6 right.

**How it was established, because the method is the transferable part.** The user's first
hypothesis was that the variance was their own input error, and tested it by dodging left and
right deliberately. Repeat measurements came back *identical* — L 372.0 then 371.9, R 462.6
twice. Within-direction repeatability is ≤6 uu against a 90.6 uu between-direction spread, about
15:1, which converts "probably the clips" into "certainly the clips". **A disconfirmed hypothesis
settled this faster than more measurement would have.**

Then the amplifier: **dodging up a ramp**. On flat ground a 90 uu difference is arguable; on a
slope it becomes a visible difference in how far up you end up, and the user reported right
reaching nearly the top where left managed two thirds. Worth remembering as a technique — *a
slope converts a distance difference into a position difference the eye can judge.*

**The fix is an authored target with derived corrections, not eight authored scales.**
`DodgeTargetDistanceCm` (405) is the distance knob; `MeasuredTravelCm` holds what each clip
actually travels; the per-direction scale is `Target / Measured`. Retuning distance stays one
number. Eight authored scales were rejected for the same reason recovery is authored in absolute
time rather than as a fraction of the dodge: it couples "how far" to "which direction" and makes
every future retune an eight-value edit that can silently drift apart.

405 is deliberately **item 5's play-verified V1 distance**, not V3's incidental 413 mean — the
feel that play already approved is preserved while only the asymmetry is removed.

Verified: 17 dodges across all eight directions, mean **405.5**, spread **21.3 uu** — tighter
than V1 ever was. Residual variance sits in the diagonals (FL 407.1 vs 419.8) where cardinals
repeat within 1 uu, which is input precision rather than clips and is well inside item 5's
perceptibility threshold.

**`MeasuredTravelCm` is calibration, not tuning, and it is now a standing obligation: re-measure
it whenever the dodge montage is rebuilt from different clips.** Stale values do not fail
loudly — they quietly reintroduce exactly the per-direction bias they exist to remove.

## 2026-08-11 — Item 6's clip candidates, and the play-rate figure nobody has measured

Measured 2026-08-10, moved here from `CLAUDE.md` on 2026-08-11 because it is working data and an
unverified assumption, not a rule. The assumption is the reason it earns an entry.

**The pack splits every attack into a short mid-chain strike and a long terminal one.**
`Attack3_Stage1_RM` **0.73 s** and `Attack7_Stage2_RM` **0.70 s** are strike-only and chain;
terminal stages (`Attack3_Stage2` 2.20 s, `Attack7_Stage3` 2.27 s) carry a 2 s+ recovery to idle.
The standalone `AttackN_RM` clips are those same strikes wrapped in idle at both ends and run
**1.50 s** (`Attack9`, the shortest) to **6.60 s** (`Attack8`).

**The ~0.7 s chain stages are the candidates.** Windup rate scales with where the impact frame
sits, so against our current 1.0 s montage with its impact 36% in, a 0.7 s clip lands near
**1.0×** — better than today's 1.44× — while `Attack9` implies ~2.2× and `Attack1` ~7.4×.

**Those rate figures assume the pack shares our clip's impact proportion, and that is not
measured.** Impact position is unreadable through the MCP toolset, which is the same gap that
forces `ReleaseStartSeconds` to duplicate the notify by hand in the first place. So the numbers
above are an *argument for previewing*, not a result — and the failure mode if the assumption is
wrong is not a bad rate, it is a `ReleaseStartSeconds` that has drifted from the notify it was
copied from, which makes an attack silently stop dealing damage. Confirm on a preview before
committing to a family.

**`Attack7` is favoured for a reason that is not about rate:** the pack ships native combo
families, and `Attack7` has three authored stages (`Attack3`/`4`/`6`/`10` have two). That is
exactly what item 9's 2–4 hit light string wants, and choosing a family without the string in
mind means placing the `Release Window` notifies twice.

> **Corrected the same day, by measuring both packs. Two errors above.**
>
> **1. The durations are V1's, and the entry never said so.** `Attack7_Stage2_RM` exists in all
> three `SwordShield` packs *and* in `Spear`; the clip name alone does not identify a pack.
> Confirmed by measurement: V1's is 0.700 s, V3's is **0.467 s**.
>
> **2. "Three authored stages" does not mean three usable strikes**, and this is the error that
> would have cost a re-do. Nearly every family in **both** packs is *long opener → one short
> strike → long terminal*: V1 `Attack7` is 1.667 / 0.700 / 2.267; V3 `Attack7` is 1.500 / 0.467 /
> 2.333; V3 `Attack6` is 1.500 / 0.433 / 2.200. One chainable strike per family, not three. Item
> 9's string must therefore be assembled from short stages **across** families, or accept uneven
> lengths. The single exception found is **V3 `Attack4`**, the only four-stage family, at
> 0.600 / 1.167 / **0.667** / 2.333 — two short stages inside one authored chain.
>
> **And a correction to the reasoning, not just the data: shorter is not better.** The target is
> a clip whose impact frame lands at 250 ms at a play rate near **1.0**, which at an assumed 36%
> impact position means **L ≈ 0.70 s**. A 0.433 s clip implies a rate of **0.62** — slow motion,
> which is as much an artifact as the 1.44× it would replace. This kills the tempting reading
> that V3's snappier clips are automatically the better source.
>
> **Both packs have 0.700 s candidates, so length does not discriminate between them** — V1
> `Attack4_Stage1` and `Attack7_Stage2`, V3 `Attack1_Stage2` and `Attack8_Stage2`. The pack
> choice is therefore a *look* question, settled by preview, not by this table.

**Measured 2026-08-11, both packs, every non-`React`/`Complete`/`Back` attack clip.** Short
chainable strikes only; full data in git history of this entry.

| V1 | s | V3 | s |
|---|---|---|---|
| `Attack10_Stage1` | 0.633 | `Attack6_Stage2` | 0.433 |
| `Attack4_Stage1` | **0.700** | `Attack7_Stage2` | 0.467 |
| `Attack7_Stage2` | **0.700** | `Attack4_Stage1` | 0.600 |
| `Attack3_Stage1` | 0.733 | `Attack4_Stage3` | 0.667 |
| `Attack6_Stage1` | 1.067 | `Attack1_Stage2`, `Attack8_Stage2` | **0.700** |

**V1's two-stage families open short** (0.633–0.733) where V3's mostly do not, so for item 6's
*single* light — all the current scope needs — V1 gives a usable strike with no long opener in
front of it. **V3's combo families are deeper** (`Attack4` has four stages; `Attack1`/`6`/`7`/`8`
have three, against V1's mostly two), which is what item 9 will want. That tension is real and is
not resolved here.

## 2026-08-11 — The training dummy gets the sword too, and the blade is authored rather than measured

Raised by the user while setting item 6's scope: should the dummy get the new attacks, or keep
punching with stock assets? Settled as **parity in what the player is tested against** — the
user's phrase was "the correct amount of parity", and the qualifier is the load-bearing part.

**Keeping the dummy unarmed as a stable control was the real alternative, and it is better than it
sounds.** A test fixture that changes whenever the player changes stops being a control: you
cannot ask "does blocking feel different than it did before item 6" if both sides moved. That is
a genuine methodological argument and it is being rejected rather than refuted.

What defeats it: **item 6 changes reach**, and reach is what spacing is measured against. Every
defensive verdict from here — block coverage, blockstun duration, the parry window, whiff-punish
distance — is measured against whatever the dummy throws. A dummy that keeps fist-reach is a
*stable* reference to the wrong number, and item 7's blockstun is explicitly "a duration based on
the attack blocked". The failure is silent: the tuning all looks principled, and is calibrated
against an opponent nobody will ever fight. A wrong control answers a question the prototype is
not asking, and PvP is the destination.

**Parity is deliberately partial.** The dummy gets *offense* parity because offense is what the
player is measured against. It does not get `GA_Dodge` and should not — nothing about a defensive
verdict depends on the dummy being able to evade, and a dodging dummy would make every spacing
measurement noisier for no gain. The rule is not "the dummy is a second player"; it is "the dummy
is accurate in the dimension being measured."

Worth noting the decision was nearly made already and neither of us had noticed: **both characters
are granted the same `GA_Attack`**, so montages, branch timings, `TraceSocket` and `TraceRadius`
are one shared set. Divergence was the option that cost work — a second ability asset with a
second set of timings to keep in sync. Checking that first turned a design debate into a much
smaller one, which is the general lesson: *establish whether the alternatives actually differ in
cost before arguing their merits.*

**The blade's length is authored, not derived from the weapon mesh.** This is the design decision
the dummy question forced out early, and it is the more valuable half of the entry. A
blade-base-to-tip sweep needs a length, and taking it from the attached `WeaponMesh` bounds is the
obvious implementation. It fails on exactly one case and fails silently: the `Sword` socket lives
on the *skeleton*, so it exists whether or not a prop hangs off it, and a character with no weapon
mesh would produce a well-formed trace of **zero length** — a hitbox that misses everything, with
nothing logged and nothing null. Authoring the length as a Blueprint-exposed number instead means
the trace is correct for anyone holding anything, including nobody.

Had the dummy stayed unarmed, a mesh-derived blade would have worked perfectly in every test —
because the player, the only armed character, would have been the only one traced.

The consequence for item 6 is small and should not be mistaken for the decision: the dummy needs
its `WeaponMesh` and `ShieldMesh` set, which is two properties on `BP_TrainingDummy`, not a class
change. If player and dummy become hard to tell apart, a material tint is the answer — it does
not touch mechanics.

## 2026-08-11 — The ASC moves to the PlayerState, and the character resolves which one it uses

**Decided, not yet built.** Recorded now because the alternatives were argued and discarded, and
the code will only contain the winner.

**The ASC moves to a new `ATDPlayerState`** for player characters — the conventional PvP shape,
and the one that survives if respawn ever means a fresh pawn rather than the revive it is today.
Keeping it on the character was defensible on current behaviour and was rejected on the grounds
that the destination is known.

**The training dummy has no PlayerState**, being an unpossessed placed pawn, so the character
resolves its ASC: the PlayerState's when there is one, an owned component when there is not.
One class, one `InitialiseAbilitySystem`, no Blueprint changes.

Two alternatives were rejected:

**Splitting into `ATDPlayerCharacter` / `ATDAICharacter`** was chosen first and then withdrawn.
It is the cleanest conceptually — each class honest about what it owns — but it means
**reparenting `BP_PlayerCharacter` and `BP_TrainingDummy`**, and a Blueprint stores its parent
class path. That is the operation this project's first implementation convention exists to warn
about, and it would be performed on the two Blueprints everything else depends on, to buy a
distinction a header comment already makes. What decided it: *the training dummy will only ever
be a training dummy.* There is no future AI opponent for the split to serve, so it would be
structure built for a case that is not coming.

**Giving the dummy an AI controller with `bWantsPlayerState`** would make the rule uniform with
no fallback branch, and was rejected for the same reason — a controller the dummy does not
otherwise need, to remove a branch that is two lines.

The accepted cost is a small one, stated plainly: a player character carries an owned ASC
subobject it never uses. It seeds no attributes and grants no abilities, so it costs registration
rather than bandwidth.

## 2026-08-11 — PvP is the destination, so state must replicate; the model is server-auth + prediction

Raised by the user, who noted this prototype is ultimately PvP and that netcode should be treated
as an inevitability rather than a later phase. An audit went looking for how large the gap
already was.

> **Corrected within the hour, by the user.** This entry originally said `CLAUDE.md` still listed
> netcode as out of scope "and that stands". It does not stand — the user withdrew the scope call
> as soon as it was restated back to them: *"A PvP prototype doesn't do all that much, ultimately,
> if it's incapable of PvP."* `CLAUDE.md` now carries **Building for the network** instead, and
> only *live multiplayer sessions* remain out of scope. Corrected inline rather than superseded
> because the reasoning below never depended on the deferral — it argued the opposite — and a
> stale claim about what another document says is exactly the kind that gets read as current.
>
> Worth keeping as an instance of a pattern this log already knows: **a claim moved between
> documents arrives with the authority of its new home and none of the scrutiny it never had.**
> The same thing happened on 2026-08-10 with the stock-ABP claim. Restating an inherited
> assumption out loud is the cheapest moment to test it, and both times that is what killed it.

**The verdict: caught early, and the expensive-to-retrofit decisions had already gone the right
way.** Attributes replicate correctly (`Mixed` mode, `REPNOTIFY_Always`, three-layer clamping),
damage and cost application are authority-gated, and **displacement is root motion rather than
code-driven** — that last one was chosen on 2026-08-10 for a completely unrelated reason ("the
cheaper experiment runs first") and is the single most valuable accident in the codebase, because
CMC replicates root motion and a hand-rolled displacement system would have had to be rewritten.

What was wrong was wrong *mechanically* rather than structurally: ten call sites of one mistake,
one unset property, one missing guard.

**The latency model is server-authoritative with client prediction.** Chosen deliberately over
rollback. Rollback is arguably the better fit for the design — a 250 ms light and frame-precise
punishes are fighting-game shapes — but GAS is not built for it, and choosing it would mean
fighting the framework or moving combat out of it entirely. This is the model GAS is designed
around, and the project has shipped PvP on it before.

The consequence to hold onto: **latency is now a design constraint, not an implementation
detail.** Reactability is measured from the tell, and the heavy's coil→damaging window is already
"right at the edge of human reaction" at ~240 ms. Whatever a network adds comes out of that
budget. Items 6–12 should be designed knowing it.

**Loose gameplay tags do not replicate, and every state tag in the project was one.** For tags
applied by an ability this survived by accident — the ability runs on both machines under
`LocalPredicted`, so both ASCs get the tag independently. For tags applied by the *character* it
was simply broken: `State.Dead` and `State.Exhausted` are driven by attribute delegates bound
behind the authority gate, so a client's ASC never received them, its own `CanActivateAbility`
would pass, and it would predict actions the server had already refused.

Fixed by making `bDead` / `bExhausted` replicated properties whose `OnRep` applies the same tag
locally that the server applied on itself. **A GameplayEffect carrying the tag is the more usual
answer and was rejected on tooling grounds**: UE 5.8 expresses effect-granted tags through
`gEComponents`, which cannot be scripted, so it would need a human in the details panel for a
mechanism the economy already owns in C++.

The shape this establishes, and the reason it is worth an entry: **decide on the server, apply
everywhere.** `EnterDeath`/`ExitExhaustion` decide whether the state changed and are
authority-only; `ApplyDeathState`/`ClearExhaustionState` make it true on the local machine and run
on the server directly and on clients from `OnRep`. Two things stayed deliberately on the server
side of that line — `CancelAllAbilities`, because it replicates through GAS by itself and running
it again from a client would cancel that client's predicted copies, and attribute restoration,
because a client rewriting its own health is the shape of an exploit.

**`NetExecutionPolicy` was `LocalPredicted` on every ability by accident.** `UGameplayAbility`'s
constructor never assigns it and `LocalPredicted` is enum index 0, so the most demanding policy
was in force without anyone choosing it. It is now set explicitly — to the same value, which is
correct for the agreed model. What is still owed is prediction windows, so a mispredicted
activation is rolled back rather than left standing. Declaring the intent while the
implementation is incomplete is deliberate and better than leaving it undeclared.

**The melee trace is now authority-guarded.** It ran on client and server both, from socket
positions a round trip apart. Damage was already server-gated so this was wasted work rather than
double damage, but it was also a second opinion about what was hit that nothing reconciled.
Prediction does not change this: a client predicts its own action, never whether that action
connected with someone else.

**Standing rule from here: new state is a replicated property or an attribute, never a loose tag;
new authority-sensitive work is explicitly gated.** That is what stops the gap growing while the
actual networking waits.

Known to remain, and deliberately deferred: prediction windows and keys, lag compensation for
i-frames and hit resolution (the dodge is 400 ms of invulnerability whose start instant two
machines will disagree about), client-side stamina prediction, and 14 `SetTimer` sites that are
not network-aware.

> **The figure of 14 is wrong, recounted 2026-08-11.** It is **2** — `TDChargedAttackAbility`'s
> checkpoint and `TDDodgeAbility`'s duration. The original came from a module-wide grep that
> counted six sites in `Variant_Combat/` (Epic template code we do not derive from), four
> debug-only timers, and the buffered-release replay, which is local input and correctly stays
> local. Corrected **inline** rather than superseded because nothing was decided on it — it was a
> miscount, not a position that changed, the same treatment the stock-ABP claim got on
> 2026-08-10. See the known-traps section for the full breakdown. **The correction makes the list
> shorter, not the work smaller:** the dodge timer is the i-frame lag-compensation problem named
> two sentences earlier, and that is the hardest item here.

## 2026-08-11 — Dodge travel ships at the clips' authored distance, measured rather than assumed

Item 5. `DodgeRootMotionScale` is now wired through to the montage task, and stays at **1.0** —
the value play already had. The slice's product is the knob and the measurement, not a change.

**The eight directions were suspected of disagreeing and do not.** `AM_Dodge` is built from eight
separate `Dash` clips, and nothing had ever checked they travel the same distance; if they had
disagreed, a single uniform scale would multiply the gap rather than close it, and the fix would
have been per-direction data. Measured over one dodge each on flat ground:

| Direction | BR | FR | FL | R | Fw | BL | Bw | L |
|---|---|---|---|---|---|---|---|---|
| Travelled (uu) | 418.5 | 417.3 | 408.8 | 403.6 | 403.4 | 402.2 | 398.6 | 387.0 |

Mean **404.9 uu**, total spread 31.5 uu — under one capsule radius, and below what a player can
perceive as directional bias. A single scale is the right shape. Durations were 0.400–0.408 s
against `DodgeSeconds` 0.400, matching the project's usual within-a-frame-biased-late pattern.

**Measured at the actor, deliberately, not read off the clip.** What the player feels is where
the character ended up, which collision, slopes and the movement component all get a say in. The
authored displacement would describe the animator's intent instead. The trace reports the actual
delta, horizontal only — vertical travel is gravity, and counting it would make every dodge down
a slope read as longer.

**The number worth carrying forward: a dodge covers roughly three times an attack's static
reach**, and averages about twice `MaxWalkSpeed`. So it does not merely evade, it disengages, and
punishing afterwards costs the time to close again. That is not a complaint — play reports the
dodge feels good — but it means the dodge has *two* safety margins, exposure and distance, where
the tuning map's "dodge is too safe" row only names the first. Whichever gets moved, the other is
still there.

Reach is estimated rather than measured: the trace follows `hand_r`, and where that socket sits at
the impact frame is unknown. If spacing becomes contentious, that is the missing number.

## 2026-08-11 — Death cancels what exhaustion lets finish, and inert is not the same as dead

Item 4, the minimum death: health reaches zero, `State.Dead` goes on, every ability is refused,
the character stops. Respawn rules, whether death routes through knockdown, and whether the dummy
should die at all remain item 11's.

**Death cancels running abilities; exhaustion deliberately does not.** This is the one place the
pattern being copied was wrong to copy. Exhaustion gates *activation* and lets a running ability
play out, which is right for a stamina lockout — and is the reason a block held through zero is a
filed trap. For death it is visibly wrong: a killing blow landing mid-swing would leave the corpse
completing its attack, hitbox included. `CancelAllAbilities` also clears `State.Attacking` and
`State.Attacking.Committed` through the normal end path, so death cannot leak the tags that would
forbid every future defensive action after a revive.

**The refusal lives on the shared ability base, not in each ability's `ActivationBlockedTags`.**
An ability can be authored without a tag; it cannot be authored without its base class. The dead
are not a special case any one ability should have to remember.

**`State.Dead` is native, not an `EditDefaultsOnly` property like `ExhaustedTag`.** A placed actor
can serialize stale `EditDefaultsOnly` values that silently override its Blueprint, and the
training dummy shipped for days with `ExhaustedTag` unset for exactly that reason — unreachable
from the details panel, so nothing showed it. A native tag has no per-instance value to go stale.

**A ragdoll, because "inert" and "dead" look identical.** Play's first report was that death was
unreadable: the character stopped and nothing announced it. The ragdoll needs no animation
content, which matters because the directional `Death_<DIR>` clips belong to item 11 — and it is
behind `bRagdollOnDeath` so that slice can switch it off rather than unpick it. The mesh's rest
offset is captured in `BeginPlay`, before physics can touch it: restoring from a value read after
death would bake the ragdoll's final pose in as the new rest offset and the character would stand
up crooked permanently.

**Disabling movement does not disable facing, and that cost a bug.** The dead character still
turned to track the camera, because `UpdateCameraRelativeFacing` is a separate system that knew
nothing about death. Worth stating generally: *stopping a character* is not one switch, and the
list of what "stopped" means is not discoverable from the thing you just turned off.

The user proposed **depossessing the pawn** instead, which is the stronger version of this
argument and was rejected only on scope: it would kill the whole class of problem at once, but
pulls in where the controller goes meanwhile and what the camera does, both of which are item 11's
questions. Revisit if death ever needs its own camera. Recorded because the reasoning is better
than the alternative it lost to, and someone should propose it again.

**Two defects found by review rather than by play**, both silent:

- *Dying airborne stranded `bJumpRegenPauseActive` set forever.* `DisableMovement` stops the fall,
  so `Landed()` never fires to clear it, and stamina regen stays suppressed for the rest of that
  character's life — past the revive, with nothing to indicate it.
- *`ReturnToDebugAutoAttackHome` guarded on the wrong flag.* It checked
  `bDebugAutoAttackResetPosition` (default true) but not `bDebugAutoAttack`, while the home
  transform is only ever captured for auto-attackers. Unreachable until the revive started calling
  it — at which point the *player* would have teleported to the world origin on every death.

**And one found by play that was a wrong comment rather than wrong code.** The reset is correctly
suppressed while dead, so a corpse is not teleported mid-ragdoll; the comment then claimed "the
revive restores the transform anyway", which was false — the revive restores the *mesh's* offset
from the capsule, never the *actor's* world transform. So a dummy killed mid-lunge revived
displaced and only drifted home on its next attack cycle. The behaviour was changed to match the
comment rather than the comment softened to match the behaviour, because the comment described
what the system should have done.

## 2026-08-11 — The buffer window doubles to 200 ms, and what the tuning sessions ruled out

`InputBufferSeconds` 100 → 200 ms, settled by play the same day it was built. Measured, across
three sessions on the same build:

| Window | Presses buffered | Expired unfired | Fired |
|---|---|---|---|
| 100 ms | 40 | **17 (43%)** | 23 |
| 200 ms | 35 | 2 (5.7%) | 33 |
| 200 ms, ladder test | 70 | **2 (2.9%)** | 68 (97%) |

**Going higher was rejected, and exhaustion is the reason it stays rejected.** The user's read
was that a wider window buys only false positives; the log gives that a mechanism. Exhaustion
lasts until stamina refills — about four seconds — and dodge presses made while exhausted
correctly expire rather than firing. A window wide enough to bridge that would hand back a dodge
seconds after it was asked for. So the ceiling is not "how long until an input feels stale" in
the abstract; it is set by the longest lockout the design deliberately refuses to shorten.

**A verdict that reads as a dodge failure and is not.** Zero of seven buffered dodges fired in
one session, which looked alarming and was not: all seven were pressed either during a charged's
commitment or while exhausted, and 14 further dodge presses that session activated immediately.
Both blocking states are visible to the player — a swing in progress, an empty greyed bar — so
none of these was an input silently doing nothing. **The lesson is the denominator**: judging a
buffer by the subset that needed buffering measures the hardest cases only, and reports a healthy
system as broken.

**Holding an input to have it fire on the first legal frame is accepted as a technique.** It
falls out of the no-expiry-while-held rule rather than being designed, and it was left in
deliberately after being checked for the failure that would have killed it: it does not loop.
Buffering happens only on a press edge (`ETriggerEvent::Started`), and the slot is cleared when
it fires, so a held button yields exactly one action. Confirmed in play — the tightest interval
between consecutive dodges was 834 ms against a 0.4 s dodge; a loop would show a run of ~400 ms
repeats until stamina ran out.

**Accepted, with eyes open: a buffered attack's tier depends on how long the buffer waited, and
the player cannot see that.** The hold is measured from activation, so the wait is subtracted
from the front. Measured in one session: a **452 ms** physical hold produced a **Heavy** while a
**484 ms** hold produced a **Light**, because the buffer had sat for 0 ms and 282 ms
respectively. This is not a defect — it is "windup length is preset" doing its job, and
crediting the pre-activation hold is the fix that was rejected when the buffer was built. It is
recorded because someone will propose that fix again on seeing exactly this. What makes it
liveable is that the attack is *visually* cued: hold until the coil appears and the latency
stops mattering. The user's verdict after extended play was that the misses "felt like a skill I
could grind improving, rather than the game's inputs betraying me", which is the distinction
that matters and the one only play can supply.

**A note on what "verified" required, because the bar was briefly set wrong.** The
tier-preserving replay was called untested for two sessions on the grounds that no logged case
showed a hold past 200 ms producing a heavy. That standard was wrong: there is no branch on
200 ms. The path schedules the replay at `HoldSeconds` whatever its value, it executed 24 times
correctly, and what follows the replayed release is the ordinary escalation logic that 39 heavy
and 38 charged commits already exercise. **Treating a parameter value as a code branch produced
a test that could only be passed by hand-timing a float**, and cost two play sessions chasing it.
Ask what the code actually branches on before asking play to cover a case.

## 2026-08-11 — The input buffer remembers a press, and one rejection that play reversed

A press nothing could answer is kept for `InputBufferSeconds` (100 ms) and retried each frame.
Item 8, verified in play. The code says what it does; below is what was rejected, and the one
rejection that turned out to be wrong.

**Crediting the hold already elapsed was rejected outright.** It is the tempting way to make a
buffered charge arrive "on time", and it breaks the rule that windup length is preset — the
attack would land sooner than its tier is authored to take. The hold is measured from
activation, and time spent pressing before that is spent.

**Replaying the release as a *duration* was built, dropped, then reinstated by play. That
sequence is the entry.** The simplification was to record the release as a yes/no and replay it
at once, justified like this: the buffer outlives a release by only `InputBufferSeconds`, so
every recordable hold must be under 100 ms — comfortably inside the light's 200 ms boundary —
and therefore replaying the true offset and releasing immediately select the same branch, so the
offset machinery buys nothing.

The premise is false. **The buffer survives indefinitely while the button is held**, so a
recorded hold is bounded by how long the *block* lasted, not by the window. Release while still
locked out and any hold length can be recorded. Play produced a **236 ms hold — a heavy by every
rule the ladder has — flattened to a light**, visible only because the trace prints the duration
rather than a boolean.

Two things worth carrying out of that:

- The instruction it overrode — record both edges, *because you need to be able to buffer any
  attack type* — was the user's, and the argument that talked us out of it was mine. **A
  simplification justified by an invariant is worth exactly as much as the invariant**, and this
  one was never checked against a rule written into the same struct three edits earlier.
- **Where a value drives behaviour, print the value.** A boolean trace would have shown this
  system working perfectly. That is the second time this project has learned it; the first was
  `want=0.500s` on eleven consecutive dodges.

**One slot, not a queue.** A queue replays stale intent as a burst — press dodge then attack into
a lockout and both arrive, in an order you had already stopped asking for.

**Polled, not event-driven.** Waking on "the reason this was refused" means enumerating every
blocking tag, every ability end and the airborne check; missing one fails silently. Polling
cannot be incomplete, and there is at most one entry to retry.

**The window is grace on taps, not on intent.** A held button never expires, so the 100 ms
measures from the release. Without that a heavy could not be buffered at all — its 200 ms
boundary is beyond any window this size, so every tier above light necessarily comes from a hold
that outlives the window.

**The airborne dodge refusal is deliberately not buffered**, superseding the prediction in
"2026-08-10 — No dodging in the air" that buffering would turn it into a defer. Landing is not a
moment you are free to act through: a landing recovery would lock the dodge out anyway, so
deferring to the touchdown frame hands back a dodge a finished character will later have to take
away, and tunes the timing of a window that does not exist yet. `ShouldBufferFailedInput` is
where an ability declares that its refusal was not the temporary kind.

**A held buffer does not expire, but it does not wait either** — and confusing the two cost two
play sessions. I proposed testing the supersede path by holding dodge through a committed attack
and then pressing attack, expecting the held buffer still to be there. It was not: holding
prevents *expiry*, not *firing*, and the buffer fires at the first opportunity. Measured, the
dodge escaped in 202, 227 and 335 ms across three attempts. The general form is more useful than
the bug — **a superseding press only matters when something blocks an ability for longer than a
person can react**, and today exhaustion is the only such thing in the game. Items 7 and 11
(block, blockstun, knockdown) make long lockouts routine, at which point this stops being exotic.

**Feel is deliberately untuned.** 43% of buffered presses expired unfired in the first session.
Whether the window should be wider is open and is the user's call; see the trap above about
sizing it against a recovery length item 12 has not settled.

## 2026-08-11 — The character wears the bundle's mesh, and `ABP_Combat` is Epic's ABP with two nodes swapped

Three choices from items 3b and 3c that a future reader would reasonably question.

**The character mesh is `GDHBundle`'s `SKM_Manny`, not Epic's `SKM_Manny_Simple`.** Not a
preference — the pack's `Sword` and `Shield` sockets exist only on its own mesh, and those
sockets carry the grip rotation and the non-uniform scale that corrects an oversized shield.
`_Simple` also lacks the whole twist/corrective/weapon bone family. The cost is that the mesh
now sits on the bundle's skeleton while `AM_LightAttack_01` is still bound to Epic's, which
works **only** because GDH's `SK_Mannequin` was given a reverse `CompatibleSkeletons` entry
pointing back at Epic's. That entry is load-bearing: remove it and the light attack silently
stops playing. The original migration added the forward entry; this session added the return.

**`ABP_Combat` is a duplicate of Epic's `ABP_Manny_Combat` with two nodes repointed**, rather
than an AnimBlueprint authored from scratch. Duplicating buys a working state machine, jump and
fall states, and a foot-IK control rig for free, and putting the copy under `/Game/TheDream/`
satisfies the ownership rule. Authoring one fresh would have cost a day to arrive somewhere
worse. The accepted consequence is that we now maintain Epic's structure without having chosen
it, and that its jump, fall and land states still play Epic's generic `MM_` clips — visible in
play, deliberately out of item 3c's scope.

**The blendspace has a walk row even though WASD cannot ask for a walk.** Raised by the user,
who expected it to be dead content. It is not: the Speed axis is fed by actual movement speed,
not by an input mode, and the character ramps through walk speeds on every start and stop. With
no walk row that ramp blends idle straight into a full run and reads floaty. The row that would
genuinely be dead content is a *sprint* row, since nothing can request one.

**Known and not fixed: adjacent directions disagree about the guard pose.** `BR` holds the
shield front-left while backpedalling, `R` holds it front-centre, so it snaps ~135° blending
between them. That is in the source clips, not in our setup. The standard fix is an upper-body
layered blend over one consistent guard pose, above `spine_01` or `spine_03` — which is very
likely what block needs anyway, so building it now would mean building it twice or building it
before knowing what item 7 wants from it. Deferred deliberately, not overlooked.

## 2026-08-10 — Facing is camera-relative always, and snaps on *input* rather than on movement

Four decisions taken together, before any of item 3 was built. Moved here from `CLAUDE.md`,
which was carrying the argument as well as the ruling.

**Why facing, locomotion and the props are one item.** `ResolveDodgeDirection` measures input
against actor facing, and `bOrientRotationToMovement` meant the actor already faced its input —
so the angle was always ~0 and it resolved to `Fw` in steady state, `Bw` only via the stationary
fallback. Across every dodge logged 2026-08-10, no other direction ever fired. **The function
needs no code change; it was correct and merely degenerate.** Camera-relative facing lights up
all eight as they are.

**Locomotion still ships with facing, but not for the reason `CLAUDE.md` gave.**

> **Corrected the same day it was written.** This entry originally claimed locomotion could not
> be deferred because "the stock ABP plays a forward run while moving sideways." **That is
> false** — 3a went into play and `ABP_Manny_Combat` produced correct strafe and backpedal
> animations immediately. The claim came from `CLAUDE.md`, was moved here unexamined, and was
> falsified within hours by the very slice it was justifying. Neither of us had checked it. It
> is corrected inline rather than superseded because nothing was *decided* on it — it was a
> factual error about existing content, not a position that changed.

The real reasons are ownership and stance, and both are weaker urgency than the false one:
`ABP_Manny_Combat` lives at `/Game/` root, is Epic template content authored for the
`Variant_Combat` hierarchy we deliberately do not derive from, and animates a generic Manny
rather than a sword-and-shield stance. So it gets replaced because it is not ours and not the
right character, not because it is broken.

**The transferable lesson is about inherited claims.** A statement moved between documents
arrives with the authority of the file it lands in and none of the scrutiny it never had.
Deduplicating is still right — the error was in exactly one place when it came time to fix it,
which is the whole argument for pointing rather than copying — but *moving* a claim is the
cheapest moment to test it, and this one would have cost one PIE session to check.

**Facing is camera-relative permanently, not by combat stance.** A stance toggle is the common
action-game answer and was rejected for now on state cost: dodge, block and parry would each
have to agree with it, and there is no traversal content that would benefit. Revisit if one
ever exists.

**But the character does not snap to the camera at all times.** While there is no movement
input it turns smoothly toward camera yaw; the moment there is any movement input it snaps.
The reason snap is wrong while stationary is that a static idle has nothing to mask an
instant rotation pop — and the standard fix is unavailable to us:

> **The bundle contains no turn-in-place animation whatsoever.** Zero matches for `Turn`,
> `Pivot`, `Rotate` or `Spin` across all 6,576 rows of the unfiltered index, and no `Turn`
> token in the exhaustive vocabulary. `SwordShield` ships two idles per pack and nothing else.

So the hybrid is not a preference over the animated solution; it is the only solution we can
build without new content.

**The snap keys on input, never on velocity, and that is the load-bearing part.** Keyed on
*movement* the hybrid would break the dodge at exactly the moment dodges get pressed. Standing
still, camera swung north, character mid-turn still facing east, press forward+dodge on one
frame: input is north, facing is east, the signed angle is −90° and the dodge resolves to `L`.
You pressed forward and went left.

Keying on input closes it, because `ResolveDodgeDirection` already returns `Bw` unconditionally
on near-zero input and never consults facing while stationary. The two rules then key off the
**same** condition and cannot disagree: facing is allowed to lag only while nothing reads it,
and is exact the instant anything does.

This is the third application of a rule the codebase keeps arriving at independently — **key
off input or action, not off the state it produces.** `ResolveDodgeDirection` reads
`GetLastInputVector()` rather than velocity because velocity still carries the old direction
after the stick is released; the jump regen pause hooks `OnJumped` rather than `IsFalling()`
so a ledge-walk is not billed. Worth stating generally now that it has come up three times.

**Accepted cost, stated plainly: the pop is relocated, not removed.** Swing the camera 180°
while stationary and move before the turn completes, and the remainder snaps. It lands on the
idle→locomotion transition, which is changing the animation anyway, and at 500°/s a 180° turn
takes 0.36 s so it will usually have finished. The turn rate gets its own tunable rather than
borrowing `RotationRate`, because the two are now different things.

**Locomotion is run-only, eight directions, `Loopable` clips, from `SwordAndShieldAnimV1`.**
One 1D directional blendspace, eight samples, idle as a separate state. Walk is not authored
because there is no walk/run input to select it — a speed axis would be content for a control
that does not exist. Start/End transition clips are likewise deferred: they are polish on a
baseline nobody has felt, and this project has now twice been right to judge the baseline
first. V1 is chosen to match `AM_Dodge`, which is built from V1 `Dash` clips; mixing packs
risks two stances reading as two characters. If V3 previews better, `AM_Dodge` should move too
rather than the two diverging.

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

> **Superseded the same day, by "Animations play whole" below.** Trimming is now forbidden: an
> animation plays in full across the mechanical duration it belongs to, and the lever for a
> fast-forwarded dodge is `DodgeSeconds`. The trailing pointer to "the open question above" is also
> dangling — that section no longer exists. Both found by the 2026-08-12 audit, a day after the
> superseding entry was written directly beneath this one without a table row being added.

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

> **False, and corrected in `Docs/Animation-Library.md` rather than here.** The bundle ships
> `SwordShield/DEMO/StaticMesh/Shield_Heater`, and it was missed because it carries **no `SM_`
> prefix** while the search filtered on one — one of the three absence claims that produced this
> project's standing unfiltered-search rule. The shield in play is the bundle's, not a primitive.

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

**Compacted 2026-08-11 — this entry recorded nothing the other docs did not.** Audited against
the new bar and found to be the only one of 27 that was fully duplicated: the vocabulary and the
coil-inside-windup ruling are in `CLAUDE.md`'s **Combat Vocabulary**, and the reason
`UAnimNotifyState_MeleeWindow` cannot be renamed — placed notifies serialize against the class
path — is in `Docs/Working-In-Unreal.md` under **Not scriptable at all**. Both are better homes:
one is loaded every session, the other is read before touching notifies.

Kept as a stub rather than deleted so the date survives and a grep for the vocabulary lands
somewhere that points onward. Full text in git before `2026-08-11`.

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
