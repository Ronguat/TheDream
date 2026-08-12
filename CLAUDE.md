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
- **Latency comes out of the reactability budget, so it is a design input.** The tightest window
  in the game is the **light's 200 ms**, measured from the montage starting on the press — not
  the heavy's coil, which is 350 ms. A network does not get to spend that budget silently. When a
  timing is chosen, say what it looks like with a round trip in it.

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
| Light | 150 ms | **200 ms** |
| Heavy | 450 ms | **500 ms** |
| Charged Heavy | (held past 450 ms) | **750 ms** |

**Space is authored the same way, as of 2026-08-12.** Each tier also authors its damaging volume — reach, arc and a vertical band — as an `FTDAttackHitbox` on its branch, so range is a designed number rather than a property of the clip. `GA_Attack`'s `Branches` array is authoritative for both.

Two rules the model depends on:
- **Windup length is preset.** Releasing early inside a band changes nothing — the attack still takes its full time to arrive. The cost is real dead time, and it is what stops a fractionally-held heavy from dominating light.
- **Reactability is measured from the tell, not from the press.** All tiers share one windup, so the defender's window is coil → damaging. Lengthening a windup does not by itself make an attack more reactable; moving the coil earlier does.

- **Light**: released before 150 ms, hits at 200 ms. 2–4 hit string (weapon dependent) — *not yet built; currently a single hit*. First hit safe on block; subsequent hits are not. Any hit in the string guarantees the rest — **coupled to the ban on heavy→light; they are one decision, see `Docs/Combat-Decisions.md`**. Last hit knocks down but has heavy endlag. Minimal stamina damage. It never *coils*, so it carries no tell that distinguishes it from a heavy — but **it is not unreactable**, which this file claimed until 2026-08-11: the montage starts on the press, so the windup is a tell from frame one. **250 ms was reactable and the light moved to 200 ms because of it** (2026-08-12); the 150 ms boundary is the measured floor for trivially consistent inputs, not a guess. Whether 200 ms is far enough is itself unverified — it has never been played against a human.
- **Heavy**: held past 150 ms, hits at 500 ms. Single hit. Safe on block, punishable on whiff. Knocks down. Higher range, moderate stamina damage. *Currently **350 ms** coil → damaging, more reactable than intended; deferred until the ladder is tuned as a whole. **It got worse when the light got faster**, and that coupling is easy to miss: the coil begins where the light stops being available, so moving that boundary 200 → 150 ms widened the heavy's tell window by the same 50 ms. Any future change to the light's boundary moves the heavy's reactability with it.*
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
- **`Docs/Combat-Decisions.md`** — dated log of combat decisions and the reasoning behind them, plus the working sections at the top: **known traps**, latent defects filed against the slice that trips them; the **tuning map**, which knob to move when a verdict comes back and which obvious-looking knob is wrong; and the bridge tables for anything superseded or renamed, **including the item numbers this file stopped using on 2026-08-12**. Append an entry whenever a gameplay choice is made that a future reader could reasonably second-guess; never rewrite an entry to match new code, supersede it with a new one.
- **`Docs/Animation-Library.md`** — where animations come from, the naming convention that makes 5,319 of them searchable, what the library does *not* contain, and how to migrate one in without dragging a duplicate skeleton behind it. Read before asking for or importing any animation.

**Durable knowledge belongs in these files, not in an assistant's per-machine memory.** Anything a future contributor would need — combat reasoning, tooling behaviour, rules and current facts — goes in the repo, where it can be reviewed and corrected. Memory keeps only what is genuinely session- or machine-scoped, and *points* at the repo rather than restating it: `Docs/Working-In-Unreal.md` exists precisely because those notes were once memory-only and therefore invisible.

**One fact, one home; everywhere else points at it.** This applies *inside* the repo, not only between memory and repo — including between two items in this file, and between a doc and a code comment. **The 2026-08-12 audit found eight wrong claims and three were this**, each a fact stated twice where only one copy was updated: a dodge scale contradicted between Dodge and Dodge Distance, a cvar default documented as off in two places while the code set it on, a shield mesh recorded as absent in one file and present in another. A second copy does not reinforce a fact, it creates something nobody reviews.

