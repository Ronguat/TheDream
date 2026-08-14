# Working in Unreal on this project

**Read this file front to back at the start of every session.** It is not a reference to consult
when something breaks; it is the set of things that must already be in your head before you touch
the editor, because most of what it describes **fails silently** — a write that returns true and
changes nothing, a build that never happened, a log that lies about absence.

It is kept short enough for that to be reasonable. **Anything that can be compressed to its rule
has been**, and the incidents behind them live in git history and `Docs/Combat-Decisions.md`.

**The budget is ~400 lines, and it is enforced when you add, not when you audit.** If a new line
would take this past about 420, compress something first — the person adding a line is the one who
knows what it replaces, and a periodic audit is too late by definition. This is stated as a number
because the vaguer version ("keep it short enough to read") let it drift **47 lines in a single day**
after being cut from 820 to ~400 on 2026-08-13, without anyone noticing. Step 3 of
`Docs/Closing-Down.md` is the backstop, not the mechanism.

**Growth that is genuinely this file's belongs here; growth that is the project's does not.** All 47
of those lines were documentation of *our own* debug instrumentation, which grows once per combat
feature forever. That is now `Docs/Debug-Instruments.md`. What is left grows only when Unreal or its
toolset surprises us, which is rare and worth the space.

**Confidence marks.** *(confirmed)* was observed directly. *(reported once)* comes from a single
incident and has not been reproduced — work around it, but re-test rather than treat as settled if
it blocks you. Never promote a mark without re-observing the behaviour.

---

## Before you start

**The editor must be open before Claude Code starts.** `unreal-mcp` is an HTTP server hosted by the
in-editor plugin (`127.0.0.1:8000`, see `.mcp.json`).

- **Editor absent at Claude Code startup** *(reported twice)* — the tools never register for that
  entire session, and opening the editor afterwards does not fix it. Restart Claude Code.
- **Editor closed and reopened mid-session** *(confirmed)* — fine, tools resume by themselves.

The distinction is **registration versus connection**: schemas are picked up once at session start,
the connection can drop and re-establish. So the assistant closing the editor for a rebuild is safe;
starting without one is not.

If asset writes are needed, confirm the tools respond before promising any.

---

## Driving the editor

*(confirmed 2026-08-11, full cycle tested)* The assistant opens and closes the editor itself.

| Step | How |
|---|---|
| Running? | `tasklist \| grep -i UnrealEditor.exe` |
| Open | `nohup "/c/Program Files (x86)/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "<abs path>/TheDream.uproject" >/dev/null 2>&1 &` |
| Save all dirty | `AssetTools.save_assets` with `asset_paths: []` |
| Close | `taskkill //F //IM UnrealEditor.exe` — exits in ~2 s |

**Save-all, then read `git status`, then kill.** Calling `save_assets` is not the check; *seeing the
files listed* is. **Two failure modes wear the same face:** a write that is saved but not yet live
needs a restart and is safe, and a write that was never saved is already gone. Both read back fine
from inside the editor. `git status` is the only thing that separates them and it must happen
*before* the kill. A CDO write was lost exactly this way.

There is **no graceful quit** — checked across the whole toolset registry. So closing is always a
forced kill, and **save-all covers assets only**: in-progress asset-editor state dies unasked.
`Saved/Autosaves/PackageRestoreData.json` reading `Packages: []` confirms nothing was stranded;
**`Packages` is the field that matters, not `RestoreEnabled`**, which is not a stable signal.

**Announce before closing, every time — but it is an announcement, not a request.** Saying it is
what makes it safe, because the user stops working in the editor on reading it. Announce and proceed
in the same turn. Opening needs no announcement.

**Process up is not ready.** The port can listen while the engine boots, and a call in that window
fails with `Unable to connect`. The only reliable signal is **an MCP call returning a result** —
poll `SceneTools.get_current_level` rather than sleeping a fixed interval. A blind wait is wrong in
both directions.

**Stop PIE before compiling a Blueprint or saving an asset.** While PIE runs, actor lookups return
the `UEDPIE_0_` world's actors — right for inspecting live state, wrong for authoring.

