# The Dream – Combat Prototype

## Project Intent
Build a high-precision **PvP** combat prototype in Unreal Engine that prioritizes spacing, reactability windows, stamina as a real resource, and clear punish opportunities.

**PvP is the destination, not a later phase** — every feel goal below is about what two players do to each other, and a prototype that cannot be played against another person does not answer the question it exists to ask.

Feel goals (in priority order):
- Precise spacing and whiff punish
- Unreactable-but-risky light offense vs reactable-but-rewarding **charged** options. Light and
  heavy form a **fast layer**, read as *"they pressed"*; only the charged holds the reactable
  pole, read as *"they're charging"*
- High-agency defense (block, dodge, parry) with meaningful costs
- Fair, readable knockdown / oki
- Strong melee identity first; ranged and hybrid come later

## Current Prototype Scope
**In scope** is the execution order below. **Explicitly out of scope:**
- Multiple weapons / weapon swapping
- Ranged and Hybrid archetypes
- Armor classes
- Abilities / specials
- Full frame-data tuning pass (placeholder numbers are fine)
- **Multiplayer session UX** — lobbies, matchmaking, reconnect handling. *Running* multiplayer is
  in scope; **Netcode** holds a roster position ahead of Interplay.

## Building for the network

The model is **server-authoritative with client prediction** — the one GAS is designed around.
Three rules bind all new work:

- **New state is a replicated property or an attribute, never a loose gameplay tag** — loose tags
  do not replicate. Follow `bDead` / `bExhausted` on `ATDCombatCharacter`: the server decides, the
  bool replicates, `OnRep` applies the tag locally. **Decide on the server, apply everywhere.**
- **Authority-sensitive work is explicitly gated.** Damage, attribute writes and hit detection
  belong to the server. State only the local machine can know — input, buffered presses,
  camera-relative facing — deliberately does not.
- **Latency comes out of the reactability budget, so it is a design input.** The tightest window in
  the game is the **light's 200 ms**. When a timing is chosen, say what it looks like with a round
  trip in it.

**Netcode difficulty is never a reason to compromise combat feel.** Status and what is outstanding
are in Netcode's brief.

## Combat Vocabulary

Used consistently in code, comments and discussion. **The spec — timings, costs, windows, volumes,
state transitions — is `Docs/Combat-Spec.md`**, with one exception kept here: the ladder's two
numbers per tier, needed to read a commit message or a trace line in any session.

| | Release before | Hitbox live |
|---|---|---|
| Light | 150 ms | **200 ms** |
| Heavy | 300 ms | **350 ms** |
| Charged Heavy | (held past 300 ms) | **750 ms** |

- **Windup** — everything before the attack can deal damage.
- **Release** — the period during which the attack deals damage. Marked on a montage by the
  `Release Window` notify state (`UAnimNotifyState_MeleeWindow`).
- **Recovery** — a tail you inflicted on yourself, during which you owe time. For an attack, the
  third phase: from the end of the damaging phase to the end of the attack. A whiffed parry's is
  the same idea off an attack. Say *attack recovery* only where context leaves it open.
- **Lockout** — a tail someone else inflicted on you: blockstun, hitstun, a guard break. **The axis
  is who caused it, not what it forbids** — a recovery can refuse more than a lockout does. Whether
  the inflicter was the attacker or the defender is immaterial.
- **Coil** — *not* a fourth phase: the sub-state of windup slowed while waiting for the commit
  checkpoint, existing as visual feedback. Its tuning values are named `Coil*` rather than after a
  phase.
- **Initiative** — frame advantage. One of the **two ledgers** an exchange settles in, the other
  being stamina; an attack that is plus on block takes stamina *and* keeps initiative.
- **Flinch** — hitstun interrupting offense. Distinct from the **challenge**, a raw counter thrown
  out of blockstun; the two race, and the blockstun derivation decides by how much.

Note that "release" also names the button coming up, via GAS's `InputReleased`. Bare "release"
always means the damaging phase; the button edge is always written as *input release*.

**Attack and swing mean the same thing.** A chained light is **three attacks**, not one attack in
three parts — `FTDStringSwing` and the trace's `swing=N` are that index. A **string** is the chain
they form. A **burst** is the debug fixture's firing cycle: a string when
`DebugAutoAttackStringTaps` > 1, a single attack at its default 1.

