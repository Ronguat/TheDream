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
- **Live multiplayer sessions** — lobbies, matchmaking, connection handling, a real network test
  pass. *Building networkably is not on this list and never should have been;* see below.

## Building for the network

**Networkability is a property of every slice, not a later phase** (2026-08-11, replacing an
earlier call that put netcode flatly out of scope). What is deferred is *running* multiplayer —
sessions, lobbies, a real test pass against latency. Being *able* to is not.

The model is **server-authoritative with client prediction**, the one GAS is designed around.
Three rules bind all new work:

- **New state is a replicated property or an attribute, never a loose gameplay tag.** Loose tags
  do not replicate. Follow `bDead` / `bExhausted` on `ATDCombatCharacter`: the server decides,
  the bool replicates, `OnRep` applies the tag locally. **Decide on the server, apply everywhere.**
- **Authority-sensitive work is explicitly gated.** Damage, attribute writes and hit detection
  belong to the server. State that only the local machine can know — input, buffered presses,
  camera-relative facing — deliberately does not.
- **Latency comes out of the reactability budget, so it is a design input.** The heavy's
  coil→damaging window is ~240 ms and already "right at the edge of human reaction"; a network
  does not get to spend that budget silently. When a timing is chosen, say what it looks like
  with a round trip in it.

**The ASC lives on `ATDPlayerState` for players** (2026-08-11). The training dummy is an
unpossessed placed pawn with no PlayerState, so `ATDCombatCharacter` *resolves* which ASC it
uses rather than assuming one — never reach past `AbilitySystem` to the owned fallback.

**Still outstanding:** prediction windows, lag compensation for i-frames, client-side stamina
prediction, and **2** network-unaware `SetTimer` sites — the charged attack's checkpoint and the
dodge's duration. Those two are the same problem twice: the dodge timer *is* the i-frame lag
compensation. (Recounted 2026-08-11 from 14; the breakdown is in `Docs/Combat-Decisions.md`,
along with the audit and the reasoning behind the model.)

**Nothing has ever run with two machines**, so every claim of networkability above is about
structure, not behaviour — including `OnRep_PlayerState`, which is written and has never fired.

**On commitment level, stated 2026-08-11 so nobody has to guess it:** *building* networkably is
non-negotiable and binds every slice, as above. *Actually networking the game* is the *final
frontier* — attempted in earnest, held with a stretch-goal mentality. The distinction matters in
two directions. It means netcode difficulty must never be a reason to compromise combat feel,
which is what the prototype exists to establish. And it means **the prototype is not a failure if
netcode proves too hard** — a combat model that is verified good and provably networkable has
answered its question. Do not let readiness work crowd out the feel work it exists to protect.

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

**The numbers are stable identifiers, not sequence.** They are referenced from `Docs/` and from
each other, so renumbering breaks cross-references for no gain. Execution order is stated here
instead, and only this line changes when the order does:

> **1 → 2 → 3 → 8 → 4 → 5 → 6 → 7 → 9 → 10 → 11 → 12**

**Items 1, 2, 3, 8, 4 and 5 are done**, plus the netcode groundwork Slices A and B, which are not
numbered items. Next is **item 6, the sword-and-shield attack swap**.

Every item here gets done; only the sequence was ever in question. Items 1–3 were ordered by
dependency, not preference; the rest is judgement and may be revisited.

**Completed items are one line plus whatever they left behind that can still bite.** What was
built is in the code and in git; what is kept here is the live consequence. Reasoning for all of
them is in `Docs/Combat-Decisions.md`.