**A PIE transform is not a placed transform** *(confirmed 2026-08-13)*. It is where an actor *ended
up*: settled under gravity, and pushed if anything could push it. The dummy read `x=175.81, z=98.15`
in PIE against `x=200, z=100` placed, and the PIE figure was written up as a correction to
documentation that was right. **The tell is that `z` has also moved.** Re-read in the editor world
before writing any placement number down.

---

## Building C++

**State up front whether a change touches a header.** That single fact decides whether the editor
must close, and it is easy to lose track of mid-discussion.

**Live Coding patches exist only in the memory of the process that compiled them.** A new `UCLASS`
patched that way vanishes on restart and every Blueprint parented to it fails to load — surfacing as
`Failed to load Class /Script/...`, which looks like asset corruption, sessions later. **Anything
touching reflection — new classes, new or renamed `UPROPERTY`s, new module dependencies — needs a
full editor-closed rebuild.** It has also crashed the editor once on a change it should have handled
*(reported once)*. Treat it as a convenience that can cost you the editor, not as the cheap path.

**Build from Bash, never PowerShell.** Every PowerShell tool call rewrites the user's console font
registry key — it is the tool invocation itself, not the build. The repair, if it happens *(the
user's preset is Consolas 14)*:

```bash
K="HKCU\Console\%SystemRoot%_System32_WindowsPowerShell_v1.0_powershell.exe"
reg add "$K" //v FaceName   //t REG_SZ    //d Consolas //f
reg add "$K" //v FontSize   //t REG_DWORD //d 917504   //f   # 0xE0000: height 14
reg add "$K" //v FontFamily //t REG_DWORD //d 54       //f   # TrueType
```

Note `//v` rather than `/v` — MSYS rewrites a leading single slash as a path.

**"Bash cannot do this" is nearly always wrong, and believing it is what breaks the rule.** There is
**no Python on PATH** *(machine fact, hit twice)*, but Git Bash ships `base64`, `reg`, `xxd`,
`certutil` and `curl`. Check for the binary before concluding otherwise.

`Build.bat` cannot be called from Git Bash — the space in `Program Files (x86)` survives every
quoting form. Call UnrealBuildTool directly, with the **bundled** dotnet (the system one tops out at
.NET 9; UBT needs 10). **The CWD matters** — UBT must run from `Engine/Source`:

```bash
cd "/c/Program Files (x86)/UE_5.8/Engine/Source" && \
"/c/Program Files (x86)/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "C:/Program Files (x86)/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  TheDreamEditor Win64 Development \
  -project="C:/Users/rross/Documents/Unreal Projects/TheDream/TheDream.uproject" -waitmutex
```

**Verify every build, without exception** — not that it reported success, but that the DLL is newer
than every source file. The automation makes this *more* important, not less: the close/build/reopen
cycle runs without a human in it, which removes the pause where a missing build used to be noticed.
A fix was once described, handed over for testing, and had never been compiled.

```bash
# Empty output means the DLL is newer than every source. This is the check.
find Source \( -name "*.cpp" -o -name "*.h" \) -newer Binaries/Win64/UnrealEditor-TheDream.dll -print
ls Binaries/Win64/UnrealEditor-TheDream.patch_* 2>/dev/null || echo "no patch files"
```

Use `-newer` rather than comparing timestamps by eye: `ls` and `find -printf` report in **different
timezones** here, and a time without a date makes yesterday's file look newer than a fresh build.
Sweep leftover `patch_*` files so the binary state is unambiguous.

Batch all the C++ for a slice while the editor is closed, do one rebuild, then open once and stay in.

---

## Writing assets through the toolset

**Read a property off the asset rather than guessing from a filename** *(confirmed 2026-08-11)*. A
naming-convention guess is a filter, and a filter that misses proves nothing. The call shape is
`{"instance": {"refPath": "..."}, "properties": [...]}` — calling any of these wrong returns the full
input schema, which is the fastest way to learn one.

**Verify against the artefact, not the return value, and prefer a check that does not go back
through the layer that wrote it.** These tools have reported success while changing nothing in
several distinct ways:

- **`set_properties` can return true and `get_properties` read the value back intact while the write
  accomplishes nothing** *(reported once, twice in a session)*. Round-trip verification is not
  sufficient.
- **Binary presence proves a write landed, not that anything derived from it was rebuilt**
  *(confirmed 2026-08-11)*. A BlendSpace's `SampleData` wrote, read back, grew the `.uasset` and
  displayed correctly on the grid — and produced no pose at all, because the derived interpolation
  grid never rebuilt. Suspect this for **any asset type with a build step**; the fix is a real edit
  in the editor, then save. Only play confirms those.
- **CDO writes are property-dependent, and reading the CDO cannot tell you** *(confirmed
  2026-08-13)*. Two writes to `GA_Attack`'s CDO seconds apart: a direct object reference did **not**
  reach the live ability instance, while object references inside a struct array did. Both read
  correctly off the CDO throughout.

**For a CDO, the artefact is the runtime instance** — not the CDO, not the file. Reach it during PIE
by reading `ActivatableAbilities` on the `AbilitySystemComponent`; each spec's `nonReplicatedInstances`
holds the live ability's `refPath`. The GAS inspector cannot do this (`GetGrantedAbilities` returns
names only). No mechanism is offered for why properties differ.

