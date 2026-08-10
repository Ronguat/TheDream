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

- **Does the 250 / 500 / 1000 ms windup ladder feel right?** Untested — this is the
  whole reason the prototype exists. The alternative on the table was a faster
  250 / 500 ladder (roughly Divine Knockout vs. New World feel). All thresholds are
  per-branch `EditDefaultsOnly` floats, so trying another ladder is a Blueprint edit.
- **What should `CoilPlayRate` actually be?** A slow creep was chosen over a hard freeze
  (see below), but the value is a guess. The thing to judge is "loaded" vs. "stalled".
- **Should all three attacks share identical impact frames?** They no longer do. The
  coil advances the animation while held, so the gap between commit and first active
  frame shrinks the longer you charge. This may read correctly — a wound-up attack
  snapping out faster is intuitive — but it reverses an earlier deliberate goal.
- **"Release" is overloaded.** It names the damaging phase *and* the button coming up
  (GAS gives us `InputReleased`, which cannot be renamed). Mitigated by convention
  rather than solved; revisit if it causes a real misread. "Active" is the standard
  alternative.

---

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