## Technical Preferences
- **C++** for core systems, characters, AttributeSets, ability base classes, and any non-trivial
  logic; Gameplay Abilities, Gameplay Effects and notify logic can live in Blueprint where that
  makes iteration faster.
- **GAS is preferred** for attacks, block, dodge, parry, stamina, hitstun, blockstun, and
  knockdown.
- **Every important tuning value — timings, costs, magnitudes, windows — is exposed to Blueprint
  via UPROPERTY** or lives in data (curves, data assets), so the designer adjusts without
  recompiling.

## Implementation Conventions
- **`TheDream` is the project codename, not the title.** It appears in exactly three places: the
  C++ module (`/Script/TheDream.*`), the content root `/Game/TheDream/`, and the `TD` class prefix.
  All three are permanent and arbitrary — renaming a module breaks every Blueprint's stored class
  path, paid for with `ActiveClassRedirects` cruft that never goes away.
- **The shipping title lives only in `ProjectName` (`Config/DefaultGame.ini`) and localized
  strings** — never in code, asset names, folder paths, or gameplay tags. That is what makes
  retitling a one-line change rather than a migration.
- **Ownership rule:** everything authored for this project lives under `/Game/TheDream/`; anything
  at `/Game/` root is Epic template or third-party content. Combat content lives under
  `/Game/TheDream/Combat/` (`Abilities/`, `Effects/`, `Animations/`, `Input/`, `Characters/`,
  `Data/`).
- C++ mirrors this: `Source/TheDream/Core/` (game mode, player controller, base character) and
  `Source/TheDream/Combat/` (`Abilities/`, `Attributes/`, `Tasks/`, `Notifies/`). Includes are
  written relative to the module root, e.g. `#include "Combat/Attributes/TDAttributeSet.h"`.
- Every new system should be playable in PIE with a debug enemy or training dummy as soon as
  possible.

## Project Documentation

Six standing files carry knowledge the code cannot. **Each has a trigger** — that is why their
contents are not in this file, which is loaded in full every session. Read them before working in
their area; keep them true in the same commit that makes them wrong.

- **`Docs/Combat-Spec.md`** — the combat spec: timings, costs, windows, volumes, state transitions.
  *Trigger: about to change how combat behaves.*
- **`Docs/Working-In-Unreal.md`** — driving the editor and its MCP toolset without losing work.
  Nearly everything in it **fails silently**, so it only helps if it is already in your head.
  *Trigger: read front to back at the start of every session.*
- **`Docs/Debug-Instruments.md`** — this project's instrumentation: trace tags, cvars, the fixtures
  and the configurations that silently invalidate them, the scenario matrix, the verification
  checklist. *Trigger: about to measure something in combat.*
- **`Docs/Combat-Decisions.md`** — dated log of combat decisions, plus working sections: **known
  traps** (filed against the slice that trips them), the **tuning map** (which knob moves, which
  obvious one is wrong, which values are derived rather than free), **slice briefs**, the **symbol
  index**, and bridge tables for anything superseded or renamed. Append an entry whenever a
  gameplay choice is made a future reader could second-guess; **never rewrite an entry — supersede
  it.** *Trigger: making a gameplay choice, or picking up a slice.*
- **`Docs/Animation-Library.md`** — where animations come from, the naming convention, what the
  library does *not* contain, how to migrate without dragging a duplicate skeleton behind it.
  *Trigger: before asking for or importing any animation.*
- **`Docs/Closing-Down.md`** — the procedure for ending a session, including the audit that keeps
  this file honest. *Trigger: the user says to wind down, and nothing else — whether a session
  concludes is theirs to decide, never yours to infer.*

**Durable knowledge belongs in these files, not in an assistant's per-machine memory.** Memory
keeps only what is session- or machine-scoped, and *points* at the repo rather than restating it.

**One fact, one home; everywhere else points at it** — between two items in this file as much as
between a doc and a code comment. **Summaries and descriptions are where this breaks**: they
restate values nobody thinks to update, and a second copy is not reinforcement, it is something
nobody reviews. **Prefer naming the authority over restating the value.**

