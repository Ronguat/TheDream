# Working in Unreal on this project

**Read this file front to back before planning or executing work that touches the engine.** It is
not a reference for when
something breaks; it is what must already be in your head before you touch the editor, because most
of what it describes **fails silently** — a write that returns true and changes nothing, a build
that never happened, a log that lies about absence. It is kept short enough for that to be
reasonable: **anything that can be compressed to its rule has been**, and the incidents live in git
and `Docs/Combat-Decisions.md`.

**Confidence marks.** *(confirmed)* was observed directly; *(reported once)* comes from a single
unreproduced incident. Never promote a mark without re-observing the behaviour.

**Re-test any limit that blocks you, whatever its mark, and record the result** — a fresh date if it held, a correction if it did not. **A limit is a measurement with a date, not a property of the
engine.**

---

## Before you start

`unreal-mcp` is an HTTP server hosted by the in-editor plugin (`127.0.0.1:8000`, see `.mcp.json`).
**`CLAUDE.md` carries the startup registration rule** *(reported twice)*, because it has to be
known before this file is triggered.

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
| Save | `AssetTools.save_assets` naming the paths — see the empty-list trap below |
| Close | `taskkill //F //IM UnrealEditor.exe` — exits in ~2 s |

**Save, then read `git status`, then kill.** Calling `save_assets` is not the check; *seeing the
files listed* is. **Two failure modes wear the same face:** a write that is saved but not yet live
needs a restart and is safe, and a write that was never saved is already gone. Both read back fine
from inside the editor. `git status` is the only thing that separates them and it must happen
*before* the kill.

There is **no graceful quit** — checked across the whole toolset registry. So closing is always a
forced kill, and **saving covers assets only**: in-progress asset-editor state dies unasked.
`Saved/Autosaves/PackageRestoreData.json` reading `Packages: []` confirms nothing was stranded;
**`Packages` is the field that matters, not `RestoreEnabled`**, which is not a stable signal.

**Announce before closing, every time — an announcement, not a request**: reading it is what stops
the user working in a dying editor. Announce and proceed in the same turn; opening needs no
announcement.

**Process up is not ready.** The port can listen while the engine boots and a call in that window
fails with `Unable to connect`; the only reliable signal is **an MCP call returning a result**.
Poll `SceneTools.get_current_level` rather than sleeping — a blind wait is wrong in both directions.

**Calling a tool takes three fields**: `toolset_name` exactly as `list_toolsets` prints it, `tool_name` as the bare function
name, and `arguments`. Inside `execute_tool_script`, call `get_execution_environment` once first;
scripts define `run()` returning a dict and pass **full dotted names** to `execute_tool`.

**Stop PIE before compiling a Blueprint or saving an asset.** While PIE runs, actor lookups return
the `UEDPIE_0_` world's actors — right for inspecting live state, wrong for authoring.

**Never duplicate a World Partition level to make a new map** — the external actor packages do not
re-path and actors silently go missing. Use File → New Level → Empty.

### Driving the editor's UI, and the console

**`SlateInspectorToolset` is a Playwright-style surface over the editor's own widget tree** —
`Windows`, `Observe`, `Snapshot`, `Click`, `Type`, `PressKey`, `Drag`, `Screenshot`.

**The editor console is drivable, and it is the only console route there is** — `EditorAppToolset`
searches cvars and cannot set one. `Observe` the main window, `Snapshot` for the status-bar textbox
beside the **"Cmd"** combobox, `Type` with `submit: true` *(confirmed 2026-08-15)*. **`Type` fails on a single quote** *(confirmed
2026-08-21)* — it returns `false`, logs nothing, enters nothing, and reads like a permissions
refusal. Double quotes are fine, so write `print("x")` and never `print('x')`.

**Menus navigate, which answers "where is this in the editor" without guessing** *(2026-08-15)*.
`Click` a dropdown, `Click` an entry for its submenu, `PressKey Escape` to leave no state behind.
Menus are separate Slate windows; **`Hover` does not open a submenu and `Click` does**; a full
`Snapshot` here is enormous, so **`WaitFor` is the cheap presence probe**. **Use it before describing a UI location from memory.** **Keep it read-only** unless a change was asked for; it is their live editor.

**In conversation, say what you are about to do before driving their UI.** A menu opening by itself
on someone's screen is startling in a way a file edit is not — the surface is in front of them and
moving without warning. This is the one toolset where the user watches it happen. **Greenlit
execution is exempt**: it was agreed in advance, and hesitating there is the failure, not the care.

**It does not reach the game** *(confirmed 2026-08-15, both confounds killed first)*. `PressKey`
delivers to the focused **accessible** widget and the PIE viewport is absent from the accessibility
tree, in-viewport and floating alike. **There is no synthetic gameplay input**: anything needing a
player to act needs a human, or a debug driver on the pawn. No exec route either — the input entry
points carry no `UFUNCTION`, and **a timer-driven function is not evidence of reflection**.

