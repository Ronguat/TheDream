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

Timings land within about a frame, biased late. `GA_Attack`'s `Branches` array is authoritative for live values; the reasoning behind the model is in `Docs/Combat/Decisions.md`.

### Defense
- **Block** (hold RMB): 180° forward. Drains stamina (heavies drain more; charged heavies exhaust if blocked). Can cancel attack startup into block.
- **Dodge** (LShift): Directional (or back if stationary). Costs 50 stamina. Grants i-frames for the duration.
- **Parry** (MB4 or LAlt+RMB): 400 ms active window, 360° coverage. Success = no stamina drain, no blockstun, and (vs melee) 500 ms offensive lock on attacker. Success vs ranged redirects to crosshair. Whiff = 1000 ms defensive lockout. Successful parries can retrigger without impeding other actions.

### Stun & Knockdown
- Blockstun: Disables offense + parry for a short duration based on the blocked attack.
- Hitstun: Brief, enables combos.
- Knockdown: 1.5 s default get-up. Early get-up via Dodge, Block, or Attack. Get-up attack knocks back, short recovery, very punishable on block/whiff.

### Stamina
- Max 100.
- Dodge = 50.
- Blocking drains based on attack + blocking weapon.
- 0 stamina → Exhausted (no defensive actions or jump) for 4 s.
- Regen 25/s. Paused during defensive actions and for 1 s after.

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
Two files carry knowledge the code cannot. Read them before working in their area; keep them true in the same commit that makes them wrong.
- **`Docs/Working-In-Unreal.md`** — how to drive the editor and its MCP toolset without losing work: which writes silently do nothing, when Live Coding is safe versus needing a full editor-closed rebuild, what is not scriptable at all, and the standing regression checks for combat changes. Read before writing assets or C++.
- **`Docs/Combat/Decisions.md`** — dated log of combat decisions, the reasoning behind them, and the questions still open. Append an entry whenever a gameplay choice is made that a future reader could reasonably second-guess; never rewrite an entry to match new code, supersede it with a new one.

Deliberately **not** kept: per-system design docs. Local rationale belongs in header comments, which are read at the moment the code is; a doc that describes a system drifts out of sync and then gets trusted over the code.

## Working Rules
- **Combat and gameplay work is deliberate, not vibed.** Minimize assumptions and state the reasoning, even when it is slower. If a gameplay question has more than one defensible answer, raise it rather than picking one quietly — and record the choice in `Docs/Combat/Decisions.md`. Unprompted initiative is welcome for debug and tooling conveniences (adding a readout to the debug HUD, say); it is not welcome for anything that changes how the game plays.
- Always propose a short plan before creating or modifying multiple assets.
- Work in small, verifiable vertical slices. After each slice, stop and wait for feedback.
- Never delete assets or change project settings without explicit approval.
- When implementing an attack or defensive move, include the relevant input binding, montage/notify windows, stamina cost, and at least a basic success/failure outcome.
- Prefer clarity and tunability over cleverness.
- After making changes, briefly list the assets created or modified and the key values set.
- **Commit and push whenever a notable contribution is finished**, without waiting to be asked. The bar is a coherent, verified unit of work — not every file edit, and not a half-finished slice. Pending *tuning* questions do not block a push; pending *correctness* verification does.
- **Every commit you author gets the `Co-Authored-By` trailer, without exception.** A trailer that is present only sometimes makes its absence ambiguous, which is worse than never using one; six commits on 2026-08-09 lost it late in a long session. Do not automate it with a hook — a hook cannot tell who wrote a change, so it would falsely claim the ones you did not.
- **Instrument before theorising.** When behaviour is wrong and the cause is not obvious, enable a trace before proposing an explanation, and prefer an experiment that manipulates the suspected cause over one that only observes it. See `Docs/Working-In-Unreal.md`.
- **If you edited this file during a session, re-read all of it before finishing.** Edits made hours apart contradict each other easily. Check for stale claims and for rationale that belongs in `Docs/Combat/Decisions.md` — this file states rules and current facts, not arguments. Do not delete lines you did not write without asking: most of them are scar tissue from something that went wrong once.

## Current Focus
1. ~~Light → Heavy → Charged Heavy with correct input timing and basic montages.~~ **Done 2026-08-09**, verified in play. Offense still lacks the light string, knockdown, and block-safety.
2. **Block, Dodge, and Parry with stamina costs.** Highest leverage: spacing and whiff punish are the top feel goals, and neither is judgeable against a dummy that cannot fight back.
3. Simple hit reaction + knockdown on the dummy.
4. Recovery and punish windows — currently unmanaged; every attack's recovery is whatever is left of its montage.
5. The 2–4 hit light string, then the stamina economy (the attribute exists; costs, regen and exhaustion do not).