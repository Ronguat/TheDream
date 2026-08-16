# Working in Unreal on this project

**Read this file front to back at the start of every session.** It is not a reference for when
something breaks; it is what must already be in your head before you touch the editor, because most
of what it describes **fails silently** — a write that returns true and changes nothing, a build
that never happened, a log that lies about absence. It is kept short enough for that to be
reasonable: **anything that can be compressed to its rule has been**, and the incidents live in git
and `Docs/Combat-Decisions.md`.

**The budget is ~500 lines, enforced when you add, not when you audit.** Past it, compress first —
the person adding a line knows what it replaces. Stated as a number because the vaguer version
drifted **47 lines in a day** after the 2026-08-13 cut from 820 without anyone noticing.

**It is a tripwire, not a cap** (2026-08-15, the user's call). Correct responses: compress, relocate
to a triggered doc — how `Docs/Debug-Instruments.md` was born — or raise it with a dated note when
the growth is genuinely rule material read every session. Deleting a live rule to hit the number is
the one wrong answer. *Raised 400 → 500 on 2026-08-15; relocation was declined because toolset
capability fails silently and so must be in your head before you touch the editor.*

**Growth that is this file's belongs here; the project's does not.** Those 47 lines were our own
debug instrumentation — one line per combat feature, forever — and now live in
`Docs/Debug-Instruments.md`.

**Confidence marks.** *(confirmed)* was observed directly; *(reported once)* comes from a single
unreproduced incident. Never promote a mark without re-observing the behaviour.

**Re-test any limit that blocks you, whatever its mark, and record the result** — a fresh date if it
held, a correction if it did not. Seven walls here fell in one evening on 2026-08-15, every one
written as flat assertion and never re-poked since. **A limit is a measurement with a date, not a
property of the engine**, and routing a human around one that is not there costs their evening.

---

## Before you start

**The editor must be open before Claude Code starts.** `unreal-mcp` is an HTTP server hosted by the
in-editor plugin (`127.0.0.1:8000`, see `.mcp.json`).

- **Editor absent at Claude Code startup** *(reported twice)* — the tools never register for that
  entire session, and opening the editor afterwards does not fix it. Restart Claude Code.
- **Editor closed and reopened mid-session** *(confirmed)* — fine, tools resume by themselves.

The distinction is **registration versus connection**: schemas are picked up once at session start,
the connection can drop and re-establish. So closing the editor for a rebuild is safe; starting
without one is not. If asset writes are needed, confirm the tools respond before promising any.

**Diff the registry against `Docs/Toolset-Snapshot.tsv`** — one `list_toolsets` call. A new row
means the surface grew and this file's limits deserve re-reading. **Toolset-level only**: it cannot
see a new tool inside an existing toolset, which is the price of staying cheap enough to maintain.

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
what makes it safe: the user stops working in the editor on reading it. Announce and proceed in the
same turn; opening needs no announcement.

**Process up is not ready.** The port can listen while the engine boots and a call in that window
fails with `Unable to connect`; the only reliable signal is **an MCP call returning a result**.
Poll `SceneTools.get_current_level` rather than sleeping — a blind wait is wrong in both directions.

**Calling a tool takes three fields** *(re-derived from transcripts at six calls' cost,
2026-08-15)*: `toolset_name` exactly as `list_toolsets` prints it, `tool_name` as the bare function
name, and `arguments`. Inside `execute_tool_script`, call `get_execution_environment` once first;
scripts define `run()` returning a dict and pass **full dotted names** to `execute_tool`.

**Stop PIE before compiling a Blueprint or saving an asset.** While PIE runs, actor lookups return
the `UEDPIE_0_` world's actors — right for inspecting live state, wrong for authoring.

### Driving the editor's UI, and the console

**`SlateInspectorToolset` is a Playwright-style surface over the editor's own widget tree** —
`Windows`, `Observe`, `Snapshot`, `Click`, `Type`, `PressKey`, `Drag`, `Screenshot`. *(Found
2026-08-15, never previously tried, so every "needs a human" claim predating it was written
without it.)*

**The editor console is drivable, and it is the only console route there is** — `EditorAppToolset`
searches cvars and cannot set one. `Observe` the main window, `Snapshot` for the status-bar textbox
beside the **"Cmd"** combobox, `Type` with `submit: true` *(verified 2026-08-15 by toggling a cvar
and reading it back through a different path)*.

**Menus navigate, which answers "where is this in the editor" without guessing** *(2026-08-15)*.
`Click` a dropdown, `Click` an entry for its submenu, `PressKey Escape` to leave no state behind.
Menus are separate Slate windows; **`Hover` does not open a submenu and `Click` does**; a full
`Snapshot` here is enormous, so **`WaitFor` is the cheap presence probe**. **Use it before describing
a UI location from memory** — a confident guess at where Blend Profiles live was wrong in a
plausible-sounding way. **Keep it read-only** unless a change was asked for; it is their live editor.

**It does not reach the game** *(confirmed 2026-08-15, both confounds killed first)*. `PressKey`
delivers to the focused **accessible** widget and the PIE viewport is absent from the accessibility
tree, in-viewport and floating alike. **There is no synthetic gameplay input**: anything needing a
player to act needs a human, or a debug driver on the pawn. No exec route either — the input entry
points carry no `UFUNCTION`, and **a timer-driven function is not evidence of reflection**.

**A PIE transform is not a placed transform** *(confirmed 2026-08-13)*: it is where an actor *ended
up* — settled under gravity, pushed if anything could push it — and one PIE reading was written up
as a correction to documentation that was right. **The tell is that `z` has also moved.** Re-read in
the editor world before writing any placement number down.

---

## Building C++

**State up front whether a change touches a header.** That single fact decides whether the editor
must close, and it is easy to lose track of mid-discussion.

**Live Coding patches exist only in the memory of the process that compiled them.** A new `UCLASS`
patched that way vanishes on restart and every Blueprint parented to it fails to load — surfacing as
`Failed to load Class /Script/...`, which looks like asset corruption, sessions later. **Anything
touching reflection — new classes, new or renamed `UPROPERTY`s, new module dependencies — needs a
full editor-closed rebuild.** It has also crashed the editor once on a change it should have
handled *(reported once)* — a convenience that can cost you the editor, not the cheap path.

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
than every source file. The unattended close/build/reopen cycle removes the pause where a missing
build used to be noticed; a fix was once handed over for testing having never been compiled.

```bash
# Empty output means the DLL is newer than every source. This is the check.
find Source \( -name "*.cpp" -o -name "*.h" \) -newer Binaries/Win64/UnrealEditor-TheDream.dll -print
ls Binaries/Win64/UnrealEditor-TheDream.patch_* 2>/dev/null || echo "no patch files"
```

Use `-newer` rather than comparing timestamps by eye — `ls` and `find -printf` report in **different
timezones** here, making yesterday's file look newer than a fresh build. Sweep leftover `patch_*`
files so the binary state is unambiguous.

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

- **Round-tripping proves nothing**: `set_properties` returns true and `get_properties` reads the
  value back intact while the write accomplishes nothing *(reported once, twice in a session)*.
- **Binary presence proves a write landed, not that anything derived from it rebuilt** *(2026-08-11)*.
  A BlendSpace's `SampleData` wrote, read back, grew the `.uasset` and displayed on the grid — and
  produced no pose, the derived interpolation grid never having rebuilt. Suspect **any asset type
  with a build step**. Only play confirms those.
- **For BlendSpaces specifically, the split is position versus count** *(2026-08-15, the harder half
  learned by crashing the editor)*. **Moving** a sample keeps the array length, so the cached
  triangulation's indices stay valid — that write works, and **merely opening the asset finishes the
  rebuild**, no edit or save needed. **Removing** a sample does not: the triangulation still indexes
  the old length and the engine dies on evaluation with
  `Array index out of bounds: 16 into an array of size 16`. **Never change a BlendSpace's sample
  count by reflection write — sample removal is a human job**, because the editor rebuilds the
  triangulation as part of the operation and a property write does not.
- **CDO writes are property-dependent and the CDO cannot tell you** *(2026-08-13)*. Two writes to
  `GA_Attack` seconds apart: a direct object reference did **not** reach the live instance, object
  references inside a struct array did, and both read correctly off the CDO throughout.

**For a CDO, the artefact is the runtime instance** — not the CDO, not the file. Reach it during PIE
via `ActivatableAbilities` on the ASC; each spec's `nonReplicatedInstances` holds the live ability's
`refPath`. The GAS inspector cannot (`GetGrantedAbilities` returns names only), and no mechanism is
offered for why properties differ.

**A Blueprint CDO property set programmatically is generally not live in the current editor
session** *(confirmed 2026-08-10, reproduced deliberately)*. Return value, read-back and the file
on disk were all green while the running game used the old value.

| Change made by | Takes effect after |
|---|---|
| Details panel | the next **PIE** restart |
| `set_properties` | the next **editor** restart — a PIE restart is not enough |

So prefer the details panel for anything a designer would touch anyway, and restart before trusting
a programmatic write. **The rule is about Blueprint CDOs and does not extend to plain assets**
*(confirmed 2026-08-12)* — an AnimSequence's `bEnableRootMotion` took effect immediately.

**Staleness is per property, so an object can be *partially* live** *(confirmed 2026-08-14)* —
`GA_Block`'s live instance had one tag container current and another empty, split by which side of
the last restart each write landed on, while the CDO read correct for both. **Batch CDO writes and
restart once**, and when a setting "is not working" **read the runtime instance before touching it**.

**Prove the instrument before trusting a null result.** When the evidence is *"I changed it and the
symptom did not move"*, the restart rule makes that ambiguous between a refuted hypothesis and a
write that never landed. Make one write whose effect is numerically measurable, confirm it, then
trust what follows. **Where a value drives behaviour, print the value** — that is what made
`DodgeSeconds` diagnosable where judging 0.2 s by feel would not have been.

**`reset_properties` resets to the property's *default*, not the inherited archetype value**
*(confirmed 2026-08-12)* — it wrote `(0,0,0)` over a component offset. There is no scriptable
equivalent of the details panel's revert arrow; **set the inherited value explicitly instead.**

**A C++ component default reaches nothing if a Blueprint or placed actor overrides it** *(confirmed
2026-08-12)* — a mesh offset needed **three** writes, two Blueprints and the placed actor, each a
serialized copy. After any C++ default change to an inherited component, read it back off a live
PIE actor.

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
- **`AssetTools` functions taking an `asset_path` string false-negative on assets that exist** —
  `exists`, `is_dirty`, `get_asset_class` and `save_assets` all rejected `GA_Attack` 2026-08-14 while
  `find_assets` listed it and `load_asset` returned it. **Use `load_asset` as the existence check**,
  and **`save_assets` with an empty list**, then `git status` to see what was written. `duplicate`
  fails with a bare `false`; a plain asset may need a human, though a `CurveFloat` worked 2026-08-13.
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
  succeeding on `EditAnywhere` ones, which is the cheapest confirmation; `set_properties` refuses
  them too *(both confirmed 2026-08-14)*, so **delete-and-re-place is the only route**, not merely
  the tidiest. **Diff the whole instance against the CDO first, not the properties you suspect** —
  overrides are the one thing nobody has a list of — and note the transform and label. Expect the
  actor's internal name to change (`_C_1` → `_C_0`), which breaks any doc naming it.

---

## Not scriptable at all

Needs a human in the editor:

- Creating levels, BlendSpaces and AnimBlueprints **from scratch**
- Placing or configuring AnimNotifies — a montage's `notifies` is not even readable, so notify
  placement can only be verified at runtime
- A montage's **`compositeSections`** — neither readable nor writable, and `sequenceLength` is
  read-only and does not recompute after a reflection write
- **`UCurveFloat`'s keys** *(confirmed 2026-08-13)* — `FloatCurve` is a bare `UPROPERTY()` the
  reflection layer cannot see. Creating the asset by duplication works, so the split is **script the
  asset, have a human author the keys**; only measured travel can confirm a curve's mean.

**A montage is the exception and is ~90% scriptable** *(2026-08-15)*. `AssetTools.duplicate` clones
one with its skeleton intact, and the segment repoints by writing **`slotAnimTracks` whole**. That
write is **live, not a round-trip** — two montages sharing a parent rendered visibly different poses
through `CaptureAssetImage`. What stops it is derived state: `sequenceLength` keeps the *source's*
value, and **opening the montage recomputes it unaided**, so the human step is open-and-save.
**Multi-section montages are fully out**, a design constraint rather than a chore: four directional
clips must be four montages.

**Duplication carries the source's notifies, and `notifies` is unreadable — so you cannot see what
you copied** *(2026-08-15, caught by the user by eye after the toolset called the montage healthy)*.
Cloning `AM_Attack` dragged its **Release Window** across, and `UAnimNotifyState_MeleeWindow` emits
`RELEASE BEGIN`/`END`, which `s1-*` asserts timing against — so a stray one poisons the checker
while reading as a timing bug. **Never clone an attack montage to make a non-attack one.**

**But creation is per-toolset, not a blanket limitation** *(confirmed 2026-08-12)*. `MaterialTools`
and `MaterialInstanceTools` create and build whole graphs end to end. Check the toolset that owns the
asset type before concluding a thing cannot be made.

**Renaming an AnimNotify class is expensive** — placed notifies serialize against the class path.

**Writing to any graph whose outer is a *node* fails; reading depends on which graph** *(refined
twice on 2026-08-15)*. `create_node` and `find_node_types` resolve the Blueprint through the outer
and fail with *"Cannot cast type 'X' to 'Blueprint'"* for all three: **`AnimStateNode`** (a state's
interior), **`AnimGraphNode_StateMachine`** (the machine itself) and **`AnimStateTransitionNode`**
(a transition's rule). So **the whole state machine is a human job** — states, transitions, rules.

**Reading splits, and that split is worth exploiting.** A **transition rule graph reads perfectly**
(`find_nodes`, `get_node_infos` with full pin detail), so **every rule a human authors can be
verified afterwards** — "they build, we check" is a real division rather than a hopeful one. A
**state's interior returns `[]`**, no error. And `get_node_infos` on an `AnimStateTransitionNode`
fails differently (`no attribute 'get_node_title'`), so **transition *direction* is unreadable by
any route** — check it against a picture.

`list_graphs` enumerates states and transitions regardless, so the tree is visible even where its
graphs are not. **Prove the instrument before believing an empty `find_nodes`** — it returns 8 nodes
on `ABP_Combat:AnimGraph` and `[]` on `Locomotion`, which is how you tell real emptiness from a
silent refusal. And **a creation `type_id` is not the one a node reports** —
`get_node_infos` gives `|GetGroundSpeed` where `create_node` wants `Variables|Default|GetGroundSpeed`;
guessing fails with *"does not exist"*, which reads like a wall. Use `find_node_types`, filtered
tightly, and only on a graph that is actually reachable.

**Optional anim-node properties become pins by writing `ShowPinForProperties`** *(confirmed
2026-08-14)* — flip `bShowPin` where `bCanToggleVisibility` is true, compile, read the node back.
That is how a `BlendSpacePlayer`'s `BlendSpace` becomes drivable; `set_pin_value` then swaps the
asset without touching the graph, which is also **the cheapest way to eyeball an animation**: point
the pin at it, PIE, `CaptureViewport`, point it back.

**AnimGraph *editing* is scriptable** *(confirmed 2026-08-11)*; only creation is not. `BlueprintTools`
has `list_graphs`, `find_nodes`, `get_node_infos`, `create_node`, `connect_pins`, `set_pin_value`,
`retarget_node_class`, `compile_blueprint` and `delete_node`. There is no disconnect function;
deleting a node breaks its links. **Reconnecting an exec output replaces its existing link**, which
is how a node is spliced into a running chain without one. `read_graph_dsl` returned empty for
`ABP_Combat:AnimGraph` — use `find_nodes` with `title: ""` plus `get_node_infos`, which reports pin
connections both ways. Change a node by a **partial** write to its `Node` struct; a full write
clobbers pin-backed fields. `describe_toolset` on it is too large to return — grep a saved dump.

**`add_variable` does not make a variable live; compiling does** *(confirmed 2026-08-15)*. It
returns null either way, and the new property is unreadable on the CDO until `compile_blueprint`
runs — which reads exactly like the write having failed.

### The user can send images, and it beats most of the limits above

**New as of 2026-08-15**, and it matters more here than most places: this toolset's hard limits are
overwhelmingly **visual** — a state graph's interior, a montage's notify track, a BlendSpace grid, a
details panel. All are listed above as unreadable and a screenshot settles each instantly. **So
ask.** Cases already met: which of four directional clips is which, whether a duplicated montage
carried an inherited notify, and what a state contains so a new one can mirror it.

**Calibration — images for what no tool can reach; tools for what is readable.** A screenshot of
something `get_node_infos` can report is slower *and* worse: reading the blocking Selects gave exact
asset paths and pin indices no picture would have carried. **And check the limit is real before
asking** — an image request resting on a stale assumption spends the user's time working around a
wall that may not exist, and re-certifies the claim as fact.

**Our own capture tools reach less than a human screenshot** — `CaptureViewport`,
`CaptureAssetImage` and the Slate `Screenshot`/`CaptureEditorImage` cover the viewport, one asset's
thumbnail, and windows, but **nothing can navigate to a state graph's interior**. *Untested hybrid:
have the user open the graph, then `CaptureEditorImage`.*

---

## Measuring and diagnosing

**Before testing whether a symptom depends on X, test whether it depends on anything at all** — a
strictly cheaper question that partitions the search harder. The hover bug was chased for two
sessions through skeletons, root motion and montages while the level viewport displayed it statically
the whole time. **A static defect is fully visible on a placed actor with no PIE and nothing
running**, which makes the viewport the cheapest instrument here.

**A sufficient explanation is not the actual one.** Prefer an experiment that *manipulates* the
suspected cause over one that only observes it. When two hypotheses are killed by evidence, file the
anomaly rather than inventing a third.

**An assumed control is worse than no control — a comparison case only disconfirms if it was
actually *measured*.** The hover hunt killed its first hypothesis with "the dodge has the same
setting and does not hover", having never checked: a guess carrying the authority of evidence, and
it corrupts not the conclusion but the test used to reject one.

**What the user glosses over is often the decisive observation.** "The dummy hovers in the preview
too" reframed a two-session bug instantly; when a bug resists, ask what *else* shows the symptom.

**A single fixed test configuration is a filter.** An automated PIE run spawns both characters at
their placed transforms and nobody moves, so **"no damage landed" is not evidence about hit
detection**. Move something, or turn on the debug draw and look.

**`StartPIE` takes a `startTransform`** that overrides the player spawn for that session only — the
way to measure around a pawn without dirtying the level.

**Measuring an actor's own movement requires nothing else touching it, and a capsule counts.** Two
42 cm radii touch at 84 cm of separation, so contamination begins long before two characters look
close; a travel figure measured against a blocked capsule had to be withdrawn.

**And do not measure one actor's travel against another actor's *assumed* position** — the general
form of the PIE-transform trap above, and it bit twice: two 2026-08-14 readings computed closing
distance against the dummy's **placed** origin while the dummy was being shoved across the floor by
the very attacks being measured, and both were withdrawn. **A moving reference frame reads as a
movement fault in the thing being measured.** Re-read both transforms, or measure against something
that cannot be pushed.

**A periodic world aliases against a periodic sampler.** Polling near a multiple of the 3 s
auto-attack cycle returned the same phase nine times, reading exactly like "the character never
moves" — vary the spacing deliberately rather than taking more samples at the same cadence.

**Prefer normal PIE for anything timed.** In `bSimulate: true` the dummy's looping timer stopped
after ~30 s and never resumed, unexplained *(2026-08-12)*; editor focus is **not** the variable.

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
tags wiped a Blueprint map purely because the value type changed, and nothing announced it.

Name the checks up front, run them in one session, report as a pass/fail table. **Much of the list
below is now automated** — `Tools/RegressionCheck/regression-check.sh` asserts the timing, stamina
and guard invariants against a PIE log and prints that table itself; see `Docs/Debug-Instruments.md`
for the scenario matrix, and run its `--self-test` before trusting a green result.

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

- **Exact values, regen resumption and the exhaustion pair are all asserted by `s2-*`/`s3`** — a
  dodge from full reading exactly 50, regen resuming at action end plus `StaminaRegenPauseSeconds`,
  and exhaustion entering at 0 and clearing at Max rather than on a timer. `CLAUDE.md`'s Stamina
  section is the rule; the checker is the check
- **Stamina can now be drained unattended** *(2026-08-15, replacing "nothing in the build can drain
  stamina without a human at the keyboard")* — `ETDDebugDefendMode` on the training dummy holds a
  guard or dodges on a timer. **The attribute set still cannot be written through the toolset** —
  `SpawnedAttributes` is not reflection-readable — so *setting* a bar to an arbitrary value remains
  impossible; you drive it by spending, not by assignment
- **Attribute *base* values are clamped, not just current.** A base drifted above Max is invisible on
  the bar and makes every cost read wrong
- **Costs never gate.** Dodging below the cost must still work and empty the bar

Most of this is checkable without UI via `AbilitySystemInspectorToolset` against the `UEDPIE_0_`
actors while PIE runs. **Those calls are separate round-trips, so a snapshot can straddle a state
change** — an ability reading `bIsActive: false` beside a live `State.Attacking` is usually sampling
skew. Take several samples before believing one.

---

## Running git

**Push through the Bash tool, never PowerShell.** Bash reaches Git Credential Manager; the
PowerShell tool runs with `GIT_TERMINAL_PROMPT=0` and fails with `could not read Username`; `gh` is
not installed. **A failure in one shell is not a statement about capability.**

**A push can hang while the commit has already landed** *(observed 2026-08-10)*. Check
`git rev-list --left-right --count origin/main...HEAD` before assuming failure. Re-running is safe.
