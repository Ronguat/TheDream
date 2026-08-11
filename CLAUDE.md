# The Dream – Combat Prototype

## Project Intent
Build a high-precision combat prototype in Unreal Engine that prioritizes spacing, reactability windows, stamina as a real resource, and clear punish opportunities. Feel goals (in priority order):
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
- Netcode / multiplayer

## Core Combat Rules (must respect)

### Combat Vocabulary
Attack phases, used consistently in code, comments and discussion:
- **Windup** — everything before the attack can deal damage.
- **Release** — the period during which the attack deals damage. Marked on a montage by the `Release Window` notify state (`UAnimNotifyState_MeleeWindow`).
- **Recovery** — from the end of the damaging phase to the end of the attack.
- **Coil** — *not* a fourth phase. It is a sub-state of windup: the portion slowed while waiting for the commit checkpoint, and it exists as visual feedback. Its tuning values are named `Coil*` rather than after a phase.

Note that "release" also names the button coming up, via GAS's `InputReleased`. Bare "release" always means the damaging phase; the button edge is always written as *input release*.

### Offense (Melee)
**An attack is defined by when it hits, not by how it plays.** Each tier authors the moment its hitbox goes live and the input boundary you must release before to get it. Every play rate is derived from those at runtime. Two numbers per tier:

| | Release before | Hitbox live |
|---|---|---|
| Light | 200 ms | **250 ms** |
| Heavy | 450 ms | **500 ms** |
| Charged Heavy | (held past 450 ms) | **750 ms** |

Two rules the model depends on:
- **Windup length is preset.** Releasing early inside a band changes nothing — the attack still takes its full time to arrive. The cost is real dead time, and it is what stops a fractionally-held heavy from dominating light.
- **Reactability is measured from the tell, not from the press.** All tiers share one windup, so the defender's window is coil → damaging. Lengthening a windup does not by itself make an attack more reactable; moving the coil earlier does.

- **Light**: released before 200 ms, hits at 250 ms. 2–4 hit string (weapon dependent) — *not yet built; currently a single hit*. First hit safe on block; subsequent hits are not. Any hit in the string guarantees the rest. Last hit knocks down but has heavy endlag. Minimal stamina damage. Unreactable — it never coils, so there is no tell at all.
- **Heavy**: held past 200 ms, hits at 500 ms. Single hit. Safe on block, punishable on whiff. Knocks down. Higher range, moderate stamina damage. *Currently ~300 ms coil → damaging, more reactable than intended; deferred until the ladder is tuned as a whole.*
- **Charged Heavy**: held past 450 ms, hits at 750 ms. Single hit. Breaks block, heavy endlag, knocks down. Highest range. Very reactable.
- Any light in a chain can be held to convert into a heavy.
- Some heavies can chain into further heavies; never into lights.

Timings land within about a frame, biased late. `GA_Attack`'s `Branches` array is authoritative for live values; the reasoning behind the model is in `Docs/Combat-Decisions.md`.

### Defense
- **Any defensive action can cancel an attack's startup** — block, dodge or parry, not block alone. The boundary is the attack's commit checkpoint, marked by `State.Attacking.Committed`: cancel before it, never after. Defensive abilities block on that tag rather than on `State.Attacking`.
- **Block** (hold RMB): 180° forward. Drains stamina (heavies drain more; charged heavies exhaust if blocked).
- **Dodge** (LShift): Directional (or back if stationary). Costs 50 stamina. Grants i-frames for the duration. **Not available while airborne** — keyed to the falling state, so it covers walking off a ledge too, unlike the jump's regen pause which keys on the action.
- **Parry** (MB4 or LAlt+RMB): 400 ms active window, 360° coverage. Success = no stamina drain, no blockstun, and (vs melee) 500 ms offensive lock on attacker. Success vs ranged redirects to crosshair. Whiff = 1000 ms defensive lockout. Successful parries can retrigger without impeding other actions.

### Stun & Knockdown
- Blockstun: Disables offense + parry for a short duration based on the blocked attack.
- Hitstun: Brief, enables combos.
- Knockdown: 1.5 s default get-up. Early get-up via Dodge, Block, or Attack. Get-up attack knocks back, short recovery, very punishable on block/whiff.

### Stamina
- Max 100.
- Dodge = 50.
- Blocking drains based on attack + blocking weapon.
- 0 stamina → Exhausted (no defensive actions or jump) **until stamina refills to 100**, not for a fixed duration. Stamina floors at 0, so there is no overspending and every exhaustion is identical — dodging at 3 and dodging at 50 both land on exactly 0. Re-emptying the bar the moment you recover is allowed. **Regen continues while exhausted** — it locks out acting, not recovering, and is the only thing that can end it, so nothing may suppress regen here. Reasoning in `Docs/Combat-Decisions.md`.
- Regen 25/s. Paused during defensive actions and for 0.5 s after, measured from when the action ends (`StaminaRegenPauseSeconds`).
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