**A Blueprint CDO property set programmatically is generally not live in the current editor session**
*(confirmed 2026-08-10, reproduced deliberately on a second, older property)*. Return value,
read-back and file on disk were all green while the running game used the old value.

| Change made by | Takes effect after |
|---|---|
| Details panel | the next **PIE** restart |
| `set_properties` | the next **editor** restart — a PIE restart is not enough |

So prefer the details panel for anything a designer would touch anyway, and restart before trusting
a programmatic write. **The rule is about Blueprint CDOs and does not extend to plain assets**
*(confirmed 2026-08-12)* — an AnimSequence's `bEnableRootMotion` took effect immediately.

**Prove the instrument before trusting a null result.** When the evidence is *"I changed it and the
symptom did not move"*, the restart rule makes that ambiguous between a refuted hypothesis and a
write that never landed. Make one write whose effect is numerically measurable, confirm it, then
trust what follows. **Where a value drives behaviour, print the value** — that is what made
`DodgeSeconds` diagnosable where judging 0.2 s by feel would not have been.

**`reset_properties` resets to the property's *default*, not the inherited archetype value**
*(confirmed 2026-08-12)*. It wrote `(0,0,0)` over a component offset, further from correct than the
override it removed. There is no scriptable equivalent of the details panel's revert arrow — **set
the inherited value explicitly instead.**

**A C++ component default reaches nothing if a Blueprint or placed actor overrides it** *(confirmed
2026-08-12)*. A mesh offset needed **three** writes — two Blueprints and the placed actor — each
carrying a serialized copy. After any C++ default change to an inherited component, read the value
back off a live PIE actor.

**When configuring an asset type for the first time, diff it against a known-good asset of the same
type.** Comparing against stock `IMC_Default` is what exposed the input bug; ours looked fine alone.

### Confirmed traps

- **InputMappingContext** *(confirmed)* — UE 5.8 reads `defaultKeyMappings.mappings`. The top-level
  `mappings` array accepts writes and is never read.
- **GameplayEffect modifier attribute** *(reported once)* — setting `attributeName` + `attributeOwner`
  leaves the `FProperty` null, so the effect modifies nothing. Re-pick it in the details panel. The
  `attribute` field reads `/Script/TheDream.TDAttributeSet:Health` when correct, empty when broken.
- **A GameplayEffect's inline tag containers cannot be written** *(confirmed 2026-08-10)* —
  `inheritableOwnedTagsContainer` and `ongoingTagRequirements` accept writes and read back empty; UE
  5.8 moved this to `gEComponents`. **Adding a GEComponent is not scriptable.** Numeric properties on
  the same asset write fine, so a partially-configured effect is the likely outcome.
- **Object references need the full path** *(confirmed)* — `/Game/Path/Asset.Asset`. Errors, at least.
- **Array edits** *(confirmed 2026-08-10)* — changing an element and adding one in the same call
  fails and leaves a partial write. **Empty the container, then write it whole**, as two calls.
  Applies to `FGameplayTagContainer`, where the array is `gameplayTags`.