The pattern that works is already in use here — *"`GA_Attack`'s `Branches` array is authoritative for live values"*. **Prefer naming the authority over restating the value**, especially for anything that lives in code. Numbers still belong in prose where they carry an argument, but then they are a *measurement with a date*, not a live value.

Deliberately **not** kept: per-system design docs. Local rationale belongs in header comments, which are read at the moment the code is; a doc that describes a system drifts out of sync and then gets trusted over the code.

## Working Rules
- **The loop is: objective → *read the traps* → measure → plan → greenlight → execute.** Named by the user 2026-08-12,
  after it turned a two-session bug hunt into a measured one-line fix. Given an objective, do all the
  reading and measuring needed to actually understand the problem, **then present a plan and stop.**
  Three parts carry the weight. *Measuring comes before planning*, so the plan is built on numbers
  instead of on a guess about what is wrong — a plan proposed before measuring is just the first
  hypothesis wearing a schedule. *The pause is real*: do not begin work that has not been described
  and agreed, however obvious it looks. *Execution after a greenlight is unattended* — drive the
  editor closes, rebuilds, asset writes and verification through to the end rather than handing
  steps back one at a time. If a measurement taken mid-execution changes what should happen, that is
  a **new plan** and needs its own greenlight; say so and stop rather than quietly widening scope.
  **Reading the traps is a step, not a hope.** Before measuring, grep `Docs/Combat-Decisions.md`'s
  known-traps section for the name of the item in play and say what it turned up. That section asks to be
  re-read at the start of the slice it is filed against, and on 2026-08-12 that failed exactly as an
  unenforced instruction does: a trap discharged during Attack Swap sat filed for a day and was found
  by an audit rather than by anyone reading it. One grep, at the moment it is most relevant.
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
- **Report what was found, not who found it.** When the user contributes an idea or catches
  something, note it in a clause if it matters to the reasoning and move on. Do not tally credit,
  and do not apologise for not having thought of it first — said directly on 2026-08-11 after two
  such creditings in one message: *"you needn't grovel just because you didn't think of something…
  a healthy collaborative relationship does not expect a 100-0 scoreline."* The over-crediting **is**
  the scorekeeping. This does not touch correcting the record: a wrong claim still gets corrected
  plainly, and a rejected alternative still gets written into `Docs/Combat-Decisions.md` with its
  reasoning intact, because that is project knowledge rather than credit.
- **Instrument before theorising.** When behaviour is wrong and the cause is not obvious, enable a trace before proposing an explanation, and prefer an experiment that manipulates the suspected cause over one that only observes it. See `Docs/Working-In-Unreal.md`.
- **Never claim something does not exist based on a filtered or derived view.** A search that finds something proves it exists; a search that finds nothing proves only that your filter did not match. Three wrong claims in `Docs/Animation-Library.md` came from exactly this — a prefix filter, a first-token summary, and a mismatched granularity — each reported as absence. Before writing "there is no X", search the authoritative source unfiltered, try synonyms and known misspellings, and quote the command you ran. If you cannot show the search, do not make the claim; say you did not find it and name where you looked.
  **And date every absence claim you write down, with what you searched.** Absence claims rot faster than any other kind, because they are statements about a world that keeps changing around them — the 2026-08-12 audit found two that had gone stale, including "nothing places the dummy below the player" about a level that has a ramp. `Docs/Animation-Library.md` shows the form to copy: *"checked 2026-08-10 across all 6,576 rows of the index"*. A dated, scoped claim tells the next reader its shelf life; a bare one is indistinguishable from a guess and will be trusted like a fact.
- **If you edited this file during a session, re-read all of it before finishing.** Edits made hours apart contradict each other easily. Check for stale claims and for rationale that belongs in `Docs/Combat-Decisions.md` — this file states rules and current facts, not arguments. Do not delete lines you did not write without asking: most of them are scar tissue from something that went wrong once.

## Closing down a session

Run this when the user says they are winding down, or when a session ends on a finished item. It
exists because the 2026-08-12 audit found eight wrong claims across the docs, and **every one of
them was cheap to catch at a boundary and expensive to trip over later**. Steps 3 and 4 are the
audit in miniature; the rest is making sure nothing is left on the floor.

