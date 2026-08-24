# The Dream – Combat Prototype

## Project Intent
A high-precision **PvP** combat prototype in Unreal, prioritising spacing, reactability windows, stamina as a real resource, and clear punishes.

**PvP is the destination.** A prototype that cannot be played against another person cannot answer the question it exists to ask.

Feel goals, in priority order:
- Precise spacing and whiff punish
- Two types of offense: unreactable but risky (lights and heavies) against reactable but rewarding
  (charged)
- High-agency defense (block, dodge, parry) with meaningful costs
- Fair, readable knockdown / oki
- Strong melee identity first; ranged and hybrid later

## Building for the network

**Server-authoritative with client prediction** — the model GAS is designed around. Binding on all
new work:

- **New state is a replicated property or an attribute, never a loose gameplay tag** — loose tags
  do not replicate. Follow `bDead` / `bExhausted` on `ATDCombatCharacter`: the server decides, the
  bool replicates, `OnRep` applies the tag locally.
- **Authority-sensitive work is explicitly gated.** Damage, attribute writes and hit detection
  belong to the server. What only the local machine can know — input, buffered presses,
  camera-relative facing — deliberately does not.
- **Latency is a design input.** The tightest window is the light's release. When choosing a
  timing, say what it looks like with a round trip in it.

**Netcode difficulty is never a reason to compromise combat feel.** Status is in Netcode's brief.

## Combat Vocabulary

- **Windup** — everything before the attack can deal damage.
- **Release** — the period it deals damage, marked on a montage by the `Release Window` notify
  state (`UAnimNotifyState_MeleeWindow`).
- **Recovery** — an input restriction you inflicted on yourself; an attack's third phase is one.
- **Lockout** — an input restriction someone else inflicted on you. `TDGameplayTags.h` says which
  states are which.
- **Coil** — *not* a fourth phase: the sub-state of windup slowed while waiting for the commit
  checkpoint, as visual feedback. Its values are named `Coil*` rather than after a phase.
- **Initiative** — frame advantage; one of the two ledgers an exchange settles in.
- **Flinch** — hitstun interrupting offense.