**`Tools/DocsCheck/docs-check.sh` is these files' integrity check** — truncated tails, orphaned
table rows, dead cross-references, a stale symbol index — each invariant naming the failure shape
it catches. Run it after any edit that moves text between docs; closedown runs it regardless.

**Name the asset, not the C++ class — a correctness rule, not a style one.** A Blueprint CDO
override shadows a C++ default silently, so a class is the authority only until someone touches a
details panel. Write *"`BP_PlayerCharacter`'s CDO is authoritative, defaults in
`ATDCombatCharacter`"*, which names both and says which wins.

**Deliberately not kept: per-system design docs.** A doc describing a system drifts and then gets
trusted over the code.

**Comments carry WHAT, and HOW where the mechanism is not plain from reading — never WHY.** No
dates, no attributions, no history; `Tools/CommentCheck/comment-check.sh` fails on those, and the
**symbol index** routes a symbol back to its reasoning. **The test is recoverability**: cut what a
reader could recover from the code or from a doc by grep; move what they could not to `Docs/` first.

## Working Rules

**Autonomy on the HOW. Interrupt on the WHAT or the WHY.** Once what to build and why is agreed,
running the how through to completion is **preferred** — do not hand steps back one at a time. If a
genuine question about *what* or *why* emerges mid-run, stop and raise it.

- **The test is reversibility.** A HOW you can undo alone; a WHAT needs the user to undo it.
  **Irreversibility converts a HOW into a WHAT** — deleting an asset looks like a how and is not.
- **The tell, in the moment, is composing a justification.** If a choice needs *defending* in a
  commit message, it was a WHAT.
- **Not every WHAT interrupts.** One that blocks the work does; one merely noticed goes in the
  report.
- **Permission prompts are not the mechanism.** A prompt only ever asks a HOW question, so it
  cannot gate a WHAT; do not add a prompt to cover a rule.

**When something is vague, or two sources disagree, ask. Every single time.** This project is
**maximally designer-authored at every step except the HOW**, so resolving an ambiguity quietly
takes a decision that was never yours — including when it looks obvious and turns out right. **An
implementation disagreeing with its spec is a question to ask, not a discrepancy to fix.** A design
question asked in service of a HOW is welcome, not an interruption — **a question is the cue to
commit a design direction and solidify it**, so ask early and ask cheaply. If a gameplay question
has more than one defensible answer, raise it rather than picking quietly, and record the choice in
`Docs/Combat-Decisions.md`. Unprompted initiative is welcome for debug and tooling conveniences;
never for anything that changes how the game plays.

**The design runway lives outside the repo, deliberately.** The user maintains living design
documentation running well ahead of the build and dispenses from it as each thing becomes relevant.
**Silence in the spec is not a gap and not missing design** — it means we have not reached that
thing yet. Do not invent to fill it, and do not try to write the runway down.

**The loop: objective → read the traps → measure → plan → greenlight → execute → report.**
- **Reading the traps is a step, not a hope.** Before measuring, grep `Docs/Combat-Decisions.md`'s
  known-traps section for the item in play and say what it turned up.
- ***Measuring comes before planning*** — a plan proposed before measuring is the first hypothesis
  wearing a schedule.
- ***The pause is real.*** Do not begin work that has not been described and agreed, however
  obvious. A plan for an attack or defensive move includes the input binding, montage/notify
  windows, stamina cost, and at least a basic success/failure outcome.
- ***Execution after a greenlight is unattended*** — drive the editor closes, rebuilds, asset
  writes and verification through to the end. If something mid-execution changes what should
  happen — **scope as much as direction** — that is a **new plan** needing its own greenlight.
- **Do not declare a task finished on your own.** Report what was built, what was **verified versus
  merely written**, the assets touched and key values set, and **what was done beyond what was
  agreed, or that nothing was** — then stop. This is what makes unattended execution safe.

**Any package planning a new combat capability includes one of two things, and there is no third:**
the scenarios it will add to `Tools/RegressionCheck/regression-check.sh` **in the same package**, or
a **dated trap** naming what is now untested. **Doing neither is a process violation, and it binds
at plan time.** A loop that lags the combat surface still prints green.