1. ~~Light → Heavy → Charged Heavy, with input timing and basic montages.~~ **Done 2026-08-09.** Offense still lacks the light string, knockdown, and block-safety.
2. ~~**Dodge**, and the stamina economy that shipped with it.~~ **Done 2026-08-10**, moved to V3 clips 2026-08-11. `DodgeSeconds` 0.4; `AM_Dodge` is eight untrimmed V3 `Dash_*` clips at a derived 2.083×. **Distance is `DodgeTargetDistanceCm` (405), with per-direction corrections derived from `MeasuredTravelCm`** — V3's clips disagree by 90.6 uu where V1's agreed within 31.5, so a single uniform scale no longer works. **Re-measure `MeasuredTravelCm` whenever the montage is rebuilt from different clips**; stale values silently reintroduce directional bias.
3. ~~**The character becomes a sword-and-shield fighter**~~ — camera-relative facing, the props, and the `SwordShield` locomotion set. **Done 2026-08-11.** **The stance moved from V1 to V3 later the same day**: V1's whole set reads as though permanently blocking, and V1 has no `Hit` or `Death` clips at all, so the "don't mix packs" rule that chose it was never achievable. **V1 is retained for the held guard (item 7), which it is decisively better at.** Two things it left behind:
   - **The reverse `CompatibleSkeletons` entry is load-bearing.** Our mesh sits on `GDHBundle`'s skeleton while `AM_LightAttack_01` is bound to Epic's; remove that entry and the attack stops playing **silently**. Also in the regression checklist.
   - **Known art seam, not a bug:** adjacent directions disagree about the guard pose, so the shield snaps ~135° blending between them. Inherent to the source clips. The fix is an upper-body layered blend over one guard pose — which **item 7 will probably want anyway**, so it was deliberately not built twice.
4. ~~**Death, minimally.**~~ **Done 2026-08-11.** `State.Dead` is refused by the shared ability base, so a new ability cannot be authored without it. Respawn rules, whether death routes through knockdown, and whether the dummy should die at all are still **item 11's**.
5. ~~**Dodge travel distance.**~~ **Done 2026-08-11.** `DodgeRootMotionScale` stays at **1.0**; all eight directions measured and agree, so no per-direction data is needed.
6. **The sword-and-shield attack swap.** **Next up** as of 2026-08-11. New attack montages, the `Release Window` notify re-placed **by hand** (not scriptable), `ReleaseStartSeconds` updated per branch. The most expensive slice in the project — choose the light clips with the string (item 9) in mind, or those notifies get placed twice.
   - **The melee trace moves off `hand_r` and onto the weapon, and sweeps a line rather than a sphere.** Both agreed 2026-08-11. `TraceSocket` is legacy from unarmed prototyping and is already wrong — the sword has been in hand since 3b and contributes nothing to what an attack hits. **A blade is a line**, so the sweep runs blade-base to blade-tip; `TraceRadius` (45 / 55 / 65) stops being the whole hitbox and becomes the blade's thickness, which is why those values need re-judging rather than rescaling. Reasoning in `Docs/Combat-Decisions.md`.
   - **Two C++ prerequisites land before any notify is authored.** `UAbilityTask_MeleeTrace` subscribes to `Event.Melee.WindowBegin` on the whole ASC and never checks which montage sent it — correct with one montage carrying the notify, silently wrong the moment a second does. That fix and the trace move are both cheaper before the content pass than after it.
   - **The blade's length is an authored, Blueprint-exposed number, never derived from the weapon mesh.** The `Sword` socket lives on the skeleton and exists whether or not a prop hangs off it, so a mesh-derived length gives an unarmed character a well-formed **zero-length** trace — a hitbox that misses everything, with nothing null and nothing logged.
   - **The training dummy gets the new attacks too**, agreed 2026-08-11: it shares `GA_Attack` with the player, so this is the default rather than extra work, and it needs its `WeaponMesh` / `ShieldMesh` set so it is not swinging an invisible sword. **Parity is partial by design** — offense only, because offense is what the player is measured against. The dummy does not get `GA_Dodge`. Reasoning in `Docs/Combat-Decisions.md`.
   - **Clip candidates: target ~0.70 s, which is the length that lands a 250 ms impact at a play rate near 1.0.** Shorter is *not* better — a 0.433 s clip implies rate 0.62, i.e. slow motion. Both V1 and V3 have 0.700 s candidates, so **length does not choose the pack; a preview does.** Measurements, the corrected stage analysis, and the still-unmeasured impact-position assumption are in `Docs/Combat-Decisions.md`.
   - **A family's stage count is not a hit count.** Nearly every family in both packs is long opener → **one** short strike → long terminal. `Attack7` having three stages does not yield a 3-hit string.