"Release" also names the button coming up (GAS's `InputReleased`). Bare "release" means the
damaging phase; the button edge is *input release*.

**Attack and swing mean the same thing.** A chained light is **three attacks**, not one attack in
three parts — `FTDStringSwing` and the trace's `swing=N` are that index. A **string** is the chain
they form.

## Technical Preferences
- **C++** for core systems, characters, AttributeSets, ability base classes and non-trivial logic;
  Gameplay Abilities, Effects and notify logic may live in Blueprint where that speeds iteration.
- **GAS is preferred** for combat — abilities, states and attributes alike.
- **Every important tuning value is exposed via UPROPERTY** or lives in data (curves, data assets),
  so the designer adjusts without recompiling.

## Implementation Conventions
- **`TheDream` is the codename, not the title.** It is baked into the C++ module
  (`/Script/TheDream.*`), the content root `/Game/TheDream/`, and the `TD` class prefix, all
  permanent — renaming a module breaks every Blueprint's stored class path and costs permanent
  `ActiveClassRedirects` cruft.
- **The shipping title lives only in `ProjectName` (`Config/DefaultGame.ini`) and localized
  strings.** That makes retitling a one-line change rather than a migration.
- **Everything authored here lives under `/Game/TheDream/`**; anything at `/Game/` root is Epic
  template or third-party. Combat content is under `/Game/TheDream/Combat/`, subfoldered by kind.
- C++ mirrors this: `Source/TheDream/Core/` and `Source/TheDream/Combat/`, subfoldered the same
  way. Includes are relative to the module root, e.g.
  `#include "Combat/Attributes/TDAttributeSet.h"`.

## Project Documentation

Standing files carry knowledge the code cannot. **Each has a trigger** — which is why they are not
in this file, loaded in full every session. Keep them true in the same commit that makes them
wrong.

- **`Docs/Combat-Spec.md`** — timings, costs, windows, volumes, state transitions. *Trigger: about
  to change how combat behaves.*
- **`Docs/Working-In-Unreal.md`** — driving the editor and its MCP toolset without losing work.
  Nearly everything in it **fails silently**, so it only helps if already in your head. *Trigger:
  before planning or executing work that touches the engine.*
- **`Docs/Debug-Instruments.md`** — trace tags, cvars, the fixtures and the configurations that
  silently invalidate them, the scenario matrix, the verification checklist. *Trigger: about to
  measure something in combat.*
- **`Docs/Combat-Decisions.md`** — dated decision log, plus working sections: **known traps**, the
  **tuning map** (which knob moves, which obvious one is wrong, which values are derived), **slice
  briefs**, the **symbol index**, and bridge tables for anything superseded or renamed. Append an
  entry whenever a gameplay choice is made a future reader could second-guess; **never rewrite an
  entry — supersede it.** *Trigger: making a gameplay choice, or picking up a slice.*
- **`Docs/Animation-Library.md`** — where animations come from, the naming convention, what the
  library does *not* contain, how to migrate without dragging a duplicate skeleton behind it.
  *Trigger: before asking for or importing any animation.*
- **`Docs/Anim-Pipeline.md`** — authoring a clip the library does not have: the Cascadeur route, the
  measured tooling surface, the round trip's conditional roll fix, and the root-motion constraint
  that decides whether a clip can serve an authored distance at all. *Trigger: before authoring a
  clip, or moving one between Unreal and Cascadeur.*
- **`Docs/Closing-Down.md`** — ending a session, including the audit that keeps this file honest.
  *Trigger: the user says to wind down, and nothing else — whether a session concludes is theirs to
  decide, never yours to infer.*

**Durable knowledge belongs in these files, not in per-machine memory.** Memory keeps only what is
session- or machine-scoped, and *points* at the repo rather than restating it.

**One fact, one home; name the authority rather than restating the value.** A second copy is not
reinforcement — it is the one nobody reviews.

**`Tools/DocsCheck/docs-check.sh` is these files' integrity check.** Run it after any edit that
moves text between docs; closedown runs it regardless.

**Name the asset, not the C++ class — a correctness rule, not a style one.** A Blueprint CDO
override shadows a C++ default silently, so a class is authoritative only until someone touches a
details panel. Write *"`BP_PlayerCharacter`'s CDO is authoritative, defaults in
`ATDCombatCharacter`"*.

**Deliberately not kept: per-system design docs.** A doc describing a system drifts, then gets
trusted over the code.

**Comments carry WHAT, and HOW where the mechanism is not plain from reading — never WHY.** No
dates, no attributions, no history; `Tools/CommentCheck/comment-check.sh` fails on those, and the
**symbol index** routes a symbol back to its reasoning. Move what a reader could not recover from
the code or a doc to `Docs/` first.

**Comment volume is ratcheted, not trusted.** The same script fails when a file outgrows its entry
in `Tools/CommentCheck/baseline.txt`, so **adding volume means raising that number in the same
commit and saying why** — a sanctioned edit, and the only one. `--baseline` is for a pass that
deliberately re-sets the standard.

## Communication

**Use as few words as you possibly can, without any form of signal degradation.** Every instance,
project-wide, whatever the context — code comments, decision entries, conversation. A useful table
earns its space; a second example does not. **Efficient, not expressive.**

**A transcription may lose detail; it may never add any.** Sharpening a source — into a term, a
category, a general rule — is a decision taken quietly and reads as more authoritative than what it
came from. Point at the source; if it says less, say less.

**Report what was found, not who found it.** Note a contribution in a clause if it matters to the
reasoning and move on; do not tally credit and do not apologise. A wrong claim still gets corrected
plainly.

## Working Rules

**Autonomy on the HOW. Interrupt on the WHAT or the WHY.** Once what to build and why is agreed,
run the how through to completion — do not hand steps back one at a time. If a genuine question
about *what* or *why* emerges mid-run, stop and raise it.

- **The test is reversibility.** A HOW you can undo alone; a WHAT needs the user to undo it.
  **Irreversibility converts a HOW into a WHAT** — deleting an asset looks like a how and is not.
- **The tell is composing a justification.** If a choice needs *defending* in a commit message, it
  was a WHAT.
- **Not every WHAT interrupts.** One that blocks the work does; one merely noticed goes in the
  report.
- **Permission prompts are not the mechanism.** A prompt only asks a HOW question, so it cannot
  gate a WHAT; do not add one to cover a rule.

**When something is vague, or two sources disagree, ask. Every single time.** Resolving an ambiguity
quietly takes a decision that was never yours — including when it looks obvious and turns out right.
**An implementation disagreeing with its spec is a question to ask, not a discrepancy to fix.** Ask
early and cheaply. Unprompted initiative is welcome for debug and tooling conveniences; never for
anything that changes how the game plays.

**The design runway lives outside the repo, deliberately.** The user maintains living design
documentation running ahead of the build and dispenses from it as each thing becomes relevant, so
**a question is the cue to commit a design direction and solidify it**. **Silence in the spec is not
a gap and not missing design** — it means we have not reached that thing yet. Do not invent to fill
it, and do not try to write the runway down.

**The loop: objective → read the traps → measure → plan → greenlight → execute → report.**
- Before measuring, grep `Docs/Combat-Decisions.md`'s known-traps section for the item in play and
  **say what it turned up**.
- Do not begin work that has not been described and agreed, however obvious — described in enough
  detail that executing it needs no further decisions.
- ***Execution after a greenlight is unattended*** — drive editor closes, rebuilds, asset writes and
  verification through to the end. If something mid-execution changes what should happen — **scope
  as much as direction** — that is a **new plan** needing its own greenlight.
- **Do not declare a task finished on your own.** Report what was built, what was **verified versus
  merely written**, the assets touched and key values set, and **what was done beyond what was
  agreed, or that nothing was** — then stop. This is what makes unattended execution safe.

**Any package planning a new combat capability includes one of two things:** the scenarios it will
add to `Tools/RegressionCheck/regression-check.sh` **in the same package**, or a **dated trap**
naming what is now untested. **It binds at plan time.** A loop that lags the combat surface still
prints green.

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

**When a doc describes something that exists, read the thing.** The same failure wearing a positive
claim: a document is accurate about the moment it was written, and describes a limit that has since
lifted, a construction still in progress, or a default that was never checked. Prefer the artefact —
the asset, the registry, the running editor — and treat its description as a pointer to it.

**Do not delete lines you did not write without asking**: most are scar tissue from something that
went wrong once.

**At startup, check that the previous session wound down, and say so if it did not** — a dirty
working tree the handoff does not account for, or `Saved/Autosaves/PackageRestoreData.json`
**modified before the running editor started**. A live editor populates that file by autosaving, so
it indicts only a session whose editor is gone.

**MCP tools register only if the editor was open when Claude Code started.** Opening it later does
not fix that session; restarting Claude Code does.

**Commit freely; the push waits for the user to call the work done.** A local commit is a HOW, and
the message is where reasoning gets recorded. Commit in coherent, verified units as work lands. **A
number in a commit message is a measurement, not a projection** — count first, then compose.
Pending *tuning* questions do not block a push; pending *correctness* verification does.

**Every commit you author gets the `Co-Authored-By` trailer** — present only sometimes, its absence
turns ambiguous, so a commit you did not author says so in its message instead. Never via hook: a
hook cannot tell who wrote a change.

## Current Focus

Execution order, the only line that changes when the order does:

> **~~Attack Ladder~~ → ~~Dodge~~ → ~~Sword & Shield~~ → ~~Input Buffer~~ → ~~Death~~ → ~~Dodge Distance~~ → ~~Attack Swap~~ → ~~[hover bug]~~ → ~~[facing pass]~~ → ~~Recovery~~ + ~~Lunge~~ → ~~Target Lock~~ → ~~Block~~ → ~~Light String~~ → ~~Parry~~ → ~~Knockdown~~ → Polish → Death-full → Settings → Netcode → Tuning Rig → Interplay**

**Pick up at Polish**, which has no plan yet — its brief is the slice-briefs section of
`Docs/Combat-Decisions.md`, and it is the fullest one on the roster. It carries the bespoke windup
pass, the parried attacker's recoil, all of Knockdown's presentation, and the three items the
2026-08-24 legibility glance routed to it.

**The verification bar every slice now ships against** *(2026-08-24, the dated entry)*: verified
functionality, with animations legible enough to tell which mechanic is firing. Visual refinement
defers to Polish, feel refinement to Tuning-Rig. **Polish is where the first half of that debt comes
due**, so expect it to be large.

**The animation pipeline is `Docs/Anim-Pipeline.md`**: authored in Cascadeur, transferred by the
scripts in `Tools/AnimPipeline/`. `AS_GetUpAttack` is its first output and the project's first
authored clip; `Docs/Animation-Library.md` carries what that means for the library correspondence.

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

**Graduation has a bar: the rule must be general.** Something that only makes sense as one slice's
history goes to the decision log instead — otherwise the spec becomes the new dumping ground.