**When play and rationale disagree, play wins.** A designed distinction that does not survive
contact with feel gets dropped, and the entry recording it superseded. Do not treat a persuasive
past entry as a commitment.

**An animation plays in full across the mechanical duration it belongs to.** Fit the clip to the
duration, never the reverse — the fix for a bad-feeling number is to change the number.

**Instrument before theorising.** When behaviour is wrong and the cause is not obvious, enable a
trace before proposing an explanation, and prefer an experiment that *manipulates* the suspected
cause over one that only observes it.

**Never claim something does not exist based on a filtered or derived view.** A search that finds
nothing proves only that your filter did not match. Search the authoritative source unfiltered, try
synonyms and known misspellings, **quote the command you ran**, and **date every absence claim with
what you searched** — absence claims rot faster than any other kind.

**Do not delete lines you did not write without asking**: most are scar tissue from something that
went wrong once.

**At startup, check that the previous session wound down, and say so if it did not** — a dirty
working tree, or stranded packages in `Saved/Autosaves/PackageRestoreData.json`.

**Never delete assets or change project settings without explicit approval.**

**Commit freely; the push waits for the user to call the work done.** A local commit is a HOW —
undoable, and the message is where reasoning gets recorded. Commit in coherent, verified units as
work lands. **A number in a commit message is a measurement, not a projection** — count first,
then compose. Pending *tuning* questions do not block a push; pending *correctness* verification
does.

**Every commit you author gets the `Co-Authored-By` trailer** — present only sometimes, its absence
turns ambiguous, so a commit you did not author says so in its message instead. Never via hook: a
hook cannot tell who wrote a change.

**Report what was found, not who found it.** Note a contribution in a clause if it matters to the
reasoning and move on; do not tally credit and do not apologise. A wrong claim still gets corrected
plainly.

## Current Focus

Execution order, the only line that changes when the order does:

> **~~Attack Ladder~~ → ~~Dodge~~ → ~~Sword & Shield~~ → ~~Input Buffer~~ → ~~Death~~ → ~~Dodge Distance~~ → ~~Attack Swap~~ → ~~[hover bug]~~ → ~~[facing pass]~~ → ~~Recovery~~ + ~~Lunge~~ → ~~Target Lock~~ → ~~Block~~ → ~~Light String~~ → ~~Parry~~ → Knockdown → Polish → Death-full → Settings → Netcode → Tuning Rig → Interplay**

**Pick up at Knockdown's sub-slice F, the get-up attack** — `Docs/Plan-Knockdown.md` is still live.
**A–E shipped 2026-08-20**, the state machine verified in play: grades, the 2.5 s total, the 0.5 s
rise, floor invincibility, the movement and facing locks, the parried attacker's lockout. **F is
blocked on `AM_GetUpAttack`** — no clip in the library suits it, and without a montage carrying a
Release Window notify the ability commits but its hitbox never opens. A placeholder is owed.

**Also owed**: `s4-360`'s first-burst exclusion, sub-slice I's spec rewrite, and the get-up options
— dodge, kip-up, block, every exhaustion refusal — built and never run, which is a filed trap.

**Structure Audit has no roster position and keeps a trigger**; its brief is in the same section.

### When a slice ships

**Route every consequence, then delete the entry.** The execution-order line above is the complete
roster — struck through is the whole record a shipped item needs here.

| Consequence | Goes to |
|---|---|
| A design rule that still governs play | `Docs/Combat-Spec.md` |
| A latent defect or unverified assumption | the **traps** section of `Docs/Combat-Decisions.md` |
| A value that is **derived** and must not be tuned freely | the **tuning map** there, as *"nothing, without re-deriving it"* |
| Which knob moves for a given complaint | the **tuning map** there |
| What the next slice inherits | its **brief**, in the slice-briefs section there |
| What a symbol *is, does or requires* | that symbol's **header comment** — and nothing else does |
| The argument behind any of it | its **dated entry**, where it already is |

**Graduation has a bar: the rule must be general.** Something that only makes sense as the history
of one slice is not a rule and goes to the decision log — otherwise the spec becomes the new
dumping ground and the problem has merely moved.