**A PIE transform is not a placed transform** *(confirmed 2026-08-13)*: it is where an actor *ended
up* — settled under gravity, pushed if anything could push it. **The tell is that `z` has also moved.** Re-read in
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


**"Bash cannot do this" is nearly always wrong, and believing it is what breaks the rule.** There is
**no usable Python on PATH** *(machine fact, hit twice)* — `command -v` finds three WindowsApps
aliases that print an install prompt when run, so the check reads as though an interpreter is
there. The *editor's* Python is a separate thing and does work; see the scriptable section. Git
Bash ships `base64`, `reg`, `xxd`, `certutil` and `curl`. Check for the binary before concluding
otherwise.

**A single Bash tool command near ~14 KB can arrive mangled** *(reported once, 2026-08-19)* —
a quoted heredoc died with a shell parse error mid-content, while the same content in ~5 KB
appended chunks wrote cleanly. Write large files in chunks and read the line count back.

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
build would once have been noticed.

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

**Read a property off the asset rather than guessing from a filename** *(confirmed 2026-08-11)*. The
call shape is
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
- **For BlendSpaces specifically, the split is position versus count** *(confirmed 2026-08-15)*. **Moving** a sample keeps the array length, so the cached
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

**Prove the instrument before trusting a null result — or a confirming one.** When the evidence is
*"I changed it and the symptom did not move"*, the restart rule makes that ambiguous between a
refuted hypothesis and a write that never landed. Make one write whose effect is numerically
measurable, confirm it, then trust what follows. **Where a value drives behaviour, print the
value.**

**`reset_properties` resets to the property's *default*, not the inherited archetype value**
*(confirmed 2026-08-12)* — it wrote `(0,0,0)` over a component offset. There is no scriptable
equivalent of the details panel's revert arrow; **set the inherited value explicitly instead.**

**A C++ component default reaches nothing if a Blueprint or placed actor overrides it** *(confirmed
2026-08-12)* — a mesh offset needed **three** writes, two Blueprints and the placed actor, each a
serialized copy. After any C++ default change to an inherited component, read it back off a live
PIE actor.

**When configuring an asset type for the first time, diff it against a known-good asset of the same
type** — a broken one usually looks fine alone.

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
  `find_assets` listed it and `load_asset` returned it. **Use `load_asset` as the existence check.**
  `duplicate` fails with a bare `false`; a plain asset may need a human, though a `CurveFloat`
  worked 2026-08-13.
- **The two `save_assets` forms do different jobs, and picking the wrong one bakes a trap**
  *(2026-08-20)*. The empty list saves everything dirty **including the level**, which is how the
  stale-override trap below gets created after a CDO session. **Naming the assets works and scopes
  the write** — re-tested, contradicting the older advice to always pass an empty list. **There is
  no discovery call** *(enumerated 2026-08-21)*: the empty list saves rather than lists, and
  `is_dirty` takes one `asset_path`, so it can only confirm a file you already suspect. Name the
  paths, and `git status` either way — seeing the files listed is the check, calling save is not.
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

## What is and is not scriptable

**Enumerate before concluding a toolset cannot do something.** `describe_toolset` overflows the
response limit on a large toolset, and the overflow is written to a file — so grep the file for
tool names rather than reading it:

```bash
grep -o '"name":"<toolset>\.[a-z_]*"' <saved-file> | sed 's/.*\.//;s/"//' | sort -u
```

That returned 51 tools for `BlueprintTools` in one call. **A `describe_toolset` that will not fit
is the exact condition under which a capability goes unnoticed for months.**

**There is no IK Rig or IK Retargeter toolset** *(2026-08-21, read off a full `list_toolsets`
response — `ControlRigTools` is Control Rig, a different system, and `SkeletalMeshTools` is mesh,
bones and sockets)*. The `IKRig` plugin is enabled, so the system exists; only the MCP surface is
missing. Python is the only candidate route and is untested for it.

**The Unreal Python API is reachable, and nothing needs installing** *(confirmed 2026-08-21)*.
`PythonScriptPlugin` is an engine default, so it is enabled while absent from the uproject's plugin
array — checking there reads as "not installed" and is a filtered view. `IKRig` runs its own
`init_unreal.py` at startup, so that API is loaded too.

The route is a two-step REPL: `Type` a `py` statement into the status bar's Cmd textbox with
`submit: true`, then read what it printed with `LogsToolset.GetLogEntries` on category `LogPython`.
`py import unreal; print(unreal.SystemLibrary.get_engine_version())` returned `5.8.1`. This is a
second scripting surface into the editor, wider than the toolsets, and **the limits below were all
measured against the toolsets alone** — re-test any of them through Python before treating it as a
wall.

Needs a human in the editor:

- Creating levels, BlendSpaces and AnimBlueprints **from scratch**
- Placing or configuring AnimNotifies — a montage's `notifies` is not even readable, so notify
  placement can only be verified at runtime. **Re-tested 2026-08-19 and it held**: `get_properties`
  on `AM_Attack` still answers *"the following properties could not be read: notifies"*.

  **But that is the toolset's limit, not the engine's — C++ reads `UAnimMontage::Notifies` fine**
  *(2026-08-19)*. `UTDParryAbility::FindGestureTime` walks the array to find its marker's trigger
  time at activation. So the split for anything notify-driven is: **a human places it, C++ reads
  it, and the trace line it emits is what lets us verify the placement.** Reaching for a screenshot
  here would be answering a question the game can answer itself, every run.
- A montage's **`compositeSections`** — neither readable nor writable *(re-confirmed 2026-08-21)*,
  and `sequenceLength` is read-only and does not recompute after a reflection write.
  **`slotAnimTracks` reads back in full structural detail**, not only writes whole: every segment's
  `animReference`, `startPos`, `animStartTime`, `animEndTime` and `animPlayRate` come back
  *(2026-08-21)*, so a montage's construction is inspectable even where its sections are not
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
you copied** *(confirmed 2026-08-15)*. A cloned attack montage brings its **Release Window** with
it, and `UAnimNotifyState_MeleeWindow` emits `RELEASE BEGIN`/`END`, which `s1-*` asserts timing
against — so a stray one poisons the checker while reading as a timing bug. **Never clone an attack
montage to make a non-attack one.**

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

**Creating a Blueprint asset *is* scriptable, and the wall was never real** *(2026-08-18)*.
`BlueprintTools.create` takes `folder_path`, `asset_name` and an `asset_type` class reference and
returns the new Blueprint — `GA_Parry` was made from `/Script/TheDream.TDParryAbility` that way,
inheriting every C++ default correctly. `set_parent` and `get_parent` reparent an existing one. This
does not contradict "AnimGraph creation is not scriptable" above, which is about *graphs*; it is the
asset that can be made.

What hid it: the snapshot recorded *"describe_toolset too large to return"* for `BlueprintTools`,
which is a fact about the **description** and says nothing about the capability. The absence was
inherited rather than measured — see the enumeration recipe at the top of this section.

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
strictly cheaper question that partitions the search harder. **A static defect is fully visible on a placed actor with no PIE and nothing
running**, which makes the viewport the cheapest instrument here.

**A sufficient explanation is not the actual one** — manipulate-over-observe is `CLAUDE.md`'s
standing rule. When two hypotheses are killed by evidence, file the anomaly rather than inventing
a third.

**An assumed control is worse than no control — a comparison case only disconfirms if it was
actually *measured*.** An unchecked comparison is a guess carrying the authority of evidence, and
it corrupts not the conclusion but the test used to reject one.

**What the user glosses over is often the decisive observation.** When a bug resists, ask what
*else* shows the symptom.

**A single fixed test configuration is a filter.** An automated PIE run spawns both characters at
their placed transforms and nobody moves, so **"no damage landed" is not evidence about hit
detection**. Move something, or turn on the debug draw and look.

**`StartPIE` takes a `startTransform`** that overrides the player spawn for that session only — the
way to measure around a pawn without dirtying the level.

**Measuring an actor's own movement requires nothing else touching it, and a capsule counts.** Two
42 cm radii touch at 84 cm of separation, so contamination begins long before two characters look
close; a travel figure measured against a blocked capsule had to be withdrawn.

**And do not measure one actor's travel against another actor's *assumed* position** — the general
form of the PIE-transform trap above. **A moving reference frame reads as a
movement fault in the thing being measured.** Re-read both transforms, or measure against something
that cannot be pushed.

**A periodic world aliases against a periodic sampler.** Sampling near a multiple of the fixture's
cycle returns the same phase every time, reading exactly like "the character never moves" — vary the
spacing deliberately rather than taking more samples at the same cadence.

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
everything that previously worked, not only what changed.** A type change can silently wipe a
Blueprint map, and nothing announces it.

Name the checks up front, run them in one session, report as a pass/fail table. Most of it is
automated: `Tools/RegressionCheck/regression-check.sh` asserts the invariants against a PIE log and
prints that table itself. **Run its `--self-test` before trusting a green result.** The checklist,
the scenario matrix and the fixtures are in `Docs/Debug-Instruments.md`.

---

## Running git

**Push through the Bash tool, never PowerShell.** Bash reaches Git Credential Manager; the
PowerShell tool runs with `GIT_TERMINAL_PROMPT=0` and fails with `could not read Username`; `gh` is
not installed. **A failure in one shell is not a statement about capability.**

**A push can hang while the commit has already landed** *(observed 2026-08-10)*. Check
`git rev-list --left-right --count origin/main...HEAD` before assuming failure. Re-running is safe.

**A mid-session `/model` switch misattributes every later `Co-Authored-By` trailer** *(reported
once, 2026-08-19)* — the harness keeps injecting the previous model's byline, and a model cannot
verify its own name from the inside. Sign from the session transcript's `"model":` fields, not
the injected instruction.