7. **Block, and the blockstun that arrives with it.**
   - **Idea, noted 2026-08-11, not decided: use V1 as a *blocking* locomotion set.** If V3 becomes the neutral stance, V1's pervasive guard-forward pose stops being a drawback and becomes exactly the right material for the one state where a raised shield is correct. That is an **authored** block stance rather than a synthesized upper-body blend, which is the alternative item 3c deferred. It does not fully dissolve the art seam — V1's directions still disagree with each other — but it means any blend is correcting a guard pose rather than inventing one. Cheap to note now, expensive to rediscover. Blockstun disables offense and parry for a duration set by the attack blocked; it is the first *reactive* stun state and pulls in plumbing hitstun will also need. Content verified 2026-08-10, all in `SwordAndShieldAnimV1` (our pack): `DefenseStart` / `Defense_Loop` / `DefenseEnd` for the held guard, **plus eight `Defense_Hit_*` clips** — four directional block impacts and four die-while-blocking variants. The impacts are what blockstun reads as, and nothing previously recorded that they exist.
8. ~~**Input buffering.**~~ **Done 2026-08-11**, verified in play and tuned. Single slot, last press wins; the window is grace on *taps*, so a held button never expires — which is what makes a buffered heavy or charged reachable at all. The airborne dodge refusal deliberately does **not** buffer. `InputBufferSeconds` on `ATDCombatCharacter` is authoritative for the live value; **re-check it when item 12 authors a real recovery**, since it was sized against a tail nobody chose.
9. **The 2–4 hit light string.** **Measured 2026-08-11:** no family in either pack offers 3+ uniformly short stages, so the string must be assembled from short stages **across** families, or accept uneven lengths. The one exception is **V3 `Attack4`**, the only four-stage family (0.600 / 1.167 / 0.667 / 2.333), which carries two short stages inside one authored chain. V3's families are deeper than V1's generally — a real argument for V3 that pulls against V1's short two-stage openers being better for item 6's single light.
10. **Parry.** **Re-searched 2026-08-11** by enumerating every distinct `SwordShield` move rather than grepping for parry words, and the earlier picture was too thin. Beyond `Block1_Parry` there are `Block1` and `Block2` — discrete block actions with their own `_Idle` and `_Hit` — so there are **three candidate shapes plus failure states**, not one clip, and all are already migrated. The two packs split by **idiom**: V1 does held guard (item 7's), V3 does discrete actions (a parry's). The `SwordShield` archetype holds three differently-named packs (`SwordAndShieldAnimV1`, `SwordShieldAnimV2`, `SwordSwordAnimV3`) and dual-sword content is all `DualSwordAnimation*` in its own archetype — so `SwordSword` is a vendor naming quirk, not a stance. What is still open needs a preview, not a search: whether V3's guard pose reads consistently beside V1's. Details in `Docs/Animation-Library.md`.
11. **Hit reaction, knockdown, and death's full treatment** — the stun family together, since they share state plumbing and the questions item 4 deferred get answered here. Verified 2026-08-10: `SwordSwordAnimV3` has **four directional** `Hit_<DIR>` and **four directional** `Death_<DIR>` clips, not single standalone ones. **`SwordShield` has no get-up content whatsoever** — unfiltered search for `Rise|GetUp|StandUp|Recover|Wake|Prone|Ground|KnockDown|Knock|Fallen|Down` returns zero for the archetype. It exists only in `DaggerCombatAnimationV1` (18: `Rise1`–`Rise9`, two variants each) and `Unarmed` (8, including the bundle's only explicit `KnockDown` and `KnockDown_React`). Knockdown recovery therefore needs a **cross-archetype migration** — raise it before the slice starts.
12. Recovery and punish windows — currently unmanaged; every attack's recovery is whatever is left of its montage.