1. **Make the editor state safe.** `AssetTools.save_assets` with an empty list, then **`git status`
   — and read it.** Calling save is not the check; *seeing the files listed* is. A write that was
   never saved and a write that is saved but not yet live look identical from inside the editor.
   Announce before closing the editor, always.
2. **Leave nothing verified uncommitted.** Push anything finished. For anything deliberately left
   out, say so and why — pending *tuning* does not block a push, pending *correctness* does.
3. **Check the docs you touched, with two greps.** `grep -n "supersede" Docs/Combat-Decisions.md` —
   every hit needs a row in the supersession table, and two were missing on 2026-08-12. Then confirm
   any cross-reference you wrote resolves to a section that exists; three pointed at a section that
   had been deleted.
4. **Discharge what you fixed.** Did this session fix anything filed as a trap? Clear it *and say
   what discharged it*, in the same commit. Did anything supersede an entry, or make an absence
   claim? Rows and dates, per the rules above.
5. **Update the focus.** If the next item changed, `Current Focus` is the only place that says so —
   and completed items keep one line plus whatever they left behind that can still bite.
6. **Check memory is still pointing, not restating.** Only `combat-prototype-state` normally needs
   touching, and only if the state actually moved. Anything a future contributor would need belongs
   in the repo instead.
7. **Hand off explicitly.** Where to pick up, what is verified versus merely written, and what is
   open. **Name anything claimed but not verified** — that is the item most likely to be believed
   next session and least likely to be re-checked.

## Current Focus

**Items are named, not numbered** (2026-08-12, replacing fifteen numbers). The numbers were stable
identifiers that meant nothing, so every reference cost a lookup — and the traps in
`Docs/Combat-Decisions.md` had already begun writing *"before block (item 7)"* unprompted, which is
a scheme failing over in slow motion. Names keep the property the numbers were chosen for: **a name
does not change when the order does.** What a name can do that a number cannot is *go wrong*, so a
renamed item gets a row in the retired-item-numbers section, exactly as a renamed symbol does.

**The dated archive still says "item 7"** in 39 places and is never rewritten. `Docs/Combat-Decisions.md`
carries the number → name bridge; do not renumber anything to match it.

Execution order, the only line that changes when the order does:

> **~~Attack Ladder~~ → ~~Dodge~~ → ~~Sword & Shield~~ → ~~Input Buffer~~ → ~~Death~~ → ~~Dodge Distance~~ → ~~Attack Swap~~ → ~~[hover bug]~~ → ~~[facing pass]~~ → Lunge + Recovery → Block → Light String → Parry → Stun → Settings**

**Structure Audit is deliberately absent from that line** — it is triggered by the combat model
being verified good, not by a position; see its entry at the end.

**Pick up at Lunge, which ships with Recovery as one slice** (2026-08-12). Both were moved ahead of
Block on the same rule: **a number that another number is felt against gets authored first.**
Spacing is measured against travel, and advantage on block is blockstun minus recovery — so
authoring either defensive number against a placeholder repeats the error the first move was made
to avoid. Recovery also feeds the Light String's endlag, how long facing stays committed, and
`InputBufferSeconds`. Reasoning and what was rejected are in `Docs/Combat-Decisions.md`.

**Attack Ladder, Dodge, Sword & Shield, Input Buffer, Death, Dodge Distance and Attack Swap are
done**, plus three things that were never items: the netcode groundwork Slices A and B, the **hover
bug**, and the **facing pass**. The last two closed on 2026-08-12 and left three live rules, all
also stated where they are enforced:

- The mesh's relative Z and `InitCapsuleSize`'s half-height **must change together** — they are
  adjacent in `ATheDreamCharacter`'s constructor for that reason. A 6 cm mismatch was invisible for
  weeks because foot IK absorbed it everywhere the IK runs.
- **`TurnRateDegrees` is derived from the light's commit time** and must move with it. Filed as a
  trap; Lunge works next to it.