- **TMap keys** *(reported once)* — logs `added key ... not found in map` while being correct.
- **`save_assets` can reject a path that demonstrably exists** *(reported once)*. **Pass an empty
  list** — save everything dirty — and `git status` afterwards to see what was written.
- **`exists` false-negatives, and `duplicate` fails with a bare `false`** *(confirmed 2026-08-10)*.
  **Use `load_asset` as the existence check**; it returns a usable object or errors. Never conclude
  an asset is missing from `exists`. Duplication of a *plain* asset may need a human — though
  duplicating a `CurveFloat` worked 2026-08-13, so try it before asking.
- **`delete` is inconsistent about removing the `.uasset` from disk** *(confirmed both ways)*.
  Always check the directory afterwards; `git rm` only what is still there.
- **Saving a level while a CDO write is not yet live bakes the stale value into placed actors as
  per-instance overrides** *(confirmed 2026-08-14)*. This is how the trap below gets *created*, and
  the sequence is one anybody would follow: write the CDO, `save_assets`, restart. The save catches
  the placed actor still reading the old value and serializes it as an override, so the restart
  fixes the CDO and the instance stays wrong forever. Seen on a **brand-new** property, which is
  what makes it unmistakable — there was nothing to inherit from. **Read the placed actor after any
  CDO write, not just the CDO**, and expect to set it explicitly.
- **A placed actor can hold stale `EditDefaultsOnly` values that silently override its Blueprint**
  *(confirmed 2026-08-10)*. The placed dummy read `DefaultAbilities: []` against a populated CDO and
  was granted nothing. The signature is the instance showing **C++ class defaults** — it was placed
  before the Blueprint authored them. `reset_properties` fails on exactly those names while
  succeeding on `EditAnywhere` ones, which is the cheapest confirmation. **The fix is to delete and
  re-place the actor**; note its transform and label first.

---

## Not scriptable at all

Needs a human in the editor:

- Creating levels, AnimMontages, BlendSpaces and AnimBlueprints
- Placing or configuring AnimNotifies — a montage's `notifies` is not even readable, so notify
  placement can only be verified at runtime
- A montage's **`compositeSections`** — neither readable nor writable, and `sequenceLength` is
  read-only and does not recompute after a reflection write
- **`UCurveFloat`'s keys** *(confirmed 2026-08-13)* — `FloatCurve` is a bare `UPROPERTY()` the
  reflection layer cannot see. Creating the asset by duplication works, so the split is **script the
  asset, have a human author the keys.** A curve's mean therefore cannot be verified through the
  toolset; only measured travel confirms it.

**But creation is per-toolset, not a blanket limitation** *(confirmed 2026-08-12)*. `MaterialTools`
and `MaterialInstanceTools` create and build whole graphs end to end. Check the toolset that owns the
asset type before concluding a thing cannot be made.

**Renaming an AnimNotify class is expensive** — placed notifies serialize against the class path.

**`create_node` cannot target a nested state graph** *(confirmed 2026-08-14)*. It resolves the
Blueprint through the graph's outer, and a state graph's outer is an `AnimStateNode` — the error is
*"Cannot cast type 'AnimStateNode' to 'Blueprint'"*. `read_graph_dsl` also returns empty there.
**Placing a node inside a state is the one AnimBP job that needs a human**; everything around it is
scriptable, including exposing optional pins.

**An anim node's optional properties can be turned into pins by writing `ShowPinForProperties`**
*(confirmed 2026-08-14)* — flip `bShowPin` on the entry whose `bCanToggleVisibility` is true, then
`compile_blueprint`, then read the node back to see the new pin. That is how a `BlendSpacePlayer`'s
`BlendSpace` or a `SequencePlayer`'s `Sequence` becomes drivable. `set_pin_value` then swaps the
asset without touching the graph, which also makes it **the cheapest way to eyeball an animation
asset**: point the pin at it, PIE, `CaptureViewport`, point it back.