## Project Documentation
Three files carry knowledge the code cannot. Read them before working in their area; keep them true in the same commit that makes them wrong.
- **`Docs/Working-In-Unreal.md`** — how to drive the editor and its MCP toolset without losing work: which writes silently do nothing, when Live Coding is safe versus needing a full editor-closed rebuild, what is not scriptable at all, and the standing regression checks for combat changes. Read before writing assets or C++.
- **`Docs/Combat-Decisions.md`** — dated log of combat decisions and the reasoning behind them, plus two working sections at the top: **known traps**, latent defects filed against the slice that trips them, and the **tuning map**, which knob to move when a verdict comes back and which obvious-looking knob is wrong. Append an entry whenever a gameplay choice is made that a future reader could reasonably second-guess; never rewrite an entry to match new code, supersede it with a new one.
- **`Docs/Animation-Library.md`** — where animations come from, the naming convention that makes 5,319 of them searchable, what the library does *not* contain, and how to migrate one in without dragging a duplicate skeleton behind it. Read before asking for or importing any animation.

**Durable knowledge belongs in these files, not in an assistant's per-machine memory.** Anything a future contributor would need — combat reasoning, tooling behaviour, rules and current facts — goes in the repo, where it can be reviewed and corrected. Memory keeps only what is genuinely session- or machine-scoped, and *points* at the repo rather than restating it: `Docs/Working-In-Unreal.md` exists precisely because those notes were once memory-only and therefore invisible. Duplicating instead of pointing is its own failure — a second copy is what let a wrong claim survive unchallenged.

Deliberately **not** kept: per-system design docs. Local rationale belongs in header comments, which are read at the moment the code is; a doc that describes a system drifts out of sync and then gets trusted over the code.

## Working Rules
- **Combat and gameplay work is deliberate, not vibed.** Minimize assumptions and state the reasoning, even when it is slower. If a gameplay question has more than one defensible answer, raise it rather than picking one quietly — and record the choice in `Docs/Combat-Decisions.md`. Unprompted initiative is welcome for debug and tooling conveniences (adding a readout to the debug HUD, say); it is not welcome for anything that changes how the game plays.
- **When play and rationale disagree, play wins.** This file and `Docs/Combat-Decisions.md` are full of carefully argued positions. They exist to make choices legible, not to defend them against evidence — a designed distinction that does not survive contact with feel gets dropped, and the entry recording it gets superseded rather than argued for. Do not treat a persuasive past entry as a commitment.
- Always propose a short plan before creating or modifying multiple assets.
- Work in small, verifiable vertical slices. After each slice, stop and wait for feedback.
- Never delete assets or change project settings without explicit approval.
- When implementing an attack or defensive move, include the relevant input binding, montage/notify windows, stamina cost, and at least a basic success/failure outcome.
- **An animation plays in full across the mechanical duration it belongs to.** Fit the clip to the duration; never trim it to hit a number nobody has felt. The fix for a bad-feeling number is to change the number. Reasoning in `Docs/Combat-Decisions.md`.
- After making changes, briefly list the assets created or modified and the key values set.
- **Commit and push whenever a notable contribution is finished**, without waiting to be asked. The bar is a coherent, verified unit of work — not every file edit, and not a half-finished slice. Pending *tuning* questions do not block a push; pending *correctness* verification does.
- **Every commit you author gets the `Co-Authored-By` trailer, without exception.** A trailer that is present only sometimes makes its absence ambiguous, which is worse than never using one; six commits on 2026-08-09 lost it late in a long session. Do not automate it with a hook — a hook cannot tell who wrote a change, so it would falsely claim the ones you did not.
- **Instrument before theorising.** When behaviour is wrong and the cause is not obvious, enable a trace before proposing an explanation, and prefer an experiment that manipulates the suspected cause over one that only observes it. See `Docs/Working-In-Unreal.md`.
- **Never claim something does not exist based on a filtered or derived view.** A search that finds something proves it exists; a search that finds nothing proves only that your filter did not match. Three wrong claims in `Docs/Animation-Library.md` came from exactly this — a prefix filter, a first-token summary, and a mismatched granularity — each reported as absence. Before writing "there is no X", search the authoritative source unfiltered, try synonyms and known misspellings, and quote the command you ran. If you cannot show the search, do not make the claim; say you did not find it and name where you looked.
- **If you edited this file during a session, re-read all of it before finishing.** Edits made hours apart contradict each other easily. Check for stale claims and for rationale that belongs in `Docs/Combat-Decisions.md` — this file states rules and current facts, not arguments. Do not delete lines you did not write without asking: most of them are scar tissue from something that went wrong once.

## Current Focus

**Order agreed 2026-08-10.** Every item here gets done — only the sequence was in question. Items
1–3 are ordered by dependency, not preference; the rest is judgement and may be revisited.