- **Foot IK now runs during montages** (`ABP_Combat`'s Control Rig `Alpha` is a literal 1.0). That
  is polish, kept because ramp attacks adapt correctly — not a fix propping anything up.

Open and **not** a defect: the character can stand on the ramp's near-vertical edge face, so walking
off it descends that face before free fall. Whether `MaxWalkableFloorAngle` should permit it has
never been examined; the value has not been read.

Every item here gets done; only the sequence was ever in question. The first three were ordered by
dependency, not preference; the rest is judgement and may be revisited.

### Done

**Completed items are one line plus whatever they left behind that can still bite.** What was
built is in the code and in git; what is kept here is the live consequence. Reasoning for all of
them is in `Docs/Combat-Decisions.md`.

- **Attack Ladder** — ~~Light → Heavy → Charged Heavy, with input timing and basic montages.~~ **Done 2026-08-09.** Offense still lacks the light string, knockdown, and block-safety.
- **Dodge** — ~~the dodge, and the stamina economy that shipped with it.~~ **Done 2026-08-10**, moved to V3 clips 2026-08-11. `DodgeSeconds` 0.4; `AM_Dodge` is eight untrimmed V3 `Dash_*` clips at a derived 2.083×. **Distance is `DodgeTargetDistanceCm` (405), with per-direction corrections derived from `MeasuredTravelCm`** — V3's clips disagree by 90.6 uu where V1's agreed within 31.5, so a single uniform scale no longer works. **Re-measure `MeasuredTravelCm` whenever the montage is rebuilt from different clips**; stale values silently reintroduce directional bias.
- **Sword & Shield** — ~~the character becomes a sword-and-shield fighter:~~ camera-relative facing, the props, and the `SwordShield` locomotion set. **Done 2026-08-11.** **The stance moved from V1 to V3 later the same day**: V1's whole set reads as though permanently blocking, and V1 has no `Hit` or `Death` clips at all, so the "don't mix packs" rule that chose it was never achievable. **V1 is retained for the held guard (Block), which it is decisively better at.** Two things it left behind:
   - ~~**The reverse `CompatibleSkeletons` entry is load-bearing.**~~ **Resolved 2026-08-12.** `AM_LightAttack_01` was bound to Epic's skeleton while our mesh sits on `GDHBundle`'s, so the attack played only via a reverse `CompatibleSkeletons` entry — and would have stopped **silently** if it were ever removed. **`AM_Attack` is built from the clip itself, so it inherits GDH's skeleton and the dependency is gone.** Build a montage *from its sequence*, never empty-then-assign — that is what makes the skeleton correct by construction. **This did *not* fix the attack hover**, which was predicted and was wrong — the hover turned out to be a 6 cm mesh offset and nothing to do with skeletons at all (fixed 2026-08-12).
   - **Known art seam, not a bug:** adjacent directions disagree about the guard pose, so the shield snaps ~135° blending between them. Inherent to the source clips. The fix is an upper-body layered blend over one guard pose — which **Block will probably want anyway**, so it was deliberately not built twice.
- **Death** — ~~death, minimally.~~ **Done 2026-08-11.** `State.Dead` is refused by the shared ability base, so a new ability cannot be authored without it. Respawn rules, whether death routes through knockdown, and whether the dummy should die at all are still **Stun's**.
- **Dodge Distance** — ~~dodge travel distance.~~ **Done 2026-08-11.** Measured all eight directions and shipped a single uniform scale, which was correct for V1's clips. ***Superseded the same day by the V3 swap — see Dodge above, which is authoritative.*** V3's clips disagree by 90.6 uu, so distance is now an authored `DodgeTargetDistanceCm` with per-direction corrections. This line said "no per-direction data is needed" until 2026-08-12, directly contradicting **Dodge**; anyone acting on it would have undone the fix.
- **Attack Swap** — ~~the sword-and-shield attack swap.~~ **Done 2026-08-12**, regression pass passed. **`AM_Attack`** plays V3 `Attack4_Stage1_Complete`, the notify sits at exactly **0.3000 for 0.1500** (measured from the montage, not assumed), and the ladder is **150 ms input boundary → hits at 200 ms → 150 ms of release**. Because the notify's width and `ReleaseSeconds` agree exactly, **the release plays at rate 1.000** — the strike's damaging frames run at the speed they were animated, which is the only setting where animation and mechanic do not disagree. What it left behind that can still bite:
   Live consequences only; every one has a dated entry with the reasoning:
   - **The three wedges are deliberately uniform** (150 cm / 60° / ±70 on all three branches) and get re-authored **alongside Lunge** — reach and travel are one felt quantity, and two of the three tiers are still playing the light's clip. `RootMotionScale` is back to **1.0** for the same reason. The dummy's spacing is approximate until Lunge exists. The vertical band is **unverified**: it spans ±70 against a 96 cm capsule half-height, so it excludes nobody standing and only matters on slopes or against a jump. *(It was recorded as untestable "because nothing places the dummy below the player" — `L_CombatTest` has a ramp, so it always was testable. An absence claim from an incomplete view of the level.)*
   - **Displacement is two scales that multiply.** `UTDMeleeAttackAbility::RootMotionScale` applies from the press and **every tier shares it** — the windup is shared by design, so a charged that pulled further forward would be a tell from frame one. `FTDAttackBranch::RootMotionScale` applies from the commit checkpoint and is the only per-tier displacement there can be. A tier needing more than its clip performs after commit needs **its own clip**, not a bigger number.
   - **An attack's damaging volume is authored, not traced off the weapon.** `FTDAttackHitbox` is a wedge in the attacker's frame; **`MaxReachCm` is the attack's range**. The blade trace and all six of its properties are deleted. **Anything derived from the art inherits the art's accidents silently** — the general form, and why the volume is authored rather than measured.
   - **Facing freezes from commit to the end of the ability, recovery included**, instantly in both directions. An actor-frame volume needs a stable actor frame; steering stays free through windup, which keeps the whole cancellable portion of an attack steerable. **Recovery's `RecoverySeconds` therefore sets commitment length as well as punish length** — one number, two jobs. **Whoever takes facing away restores it in `EndAbility`**, where every exit path converges, never on the montage delegates.
   - **Facing is `TurnRateDegrees` (1200°/s) in all states, and it is derived, not chosen:** 180° ÷ the light's `HoldUntilSeconds`. **Change it whenever that commit time changes** — nothing enforces the link, and below the derived value attacks silently point where the turn got to rather than where you aimed. `IdleTurnRateDegrees` (300) applies only when `IsIdle()` — no input, no ability, no buffered press, on the ground — and is pure taste, because the fast rate resumes at the press. **The split is by whether a number may be tuned by feel.**
   - **`bAllowPhysicsRotationDuringAnimRootMotion` is `true`**, set in `ATheDreamCharacter`'s constructor against a UE default of off. It is the **only** rotation path now, so disabling it freezes facing everywhere, not just at rest. It hands rotation to every root-motion ability, so anything wanting a committed direction says so via `SetAbilityFacingLocked` — the dodge does, having previously got it for free. Never re-disable it to fix one ability.
   - **Combatants ignore `ECC_Camera`**, so an opponent at melee range does not yank the camera boom forward. Level geometry still blocks it; only bodies are exempt.
   - **The training dummy shares `GA_Attack` with the player**, so offense parity is the default rather than extra work, and its `WeaponMesh` / `ShieldMesh` are set. **Parity is partial by design** — offense only; the dummy does not get `GA_Dodge`.
   - **Length does not choose a clip; a preview does.** Shorter is *not* better — a clip too short for its target implies a play rate below 1.0, which is slow motion and as much an artifact as a fast-forward. And **a family's stage count is not a hit count**: nearly every family is long opener → *one* short strike → long terminal.
- **Input Buffer** — ~~input buffering.~~ **Done 2026-08-11**, verified in play and tuned. Single slot, last press wins; the window is grace on *taps*, so a held button never expires — which is what makes a buffered heavy or charged reachable at all. The airborne dodge refusal deliberately does **not** buffer. `InputBufferSeconds` on `ATDCombatCharacter` is authoritative for the live value; **re-check it when Recovery lands**, since it was sized against a tail nobody chose.

### Remaining

In execution order. **Lunge and Recovery ship together**; the rest are sequential.

- **Lunge + Recovery** — authored attack displacement, and the punish window it gets tuned against. **One slice** (2026-08-12): each authors a phase of the attack that the animation currently decides by default, both expect per-tier clips to land while they are open, and Recovery's number is what every later defensive verdict is measured from. **Recovery is done; Lunge is next.** Measured while planning it, and load-bearing for both: a light attack travels **≈77 cm** on the clip's own root motion at scale 1.0, against a `MaxReachCm` of 150 — so an attack closes more than half its own reach while swinging, and the placed dummy at 200 cm sits **8 cm beyond standing reach** (the test is `CentreDistance − TargetRadius > MaxReachCm`, i.e. 200 − 42 = 158). Every hit that lands today does so *only* because of travel Lunge is about to take over.
  - **Lunge replaces root-motion scaling.** Added 2026-08-12 after play found `RootMotionScale` at 3.0 *"still a bit too animation-driven"*. A multiplier cannot decouple you from an authored curve; it only makes the animator's acceleration, pauses and stop three times larger. Lunge authors distance and timing outright, on the same terms the wedge already does for reach.
  - **It must be built on a GAS root motion source** (`UAbilityTask_ApplyRootMotion*`: `ConstantForce`, `MoveToLocation`, `MoveToActorForce`), never on `SetActorLocation`, `AddMovementInput` or `LaunchCharacter`. Those drive CMC's root motion source system, so they are authored *and* network-predicted — the netcode audit's objection to hand-rolled displacement is real and this is what answers it rather than overrides it.
  - **Lunge during the windup may not differ by tier**, inheriting the constraint that split `RootMotionScale` in two: the shared windup is what leaves the light without a distinguishing tell. **Today that property is an accident of the coil and will not survive the port** (measured 2026-08-12): a charged travels 65–75 cm against the light's 77 despite a windup 4.7× longer, because the coil freezes the montage and root motion is a function of montage *position*. A GAS root motion source runs on wall-clock time instead, so "travel X during windup" would carry a charged 4.7× further and announce the tier from frame one. **Lunge has to state the constraint explicitly rather than inherit it.**
  - **Lunge overrides an attack's root motion outright** (decided 2026-08-12) rather than adding to it — left alone the two add, and the authored distance becomes a lie by whatever the animation contributes. `RootMotionScale` goes to 0 for any attack Lunge drives; a scale surviving alongside it would put the animator back in the loop, which is what the mechanic exists to remove.
  - **Defensive moves are out of scope for Lunge, provisionally.** Dodges already function well on scaled root motion, the name does not fit them, and block and parry very likely want **no** root motion at all. Raised and set aside by the user rather than refused — an authored dodge direction is a coherent idea, just not one anything has asked for.
  - ~~**Recovery becomes an authored `RecoverySeconds` with a derived play rate.**~~ **Done 2026-08-12.** Per branch, in absolute seconds, montage warped to fit — an attack is now three authored durations. `RecoveryPlayRate` is deleted. **Recovery ends at blend-out** (the user's call between the two resolutions the trap offered), so the authored number is the ability's real lifetime and the clip's last 0.25 s is follow-through that is mechanically over. Verified in play: light 0.268–0.271 s against 0.2667, charged 0.504–0.507 s against 0.500. **Tuned and play-verified 2026-08-12: light 0.40, heavy 0.50, charged 0.60** — the user's values, verdict *"very good and expected"*, and the first time the asset agrees with the spec's *charged has heavy endlag*. Heavy was thrown ~40 times, closing the branch the implementation pass never exercised.
  - **The blend-out boundary moves with the play rate** and this cost a bug already. With `BlendOutTriggerTime` negative the engine blends when the *remaining time at the current rate* hits the blend duration, so `Length - BlendTime` is right only at rate 1.0. Solved in `ComputeRecoveryPlayRate`; never reintroduce a fixed boundary.
  - Open until play: distance-plus-duration or distance-plus-curve, phase-relative or absolute window, per branch or per attack. **Tuned in tandem with re-authoring the wedges**, once each attack has its own animation — reach and travel are one felt quantity.
- **Block** — the held guard, and the blockstun that arrives with it.
   - **Idea, noted 2026-08-11, not decided: use V1 as a *blocking* locomotion set.** If V3 becomes the neutral stance, V1's pervasive guard-forward pose stops being a drawback and becomes exactly the right material for the one state where a raised shield is correct. That is an **authored** block stance rather than a synthesized upper-body blend, which is the alternative **Sword & Shield** deferred (its art-seam note). It does not fully dissolve the art seam — V1's directions still disagree with each other — but it means any blend is correcting a guard pose rather than inventing one. Cheap to note now, expensive to rediscover. Blockstun disables offense and parry for a duration set by the attack blocked; it is the first *reactive* stun state and pulls in plumbing hitstun will also need. Content verified 2026-08-10, all in `SwordAndShieldAnimV1` (our pack): `DefenseStart` / `Defense_Loop` / `DefenseEnd` for the held guard, **plus eight `Defense_Hit_*` clips** — four directional block impacts and four die-while-blocking variants. The impacts are what blockstun reads as, and nothing previously recorded that they exist.
- **Light String** — the 2–4 hit light string. **It cannot fully finish before Stun**, since its last hit knocks down; expect to ship the string and leave its terminator behind. **Measured 2026-08-11:** no family in either pack offers 3+ uniformly short stages, so the string must be assembled from short stages **across** families, or accept uneven lengths. The one exception is **V3 `Attack4`**, the only four-stage family (0.600 / 1.167 / 0.667 / 2.333), which carries two short stages inside one authored chain. V3's families are deeper than V1's generally — a real argument for V3 that pulls against V1's short two-stage openers being better for **Attack Swap**'s single light.
- **Parry.** **Re-searched 2026-08-11** by enumerating every distinct `SwordShield` move rather than grepping for parry words, and the earlier picture was too thin. Beyond `Block1_Parry` there are `Block1` and `Block2` — discrete block actions with their own `_Idle` and `_Hit` — so there are **three candidate shapes plus failure states**, not one clip, and all are already migrated. The two packs split by **idiom**: V1 does held guard (Block's), V3 does discrete actions (a parry's). The `SwordShield` archetype holds three differently-named packs (`SwordAndShieldAnimV1`, `SwordShieldAnimV2`, `SwordSwordAnimV3`) and dual-sword content is all `DualSwordAnimation*` in its own archetype — so `SwordSword` is a vendor naming quirk, not a stance. What is still open needs a preview, not a search: whether V3's guard pose reads consistently beside V1's. Details in `Docs/Animation-Library.md`.
- **Stun** — hit reaction, knockdown, and death's full treatment, taken together since they share state plumbing and the questions **Death** deferred get answered here. Verified 2026-08-10: `SwordSwordAnimV3` has **four directional** `Hit_<DIR>` and **four directional** `Death_<DIR>` clips, not single standalone ones. **`SwordShield` has no get-up content whatsoever** — unfiltered search for `Rise|GetUp|StandUp|Recover|Wake|Prone|Ground|KnockDown|Knock|Fallen|Down` returns zero for the archetype. It exists only in `DaggerCombatAnimationV1` (18: `Rise1`–`Rise9`, two variants each) and `Unarmed` (8, including the bundle's only explicit `KnockDown` and `KnockDown_React`). Knockdown recovery therefore needs a **cross-archetype migration** — raise it before the slice starts.
- **Settings menu.** Raised 2026-08-12. Mouse sensitivity is the immediate want, and it should own
  **`TurnRateDegrees`** too — that number stopped being cosmetic the moment attacks began pointing
  wherever it had turned to, so exposing it is a balance decision rather than a comfort one, and a
  player lowering it would be quietly worsening their own aim without being told. Also the natural
  home for a **turn cap** if fast-spin inputs ever need bounding, which single-rate facing already
  provides incidentally. Last in the order; nothing depends on it.

**Structure Audit — the project's structure as a whole. It has a trigger, not a position.**

Raised 2026-08-12 as an audit of what is designer-facing, **widened the same day** to the project
entire: the C++ module layout, `/Game/TheDream/` content organisation, asset naming, the gameplay
tag hierarchy, and these docs. It is listed apart from the sequence above because it does not have
a place in it.

What prompted it is still the smallest concrete example: property categories mix *design* data with
*animation* data and give no sign which is which. `ReleaseStartSeconds` is a hand-copied measurement
of a notify's position sitting beside `ReleaseAtSeconds`, which is a genuine design knob, and the
wedges are authored under `Combat|Timing` when they are spacing.

**The trigger is the combat model being verified good in play.** Deferring the audit is right —
anything reorganised before the systems settle gets reorganised again — but *last* is not a
schedule. An audit parked at the end of a list that keeps growing is one that never runs, and this
list has grown every session it has existed. Verified-good is the prototype's actual finish line
and the first moment reorganising stops being wasted work. **If that is true and this has not run,
it is next**, whatever else has accumulated by then.