**A creation `type_id` is not the `type_id` a node reports.** `get_node_infos` returns shorthand
like `|GetGroundSpeed`; `create_node` wants `Variables|Default|GetGroundSpeed`. Guessing from the
former fails with *"does not exist"*, which reads exactly like a permissions problem and is not —
**use `find_node_types` and do not infer a create id from a read one.**

**AnimGraph *editing* is scriptable** *(confirmed 2026-08-11)*; only creation is not. `BlueprintTools`
has `list_graphs`, `find_nodes`, `get_node_infos`, `create_node`, `connect_pins`, `set_pin_value`,
`retarget_node_class`, `compile_blueprint` and `delete_node`. There is no disconnect function;
deleting a node breaks its links. `read_graph_dsl` returned empty for `ABP_Combat:AnimGraph` — use
`find_nodes` with `title: ""` plus `get_node_infos`, which reports pin connections both ways. Change
a node by a **partial** write to its `Node` struct; a full write clobbers pin-backed fields.
`describe_toolset` on it is too large to return — grep a saved dump.

---

## Measuring and diagnosing

**Before testing whether a symptom depends on X, test whether it depends on anything at all.** It is
a strictly cheaper question and partitions the search far more brutally than comparing two candidate
causes. The hover bug was chased for two sessions through skeletons, root motion and montages while
the dummy displayed it statically in the level viewport the whole time.

**So the level viewport is an instrument, and the cheapest one here.** A static defect — a bad
offset, a wrong attachment — is fully visible on a placed actor with no PIE and nothing running.

**A sufficient explanation is not the actual one.** Prefer an experiment that *manipulates* the
suspected cause over one that only observes it. When two hypotheses are killed by evidence, file the
anomaly rather than inventing a third.

**An assumed control is worse than no control.** A comparison case only disconfirms if the case was
actually *measured*. The hover hunt killed its first hypothesis with "the dodge has the same setting
and does not hover" — and nobody had ever checked whether the dodge hovers; a report that it looked
fine *during locomotion* had been silently converted into *the dodge is fine*. An assumed control
carries the authority of evidence while being a guess, and what it corrupts is not the conclusion
but the test used to reject one.

**What the user glosses over is often the decisive observation.** "The dummy hovers in the preview
too" reframed a two-session bug instantly, volunteered casually. When a bug resists, ask explicitly
what *else* shows the symptom.

**A single fixed test configuration is a filter.** An automated PIE run spawns both characters at
their placed transforms and nobody moves, so **"no damage landed" is not evidence about hit
detection** — a fixed spawn distance that used to connect need not still connect. Move something, or
turn on the debug draw and look.

**`StartPIE` takes a `startTransform`** that overrides the player spawn for that session only — the
way to measure around a pawn without dirtying the level.

**Measuring an actor's own movement requires nothing else touching it, and a capsule counts.** Two
42 cm radii touch at 84 cm of separation, so contamination begins long before two characters look
close. A travel figure measured against a blocked capsule was reported and had to be withdrawn.

**And do not measure one actor's travel against another actor's *assumed* position.** The general
form of the PIE-transform trap above, and the one that actually bites twice: two 2026-08-14 readings
computed an attacker's closing distance against the dummy's **placed** origin while the dummy was
being shoved across the floor by the very attacks being measured — the player finished one run at
`x=-110.75`. Both were reported as findings about the lunge and both had to be withdrawn. **A moving
reference frame reads as a movement fault in the thing being measured.** Re-read both transforms, or
measure against something that cannot be pushed.

**A periodic world aliases against a periodic sampler.** Polling near a multiple of the 3 s
auto-attack cycle returned the same phase nine times, reading exactly like "the character never
moves". Vary the spacing deliberately rather than taking more samples at the same cadence.

**Prefer normal PIE for anything timed.** In `bSimulate: true` the dummy's looping timer stopped
after ~30 s and never resumed, unexplained *(2026-08-12)*. Editor focus is **not** the variable — that
intermediate conclusion was wrong.

**The `TimeDilation` route is closed.** `AWorldSettings::TimeDilation` rejects writes;
`AActor::CustomTimeDilation` is writable but world timers do not scale with it.

