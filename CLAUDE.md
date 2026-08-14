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

**Displacement too, as of 2026-08-12** — two authored distances in centimetres, not a scale on the clip. A shared **base lunge** from the press to the light's input boundary, then a **per-branch lunge** from the commit checkpoint to the end of the release window. The coil carries neither, which is what keeps the tiers indistinguishable for as long as they must be. `GA_Attack` is authoritative; the reasoning is in `Docs/Combat-Decisions.md`.

**A lunge is shortened two different ways, and they are not the same mechanism** (the stop added 2026-08-14):
- **The standoff gate *pauses* it** while a body sits ahead, per movement tick, and travel resumes if that body leaves. Geometric, so it never needs to know who the target is.
- **A hit against a viable target *stops* it outright**, permanently, for that activation. Needed because a pause cannot survive the target ceasing to exist: killing someone removes their capsule, the gate opens on the corpse, and the attacker slides through. Geometry does not stop a lunge, and neither does a target who i-framed it — **a dodged attack runs on**, which is what stops a successful evade from paying the attacker in spacing.

Both only ever subtract, so the authored distance stays a hard ceiling and neither is homing.

**An attack owns your movement and your feet** (2026-08-12, from play — this was assumed for months and written down nowhere):
- **Movement input is suppressed for the whole ability** — windup, release *and* recovery. WASD and jump do nothing; the attack's own lunge still moves you. You cannot walk out of your own commitment. Implemented as `UTDGameplayAbility::bLocksMovement`, a checkbox on the shared base, so block, parry or a future crouch adopt it the same way.
- **Attacks cannot start while airborne**, via the existing `bBlockedWhileAirborne`. It gates *activation*, not continuation — an attack that starts grounded keeps running if its lunge carries you off a ledge, which is deliberate. **The refusal is not buffered**: an attack pressed in the air is dropped, not replayed on landing.
- Air attacks are therefore out for now and are a checkbox away from being back in.

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
- 0 stamina → Exhausted (no defensive actions or jump) **until stamina refills to 100**, not for a fixed duration. Stamina floors at 0, so there is no overspending and every exhaustion is identical — dodging at 3 and dodging at 50 both land on exactly 0. Re-emptying the bar the moment you recover is allowed. **Regen continues while exhausted** — it locks out acting, not recovering, and is the only thing that can end it. **The regen pause still applies, though** (2026-08-14, from play): a player may hold block at 0 and suppress their own recovery indefinitely, which is a choice with an obvious exit rather than a trap. Reasoning in `Docs/Combat-Decisions.md`.
- **Regen runs at two rates**, `StaminaRegenPerSecond` normally and `ExhaustedStaminaRegenPerSecond` while exhausted; the character Blueprints' CDOs are authoritative, with defaults in `ATDCombatCharacter`. *(Both rates were confirmed to reach the CDOs unshadowed on 2026-08-14; `StaminaRegenPauseSeconds` beside them **is** overridden per Blueprint, which is why the distinction gets stated.)* Exhaustion is the slower of the two, so being run dry costs more than the bar it emptied. **The exhausted rate may never be zero** — regen is the only thing that ends exhaustion, so zero means permanent. Paused during defensive actions and for 0.5 s after, measured from when the action ends (`StaminaRegenPauseSeconds`), **exhausted or not** — the pause is a cost of acting and exhaustion is not a refund.
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
- **`Docs/Working-In-Unreal.md`** — how to drive the editor and its MCP toolset without losing work: which writes silently do nothing, when Live Coding is safe versus needing a full editor-closed rebuild, what is not scriptable at all, and the standing regression checks for combat changes.

  **Read it front to back at the start of every session** (2026-08-13, the user's instruction). It is not a reference to reach for when something breaks — nearly everything in it **fails silently**, so it only helps if it is already in your head before you touch the editor. It was cut from 820 lines to ~400 on 2026-08-13 to make that reasonable, and **keeping it readable is now a closedown step**: anything compressible to its rule gets compressed, and the incidents behind them live in git and `Docs/Combat-Decisions.md`. *(447 lines as of 2026-08-14 and drifting back up — it is the next file due a pass, and the trace-tag list added that day is the kind of content that should be checked for compression first.)*
- **`Docs/Combat-Decisions.md`** — dated log of combat decisions and the reasoning behind them, plus the working sections at the top. **Known traps** are latent defects filed against the slice that trips them; the **tuning map** says which knob to move when a verdict comes back and which obvious-looking knob is wrong; **which numbers have been felt** separates a played value from an assistant's guess; the **symbol index** answers *"what was decided about this thing"* for any symbol in the codebase; and the bridge tables cover anything superseded or renamed, **including the item numbers this file stopped using on 2026-08-12**. Append an entry whenever a gameplay choice is made that a future reader could reasonably second-guess; never rewrite an entry to match new code, supersede it with a new one.
- **`Docs/Animation-Library.md`** — where animations come from, the naming convention that makes 5,319 of them searchable, what the library does *not* contain, and how to migrate one in without dragging a duplicate skeleton behind it. Read before asking for or importing any animation.

**Durable knowledge belongs in these files, not in an assistant's per-machine memory.** Anything a future contributor would need — combat reasoning, tooling behaviour, rules and current facts — goes in the repo, where it can be reviewed and corrected. Memory keeps only what is genuinely session- or machine-scoped, and *points* at the repo rather than restating it: `Docs/Working-In-Unreal.md` exists precisely because those notes were once memory-only and therefore invisible.

**One fact, one home; everywhere else points at it.** This applies *inside* the repo, not only between memory and repo — including between two items in this file, and between a doc and a code comment. **The 2026-08-12 audit found eight wrong claims and three were this**, each a fact stated twice where only one copy was updated: a dodge scale contradicted between Dodge and Dodge Distance, a cvar default documented as off in two places while the code set it on, a shield mesh recorded as absent in one file and present in another. A second copy does not reinforce a fact, it creates something nobody reviews.

The pattern that works is already in use here — *"`GA_Attack`'s `Branches` array is authoritative for live values"*. **Prefer naming the authority over restating the value**, especially for anything that lives in code. Numbers still belong in prose where they carry an argument, but then they are a *measurement with a date*, not a live value.

**Name the asset, not the C++ class, and this is a correctness rule rather than a style one** (2026-08-14). A Blueprint CDO override shadows a C++ default silently, so a class is the authority only until someone touches a details panel. Four documented live values disagree with their headers today — the buffer, the regen pause, `DodgeSeconds` and `AimAssistMarginCm` — and the *asset* is expected to be right in all four, though **that is unconfirmed and is filed as an open check.** Pointing at `ATDCombatCharacter` therefore lands a reader on a number that is not live. Write *"`BP_PlayerCharacter`'s CDO is authoritative, defaults in `ATDCombatCharacter`"*, which names both and says which wins.

Deliberately **not** kept: per-system design docs. Local rationale belongs in header comments, which are read at the moment the code is; a doc that describes a system drifts out of sync and then gets trusted over the code.

## Working Rules

**Autonomy on the HOW. Interrupt on the WHAT or the WHY.** Stated by the user 2026-08-14, and it is
the principle every rule below is an instance of. Once what to build and why has been agreed, running
the how through to completion is not merely acceptable, it is **preferred** — do not hand steps back
one at a time. But if a genuine question about *what* or *why* emerges mid-run, stop and raise it.

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

**Run this only when the user says so.** Whether a session continues or concludes is theirs to decide
and never yours to infer — the user took that responsibility explicitly on 2026-08-14, when this read
*"or when a session ends on a finished item"* and thereby handed the judgement over. **A session
ending on a finished item is not a session the assistant may end.**

It exists because the 2026-08-12 audit found eight wrong claims across the docs, and **every one of
them was cheap to catch at a boundary and expensive to trip over later**. Steps 3 and 4 are the
audit in miniature; the rest is making sure nothing is left on the floor.

1. **Make the editor state safe.** `AssetTools.save_assets` with an empty list, then **`git status`
   — and read it.** Calling save is not the check; *seeing the files listed* is. A write that was
   never saved and a write that is saved but not yet live look identical from inside the editor.

   **Closing the editor is not part of this** (2026-08-12). Winding down a session does not mean
   ending the user's, and leaving it open costs nothing now that the save is verified against
   `git status` rather than against the act of closing. Anything mid-session that *does* need the
   editor closed — a header change, a full rebuild — still announces first; that rule lives in
   `Docs/Working-In-Unreal.md`, where every other reason to close it lives.
2. **Leave nothing verified uncommitted, and *propose* the push.** Commit anything finished; the
   push itself waits on the completion gate in Working Rules, so this step ends by naming what is
   ready to go rather than by sending it. For anything deliberately left out, say so and why —
   pending *tuning* does not block a push, pending *correctness* does.
3. **Audit the two files that are read every session, for bloat as well as truth.** `CLAUDE.md` and
   `Docs/Working-In-Unreal.md` are both loaded or read in full at every startup, so **length is a
   correctness problem for them and not only a tidiness one** — a file nobody finishes reading
   protects nobody, and both grew past that point once already. Re-read what you added today and ask
   of each line: *is this a rule, or is it the story of how the rule was learned?* Stories go to
   `Docs/Combat-Decisions.md` or stay in git; the rule stays here. Added 2026-08-13, when
   `Working-In-Unreal.md` was cut in half without losing a single rule.

   Then **check the docs you touched, with three greps.** `grep -n "supersede" Docs/Combat-Decisions.md` —
   every hit needs a row in the supersession table, and two were missing on 2026-08-12. Then confirm
   any cross-reference you wrote resolves to a section that exists; three pointed at a section that
   had been deleted. **Then, if you edited the known-traps section, confirm every trap still has its
   bolded header** — an `Edit` that replaces a header instead of inserting before it leaves the body
   orphaned, reading as prose belonging to whatever precedes it. That happened on 2026-08-12 and was
   caught by luck rather than by a step; a trap that no longer announces itself is the one edit in
   that file which cannot be reviewed.
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

   **And state what was done beyond what was agreed, or that nothing was.** Added 2026-08-14, and it
   is what makes unattended execution reviewable: the drift that gets caught is the drift that looks
   wrong, so the additions a reasonable person would have made anyway are exactly the ones that go
   unmentioned. Saying "nothing" is a real answer and should be said out loud rather than left to
   silence, which is indistinguishable from not having checked.
8. **Title the session, for the archive. Five words maximum.** What the session *did*, not what it
   touched — "Ship Lunge, lock attack movement" over "worked on combat". Verbs over nouns; no date,
   no item numbers, nothing the archive already knows.

   **The cap is the point, not a nicety.** Five words does not fit a summary, so it forces the
   session's dominant thing to the front and leaves everything else to step 7 — which is where a
   reader who needs detail is going anyway. **A session that did two unrelated things names the
   bigger one**; the title is an index entry, not a record, and the handoff directly above it is
   what stops the smaller one from being lost.

   **Deliberately last, because it is the most compressed artefact and compression is easiest after
   the long form exists.** By this point the handoff has already forced you to say what was done and
   what is open; the title is that, squeezed. Written any earlier it is a guess about a session that
   has not finished being accounted for. Added 2026-08-12 at the user's request, capped the same day.

## Current Focus

**Items are named, not numbered** (2026-08-12, replacing fifteen numbers). A name keeps the property
the numbers were chosen for — **it does not change when the order does** — and adds one a number
cannot have: it can *go wrong*, so a renamed item gets a row in the retired-item-numbers table
exactly as a renamed symbol does. **The dated archive still uses the numbers** (51 references,
counted 2026-08-14) and is never rewritten; `Docs/Combat-Decisions.md` carries the bridge.

Execution order, the only line that changes when the order does:

> **~~Attack Ladder~~ → ~~Dodge~~ → ~~Sword & Shield~~ → ~~Input Buffer~~ → ~~Death~~ → ~~Dodge Distance~~ → ~~Attack Swap~~ → ~~[hover bug]~~ → ~~[facing pass]~~ → ~~Recovery~~ + ~~Lunge~~ → ~~Target Lock~~ → Block → Light String → Parry → Stun → Settings**

**Structure Audit is deliberately absent from that line** — it is triggered by the combat model
being verified good, not by a position; see its entry at the end.

**Pick up at Block.** Nothing stands between it and the next session.

### Open checks — cheap, and each needs an editor

Filed 2026-08-14 by the documentation audit, which could not run them: the unreal-mcp toolset does
not register unless the editor was open when Claude Code started. **None blocks Block.** Each is a
question the docs currently answer by assertion.

- **Four documented live values disagree with their C++ defaults**, because a Blueprint CDO shadows
  them: `InputBufferSeconds` (0.20 documented, 0.1 in code), `StaminaRegenPauseSeconds` (0.5 / 1.0),
  `DodgeSeconds` (0.4 / 0.5), `AimAssistMarginCm` (100 / 200). The asset is expected to be right in
  all four. **Read them off `BP_PlayerCharacter` and `GA_Attack` and confirm** — the numbers this
  file and `Docs/Combat-Decisions.md` quote all descend from prior sessions' readings.
- **`CoilTurnRateDegrees` is 300 in code and recorded as 600** in the 2026-08-12 Lunge entry, and
  `git log -S` shows it has never been 600. Either it shipped at half the intended value or the
  Blueprint overrides it. One CDO read settles which.
- **`AM_Attack`'s package references both the `_IP` and `_RM` forms** of `Attack4_Stage1_Complete`.
  The `_IP` one is required — a montage carrying root motion produces *no* lunge — and the ungated
  warning would have fired by now if it were wrong, so this is confirmation rather than suspicion.
  Confirm the segment, and drop the stale reference if that is all it is.
- **The total attack overhead has never been measured**, and the trap that asked for it wrongly said
  a log line had to be built first. `ABILITY END … elapsed=` already prints it. One held attack per
  tier with `TD.DebugCombatTiming` on; heavy and charged are the tiers to watch.
- **The wedges' vertical band (±70) has never excluded anybody.** It needs a height difference, and
  `L_CombatTest` has a ramp. Worth one attack uphill and one down.

### Done

**Completed items are one line plus whatever they left behind that can still bite** — and only what
has no other home. What was built is in the code and in git; reasoning is in
`Docs/Combat-Decisions.md`; **latent defects are in its traps section and are deliberately not
repeated here**, because a second copy is what nobody reviews.

- **Attack Ladder** — Light → Heavy → Charged Heavy. **Done 2026-08-09.** Offense still lacks the
  light string, knockdown and block-safety.
- **Dodge** — the dodge and the stamina economy that shipped with it. **Done 2026-08-10**, V3 clips
  2026-08-11, displacement authored 2026-08-13. All eight directions travel `DodgeTargetDistanceCm`
  through the same root motion source as the attack lunge, with a yaw offset taken from the
  direction enum's order. Nothing is left to calibrate.
   - **The airborne dodge anomaly was closed by removal, not diagnosis.** Air control was killed by
     experiment and the surviving hypothesis was never confirmed. **The same ground→air transition
     exists for attack lunges and nobody has looked**; `RelativeRotation.Yaw = -90` on the mesh is
     the untried lead.
- **Sword & Shield** — camera-relative facing, the props, the locomotion set. **Done 2026-08-11**,
  and the stance moved V1 → V3 the same day: V1 reads as permanently guarding and has no `Hit` or
  `Death` clips, so the no-mixing rule that chose it was never achievable. **V1 is retained for the
  held guard**, which it is decisively better at.
   - **Known art seam, not a bug:** adjacent directions disagree about the guard pose, so the shield
     snaps ~135° blending between them. Inherent to the source clips, and the fix — an upper-body
     layered blend over one guard pose — is **what Block will probably want anyway**, so it was
     deliberately not built twice.
- **Death** — minimally. **Done 2026-08-11.** `State.Dead` is refused by the shared ability base, so
  a new ability cannot be authored without it. Respawn, whether death routes through knockdown, and
  whether the dummy should die at all are **Stun's**.
- **Dodge Distance** — **Done 2026-08-11** and **entirely superseded**, first by the V3 swap and
  then by authored displacement. See **Dodge**; nothing from this item is live.
- **Attack Swap** — the sword-and-shield light. **Done 2026-08-12**, regression pass passed.
  `AM_Attack` plays V3 `Attack4_Stage1_Complete` with the notify at 0.3000 for 0.1500, so **the
  release plays at rate 1.000** — the strike's damaging frames run at the speed they were animated,
  the only setting where animation and mechanic do not disagree.
   - **An attack's damaging volume is authored, not traced off the weapon.** `FTDAttackHitbox` is a
     wedge in the attacker's frame and `MaxReachCm` is the attack's range. The general form is why:
     **anything derived from the art inherits the art's accidents silently.**
   - **Facing freezes from commit to the end of the ability, recovery included**, instantly in both
     directions — an actor-frame volume needs a stable actor frame, and steering stays free through
     windup so the whole cancellable portion is steerable. **`RecoverySeconds` therefore sets
     commitment length as well as punish length.** Whoever takes facing away restores it in
     `EndAbility`, where every exit path converges, never on the montage delegates.
   - **Facing runs at three rates, split by whether a number may be tuned by feel.**
     `TurnRateDegrees` is **derived, not chosen** — 180° ÷ the light's `HoldUntilSeconds` — and must
     move when that does. The other two are free, because the aim guarantee is discharged before
     either applies: `IdleTurnRateDegrees` when `IsIdle()`, and `CoilTurnRateDegrees` while an
     attack coils, which only heavy and charged reach. `BP_PlayerCharacter`'s CDO is authoritative.
   - **`bAllowPhysicsRotationDuringAnimRootMotion` is `true`**, against a UE default of off, and is
     the **only** rotation path — disabling it freezes facing everywhere rather than just at rest.
     Anything wanting a committed direction says so via `SetAbilityFacingLocked`. **Never re-disable
     it to fix one ability.**
   - **Combatants ignore `ECC_Camera`**; level geometry still blocks it. **The dummy shares
     `GA_Attack`**, so offense parity is the default — parity is partial by design, offense only.
- **Input Buffer** — **Done 2026-08-11**, verified in play and tuned. Single slot, last press wins;
  the window is grace on *taps*, so a held button never expires — which is what makes a buffered
  heavy or charged reachable at all. The airborne dodge refusal deliberately does **not** buffer.
- **Lunge + Recovery** — authored displacement, and the punish window it is tuned against. **Both
  done 2026-08-12**, play-verified. Recovery is `RecoverySeconds` per branch, honoured within 8 ms.
  Lunge is two authored distances — a shared base from the press to the light's boundary, and a
  per-branch one from commit to the end of release — measured within 2.5% at every tier.
   - **The base lunge follows facing, and that is load-bearing rather than cosmetic.** Rotation
     during that window *is* the aim guarantee, so a world-fixed lunge cannot be made safe by
     freezing rotation. Built on `FTDRootMotionSource_FacingForce`, a first-class `FRootMotionSource`
     subclass — predicted and replicated like the stock ones, explicitly not hand-rolled movement.
   - **Attacks became grounded-only and movement-locked**, which was never written down before. See
     the Offense section.
- **Target Lock** — attacks reach the target you aimed at. **Done 2026-08-13**, both halves
  play-verified, finished 2026-08-14. The governing rule: it **may correct where you are pointed,
  never whether you were in range**, so it cannot rescue a spacing miss and whiff punish is
  untouched. **It aims the *lunge*, not the swing** — the damage wedge already carries ±36–50° of
  tolerance while travel is a line, so a 25° error puts you 84 cm to the side. The user's framing:
  *a margin of error for aiming the lunge, not for aiming the mouse.*
   - **The gate is geometric and asked every movement tick**, contributing nothing while a body sits
     within `LungeStandoffCm` ahead, so it can only ever *subtract* travel and is not homing. **It
     shipped once as a pre-computed shorter distance and that was wrong** — pre-shortening bakes in
     a prediction, so a retreating target became unreachable, which is worse than no system at all.
   - **A hit against a viable target *stops* the lunge outright**, which a pause cannot do: killing
     someone removes their capsule, the gate opens on the corpse, and the attacker slides through.
     **A dodged attack still runs on** — that is what stops a successful evade paying the attacker
     in spacing.
   - **The aim wedge is the contract.** Its **arc** is the knob and means *how wrong your aim may
     be*; **reach is derived** from travel plus damage reach plus `AimAssistMarginCm`, the one
     authored number. **The wedge must reach past hit range — that gap is the design, not slack**,
     or lock-on becomes a rangefinder and assist starts answering whether you were in range.
     `bEnabled` turns a branch off; an arc of 0 does not.
   - **Homing follows the ladder, runs through the base lunge, and stops at commit.** Widening only
     happens at an escalation, the same instant the coil fires, so it never leaks a tier the
     defender has not been told about — and stopping at commit is where the reaction window opens,
     which is what leaves whiff punish intact.
   - **Evaluated in the camera's frame**, not the body's: assist aids the attacker's input while
     damage stays actor-framed, because defenders must be able to trust what the body does. Load
     bearing rather than tidy — **the player's authority is selection, not facing**, and two rules
     writing one yaw would deadlock. **No hysteresis yet**, so expect selection flicker as the wedge
     grows.
   - **Two ideas recorded, not decided:** dodge intangibility, worth trying only once the clamp has
     been felt; and the **lunge strength curves**, which exist wired to nothing and are parked
     against the structure audit. Measuring a curve means moving the dummy past ~800 cm first, since
     the gate truncates both lunges at the placed spacing.

Three things that were never items are also done: the netcode groundwork **Slices A and B**, the
**hover bug**, and the **facing pass**. The hover left one rule worth restating because it was
invisible for weeks — **the mesh's relative Z and `InitCapsuleSize`'s half-height must change
together**, which is why they sit adjacent in `ATheDreamCharacter`'s constructor. **Foot IK now runs
during montages**, kept because ramp attacks adapt correctly rather than to prop anything up.

Open and **not** a defect: the character can stand on the ramp's near-vertical edge face, so walking
off it descends that face before free fall. Whether `MaxWalkableFloorAngle` should permit it has
never been examined; the value has not been read.

Every item gets done; only the sequence was ever in question. The first three were ordered by
dependency, the rest is judgement and may be revisited.

### Remaining

In execution order, and all sequential. **Target Lock shipped 2026-08-13**, and Lunge + Recovery on 2026-08-12; see Done.

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

**Lunge strength curves are parked against this same trigger** (2026-08-13, the user's call), and are
listed here rather than in the sequence for that reason. They are *not* structural work — the reason
they share the trigger is that they are last-10% feel tuning, which is the same thing verified-good
is a precondition for. Assets exist and are wired to nothing; `Docs/Combat-Decisions.md`'s tuning
map carries the warning that a curve's mean must be 1.0 or it silently scales the authored distance.

**The trigger is the combat model being verified good in play.** Deferring the audit is right —
anything reorganised before the systems settle gets reorganised again — but *last* is not a
schedule. An audit parked at the end of a list that keeps growing is one that never runs, and this
list has grown every session it has existed. Verified-good is the prototype's actual finish line
and the first moment reorganising stops being wasted work. **If that is true and this has not run,
it is next**, whatever else has accumulated by then.