1. ~~Light → Heavy → Charged Heavy with correct input timing and basic montages.~~ **Done 2026-08-09**, verified in play. Offense still lacks the light string, knockdown, and block-safety.
2. ~~**Dodge.**~~ **Done 2026-08-10**, verified in play including its animation: eight-way, i-frames for its full duration, cost applied rather than gated, cancels an attack before its commit checkpoint, refused while airborne, `DodgeSeconds` 0.4, `AM_Dodge` built from eight untrimmed `Dash_*` clips at a derived 1.833× so animation and duration match exactly. The **stamina economy** shipped with it and is verified too — regen, the post-action pause, exhaustion entering at 0 and releasing at full. All eight directions became reachable with item 3a.
3. **The character becomes a sword-and-shield fighter** — camera-relative facing, the `SwordShield` locomotion set, and the sword and shield in hand. Built and verified in play as 3a → 3b → 3c.
   - ~~**3a — camera-relative facing.**~~ **Done 2026-08-10**, verified in play: **all eight dodge sections fire**, `dir` and `section` agreeing across 24 dodges. Facing snaps to camera yaw while there is movement input and turns smoothly at `StationaryTurnRateDegrees` while there is not, keyed on input rather than velocity. **The dodge is no longer blocked.**
   - ~~**3b — the sword and shield.**~~ **Done 2026-08-10**, verified in play. Attached to the bundle's **`Sword` / `Shield` sockets** on `hand_r` / `hand_l` — correct at identity, since the sockets carry the grip rotation and the shield's `0.25, 0.20, 0.30` scale correction. Required swapping the character mesh to `GDHBundle`'s `SKM_Manny`, because Epic's `SKM_Manny_Simple` has neither the sockets nor the `weapon_*` bones, plus a reverse `CompatibleSkeletons` entry so `AM_LightAttack_01` and the ABP survived the swap. Reasoning and the verification trap in `Docs/Animation-Library.md`.
   - **3c — locomotion.** Our own `ABP_Combat` and an eight-direction blendspace replacing Epic's `ABP_Manny_Combat`. **Its urgency argument was false** — strafing already animates correctly; this is replaced because it is template content at `/Game/` root built for a hierarchy we do not derive from, and animates a generic Manny rather than a stance.
   - Done when: strafing reads as a sword-and-shield fighter.
   - Reasoning: `Docs/Combat-Decisions.md`, 2026-08-10.
4. **Death, minimally.** Health reaches 0 and *nothing happens* — the character keeps acting and the bar stops meaning anything, which quietly poisons every damage observation after it. All the `Death`/`Respawn` code in the tree is Epic's `Variant_Combat` hierarchy, which our classes do not derive from — verified 2026-08-10, all 14 files matching `death|respawn|revive` sit under `Variant_Combat/`. Read it as reference, do not inherit it. Copy the `State.Exhausted` pattern instead — tag applied, abilities blocked, timed release — plus a debug auto-revive. Deliberately defers the design questions (respawn rules, whether death routes through knockdown, whether the dummy dies) to item 11.
5. **Dodge travel distance** — `AnimRootMotionTranslationScale`. Unblocked by 3a: all eight directions are judgeable now rather than one plus guesswork. Still after item 4, because tuning it means dodging repeatedly beside an attacking dummy.
6. **The sword-and-shield attack swap.** New attack montages, the `Release Window` notify re-placed **by hand** (not scriptable), `ReleaseStartSeconds` updated per branch. The most expensive slice in the project — choose the light clips with the string (item 9) in mind, or those notifies get placed twice.
7. **Block, and the blockstun that arrives with it.** Blockstun disables offense and parry for a duration set by the attack blocked; it is the first *reactive* stun state and pulls in plumbing hitstun will also need. Content verified 2026-08-10, all in `SwordAndShieldAnimV1` (our pack): `DefenseStart` / `Defense_Loop` / `DefenseEnd` for the held guard, **plus eight `Defense_Hit_*` clips** — four directional block impacts and four die-while-blocking variants. The impacts are what blockstun reads as, and nothing previously recorded that they exist.
8. **Input buffering.** Before the light string, not after: strings are where dropped inputs hurt most, and until this exists every timing verdict is confounded by presses that never registered.
9. **The 2–4 hit light string.**
10. **Parry.** Verified 2026-08-10 by unfiltered search for `Parry|Deflect|Riposte|Counter` across all 6,576 assets: **six clips exist in the entire bundle.** `SwordShield` has exactly one and no failed-parry variant; the other five are Katana's `Deflect` set, which is the wrong stance. **The one clip is `AS_SwordSwordAnimV3_Block1_Parry_RM` — V3, while the dodge and locomotion are V1.** That stance mismatch is unresolved and is the first thing to settle in this slice, not during it.
11. **Hit reaction, knockdown, and death's full treatment** — the stun family together, since they share state plumbing and the questions item 4 deferred get answered here. Verified 2026-08-10: `SwordSwordAnimV3` has **four directional** `Hit_<DIR>` and **four directional** `Death_<DIR>` clips, not single standalone ones. **`SwordShield` has no get-up content whatsoever** — unfiltered search for `Rise|GetUp|StandUp|Recover|Wake|Prone|Ground|KnockDown|Knock|Fallen|Down` returns zero for the archetype. It exists only in `DaggerCombatAnimationV1` (18: `Rise1`–`Rise9`, two variants each) and `Unarmed` (8, including the bundle's only explicit `KnockDown` and `KnockDown_React`). Knockdown recovery therefore needs a **cross-archetype migration** — raise it before the slice starts.
12. Recovery and punish windows — currently unmanaged; every attack's recovery is whatever is left of its montage.