**Editor log timestamps are UTC; git commits are local.** A log reading `2026.08.13-02.07` and a
commit reading `2026-08-12 17:26 -0600` are the same evening.

### Reading the logs

**`GetLogEntries` returns a *window* from the end of the log, so a mixed-frequency pattern lies about
absence.** `DODGE|BUFFER|DEATH|REVIVE` at `maxEntries: 60` returned 2 dodges; `DODGE` alone at
`maxEntries: 0` returned 30. **One pattern per event class, and `maxEntries: 0`, whenever the
question is "did this ever happen".**

**`GetLogEntries` silently defaults `category` to `"LogsToolset"`**, which is not a real category, so
omitting it fails with an error that reads like the log system is broken. **Pass `category: ""`
explicitly, every time.**

**The combat trace itself is in `Docs/Debug-Instruments.md`** — every tag it prints, the three
cvars and their defaults, the two ungated warnings, and the traps in reading it. Split out
2026-08-14; it grows with combat features and this file does not.

## Verifying combat changes

After any structural change — a refactor, a type change, a system swapped out — **re-verify
everything that previously worked, not only what changed.** Migrating ability input from an enum to
gameplay tags wiped a Blueprint map purely because the value type changed, and nothing announced it.

Name the checks up front, run them in one session, report as a pass/fail table.

- Damage lands in **exact expected multiples**, not "a bar moved"
- Abilities still grant, and end cleanly (`bIsActive: false` at rest)
- **No stuck state tags.** `State.Attacking` is activation-blocking, so a leak disables all future
  attacks; a leaked `State.Attacking.Committed` forbids every future *defensive* action, and a leaked
  `State.Dodging` leaves the character permanently invulnerable
- Locomotion and jump, whenever input or movement code was touched
- **The attack still plays its montage**, whenever meshes, skeletons or animation assets were
  touched. The tell that it is *not* is the absence of `RELEASE BEGIN`/`END` — those come from a
  notify, so they only fire if the montage really ran, while everything else looks healthy either
  way. **An attack that silently deals no damage is the failure mode.**
- `LogAbilitySystem` free of new warnings
- **Death and revive leave nothing stranded.** Die *in mid-air* specifically: `DisableMovement` stops
  the fall so `Landed()` never fires, and anything keyed to landing stays set past the revive

With the stamina economy involved, add:

- **Stamina lands on exact values** — a dodge from full reads exactly 50
- **Regen resumes at the right moment** — the action's duration *plus* `StaminaRegenPauseSeconds`
  from when it ended, then at `StaminaRegenPerSecond`
- **Exhaustion triggers at zero and clears at Max, not on a timer**, and the regen pause applies
  throughout — **check that pair specifically, since it briefly did not**: a dodge that exhausts you
  must finish, then wait, before the bar moves at all. `CLAUDE.md`'s Stamina section is the rule; this
  is only the check
- **Nothing in the build can drain stamina without a human at the keyboard**, so every check above
  needs hands on the dodge key. The attribute set cannot be written through the toolset either —
  `SpawnedAttributes` is not reflection-readable — so there is no automated substitute *(2026-08-14)*
- **Attribute *base* values are clamped, not just current.** A base drifted above Max is invisible on
  the bar and makes every cost read wrong
- **Costs never gate.** Dodging below the cost must still work and empty the bar

Most of this is checkable without UI via `AbilitySystemInspectorToolset` against the `UEDPIE_0_`
actors while PIE runs. **Those calls are separate round-trips, so a snapshot can straddle a state
change** — an ability reading `bIsActive: false` beside a live `State.Attacking` is usually sampling
skew. Take several samples before believing one.

---

## Running git

**Push through the Bash tool, never PowerShell.** Bash reaches Git Credential Manager; the PowerShell
tool runs with `GIT_TERMINAL_PROMPT=0` and fails with `could not read Username`. `gh` is not
installed. **A failure in one shell is not a statement about capability** — that generalisation was
made once and was wrong.

**A push can hang while the commit has already landed** *(observed 2026-08-10)*. Check
`git rev-list --left-right --count origin/main...HEAD` before assuming failure. Re-running is safe.
