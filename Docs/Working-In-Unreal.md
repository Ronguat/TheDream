# Working in Unreal on this project

**Read this file front to back before planning or executing work that touches the engine.** It is
not a reference for when
something breaks; it is what must already be in your head before you touch the editor, because most
of what it describes **fails silently** — a write that returns true and changes nothing, a build
that never happened, a log that lies about absence. It is kept short enough for that to be
reasonable: **anything that can be compressed to its rule has been**, and the incidents live in git
and `Docs/Combat-Decisions.md`.

**Confidence marks.** *(confirmed)* was observed directly; *(reported once)* comes from a single
unreproduced incident; ***(inherited)* means nobody has observed it** — recorded from an error
message, a snapshot, or an assumption, and indistinguishable from a confirmed claim without this
mark. Never promote a mark without re-observing the behaviour, and never by repetition.

***(toolset)* narrows a limit to the MCP surface.** Python is a wider one — see the scriptable
section — so a `(toolset)` wall is a claim about what these tools reach, not about the engine.

**An undated mark predates the dating convention**; seven remain, in the oldest material.

**Re-test any limit that blocks you, whatever its mark, and record the result** — a fresh date if it held, a correction if it did not. **A limit is a measurement with a date, not a property of the
engine.**

---

## How to treat a limit — read this before believing anything below

**Everything in this file is a claim about a surface, not about the engine.** That sentence used to
be a caveat on the `(toolset)` mark; on 2026-08-24 a deliberate sweep refuted **eight** recorded
walls in one session, and it is now the operating assumption. The findings are in
`Docs/Combat-Decisions.md`; what follows is the method, which is worth more than any of them.

**There are three surfaces, widest last.** Test in this order and record which one you tested:

| | Reaches | Costs |
|---|---|---|
| **MCP toolset** | whatever a tool wraps | free; narrowest, and resolves objects by name |
| **Editor Python** | the whole reflection surface, plus `BlueprintCallable` and non-reflected Python methods | free; cannot obtain some handles |
| **C++ in `TheDreamEditor`** | everything the engine exports | a rebuild, and engine-version coupling |

**Three causes recur, and each predicts which surface wins.** Learn the tells:

- **A protected reflection view of a public C++ member.** The tell is an error reading *"is protected
  and cannot be read"* — which is a verdict about reflection, never about access. `EdGraph::Nodes`,
  `Skeleton::Sockets`, `UCurveFloat::FloatCurve` are all public to C++.
- **A handle the surface cannot obtain, though the API is public.** The tell is being able to *name*
  a `BlueprintCallable` function you cannot *call*. Enhanced Input's injection lives on a local
  player subsystem and Python exposes only engine and editor subsystem getters. The fix is a shim,
  not a reimplementation.
- **An outer walk one level too shallow.** The tell is a cast failure naming a type from the outer
  chain — *"Cannot cast type 'AnimGraphNode_StateMachine' to 'Blueprint'"*. The engine's own helper
  usually walks the whole chain where the tool checked one link.

**An empty result is not a negative result.** `find_nodes` returned `[]` on a graph addressed by
*name* and its full contents addressed by *full object path*. Before believing an emptiness, change
the **address** and try again — and never change the address and the target in the same step, which
is how the original test convinced itself.

**Three things make a limit expensive rather than merely wrong**, and all three are about the record:

- **`(toolset)` names one surface and stops reading that way after about a week** on the page.
- **`(inherited)` means nobody ever observed it.** One such claim was contradicted by its own
  neighbour in the same file and survived anyway.
- **A limit re-filed as a *design constraint* stops being re-tested at all.** This is the worst
  form. *"Multi-section montages are fully out, a design constraint rather than a chore"* shaped a
  slice plan three days after it was written, and the project had already been shipping a
  multi-section montage the whole time.

**Lifting a wall is not automatically worth it.** Build the route when a slice needs it; **record the
refutation and stop when nothing does.** Four refutations from the sweep were deliberately left
unimplemented for exactly that reason. **The finding is the valuable half** — a plan made against a
wrong limit is the expensive failure, and a missing helper is cheap by comparison.

**The verification rules do not relax because the route is new.** Verify against the artefact and
prefer a check that does not go back through the layer that wrote it; and for any asset type with a
build step, **only play confirms**. A structurally valid thing that has never driven a frame is
scaffolding, not a capability.


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
| Save | `AssetTools.save_assets` naming the paths — **stop PIE first**, or every path call fails; see the empty-list trap below |
| Close | `taskkill //F //IM UnrealEditor.exe` — exits in ~2 s |

**Save, then read `git status`, then kill.** Calling `save_assets` is not the check; *seeing the
files listed* is. **Two failure modes wear the same face:** a write that is saved but not yet live
needs a restart and is safe, and a write that was never saved is already gone. Both read back fine
from inside the editor. `git status` is the only thing that separates them and it must happen
*before* the kill.

**The MCP registry has no quit tool; Python does** *(refuted 2026-08-27)*.
`unreal.SystemLibrary.quit_editor()` exists, so "closing is always a forced kill" was a claim about
one surface. **The API is confirmed present and has not been exercised** — `taskkill` remains what
this project has actually used. **Saving covers assets only** either way: in-progress asset-editor
state dies unasked.
`Saved/Autosaves/PackageRestoreData.json` reading `Packages: []` confirms nothing was stranded;
**`Packages` is the field that matters, not `RestoreEnabled`**, which is not a stable signal.
**Read it only against a closed editor**: a running one populates it by autosaving a dirty package,
so its mtime against the editor's start time is what separates bookkeeping from wreckage.

**Before an unattended relaunch, reset that file to `{"RestoreEnabled": false, "Packages": []}`** —
a forced kill after an autosave leaves it populated and the reopened editor blocks on a restore
dialog nobody is there to answer *(bit 2026-08-24)*; declining restore is already the standing rule.

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

**Python sets cvars directly, and the Slate console is the fallback rather than the only route**
*(refuted 2026-08-27)*. `unreal.SystemLibrary.execute_console_command(None, "Cvar.Name 1")` sets one
and `get_console_variable_int_value` reads it back — measured `TD.DebugCombatTiming` 1 → 0 → 1
through `run-in-editor.py`. `EditorAppToolset` searching but not setting is true of **MCP only**.
The Slate route below still matters for anything that is not a cvar or a console command. `Observe` the main window, `Snapshot` for the status-bar textbox
beside the **"Cmd"** combobox, **`Click` it to focus**, then `Type` with `submit: true` *(confirmed 2026-08-15; the `Click` confirmed 2026-08-25 — without it `Type` returns `false` and looks exactly like the quote bug next door)*. **`Type` fails on a single quote** *(confirmed
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

**Slate does not reach the game, but Enhanced Input does** *(the Slate half confirmed 2026-08-15;
the general claim refuted 2026-08-24)*. `PressKey` delivers to the focused **accessible** widget and
the PIE viewport is absent from the accessibility tree, in-viewport and floating alike — that part
stands. **What was wrong is the conclusion drawn from it.** Enhanced Input ships
`InjectInputForAction` and `StartContinuousInputInjectionForAction` as `BlueprintCallable`, so
synthetic gameplay input exists; what blocked it is that both live on a **local player subsystem**,
and Python exposes only `get_editor_subsystem` and `get_engine_subsystem`. **The API was reachable
and its handle was not** — the same shape as `SkeletalMesh::SetSkeleton` and `EdGraph::Nodes`.

**`UTDInputTools` in `Source/TheDreamEditor/` closes it**: `InjectAction` for a tap, `StartHold` /
`StopHold` for a held one. Measured on the real player pawn 2026-08-24 — one injection produced
`INPUT pressed`, `ACTIVATE swing=0`, `AIM WEDGE reach=550`, `INPUT released` nine ms later and a
`STRING` link window, which is a light because a one-tick press is a tap; a hold started and stopped
from two separate script calls measured **607 ms** and escalated the ladder, `AIM WEDGE` reach
climbing 550 → 650 → 750. **Timed defensive fixtures are scriptable from here on.**


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

**`grep -o -i -F` together crashes Git Bash's grep** *(confirmed 2026-08-27, reproduced in an empty
directory)* — SIGABRT, exit **134**, core dumped, and a `grep.exe.stackdump` left in the working
directory. `-o -F` and `-o -i` each work alone; only the trio aborts. **Piped into `wc -l` the crash
is invisible** — the count reads `0` and the absence looks clean, which is how it nearly produced a
false absence claim during the limit audit. **A stackdump file appearing is the tell**, and `echo $?`
is the check. The standing rule already covers it: a filter finding nothing proves only that the
filter did not match — and here the filter did not even run.

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
  count by reflection write** — the editor rebuilds the triangulation as part of the operation and a
  property write does not. **It is not a human job, though** *(corrected 2026-08-24)*: `UBlendSpace`
  exposes `AddSample`, `DeleteSample`, `EditSampleValue` and `ReplaceSampleAnimation` as
  `ENGINE_API`, which is the editor's own path. Reflection is the wrong tool here, not the wrong
  idea.
- **CDO writes are property-dependent and the CDO cannot tell you** *(2026-08-13)*. Two writes to
  `GA_Attack` seconds apart: a direct object reference did **not** reach the live instance, object
  references inside a struct array did, and both read correctly off the CDO throughout.

**For a CDO, the artefact is the runtime instance** — not the CDO, not the file. Reach it during PIE
via `ActivatableAbilities` on the ASC; each spec's `nonReplicatedInstances` holds the live ability's
`refPath`. The GAS **inspector toolset** returns names only (`GetGrantedAbilities`) and offers no
mechanism for why properties differ — **an MCP limit, not a project one** *(refuted 2026-08-27)*.
From Python the component answers directly: `AbilitySystemComponent` exposes `get_all_abilities`,
`activatable_abilities`, `find_all_abilities_with_tags` and `find_all_abilities_with_input_id`.

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
*(confirmed 2026-08-12)* — it wrote `(0,0,0)` over a component offset, so **set the inherited value
explicitly** rather than resetting. **Detecting an override *is* scriptable** *(refuted 2026-08-27)*:
`EditorAssetLibrary.is_editor_property_overridden(obj, "PropName")` returns `OVERRIDDEN` / `DEFAULT`
/ `NOT_FOUND` / `ACCESS_DENIED`. **It is an enum, not a bool** — the object is truthy at every value,
so compare against the members or every property reads as overridden. Measured 2026-08-27: both
training dummies and both PlayerStarts read `DEFAULT` on nine combat properties.

**A C++ component default reaches nothing if a Blueprint or placed actor overrides it** *(confirmed
2026-08-12)* — a mesh offset needed **three** writes, two Blueprints and the placed actor, each a
serialized copy. After any C++ default change to an inherited component, read it back off a live
PIE actor.

**When configuring an asset type for the first time, diff it against a known-good asset of the same
type** — a broken one usually looks fine alone.

### Confirmed traps

- **A CDO write does not reach a runtime-spawned actor until the Blueprint is compiled**
  *(confirmed 2026-08-25)*. `unreal.get_default_object(bp.generated_class())` then
  `set_editor_property` reads back correctly and reaches **placed** actors, but a pawn the
  GameMode spawns still gets the old value — one PIE session ran the dummies at 0.7 and the
  player at 0.8 from a single write. **The tell is `get_dirty_content_packages()` staying
  empty**: the write never went through the Blueprint's commit path. Adding `cdo.modify()`,
  `bp.modify()` and `BlueprintEditorLibrary.compile_blueprint(bp)` marks both packages dirty and
  the value then reaches every pawn. Editing Class Defaults **by hand compiles for you**, which
  is why the manual route works where the scripted shortcut did not. Leave the packages unsaved
  and the trial reverts on the next editor start. **This makes value trials free** -- an
  `EditDefaultsOnly` number can be swept without a rebuild -- but a value that *ships* belongs in
  the C++ default with no Blueprint override left behind to shadow it.
- **An asset the registry has no entry for still saves — through the *package*, not the path**
  *(refuted 2026-08-25)*. `AS_SwordAndShieldAnimV1_Defense_Hit_Fw_RM` answers **False** to
  `does_asset_exist` while `load_asset` returns it and `set_editor_property` takes, so every
  path-based save refuses: `AssetTools.save_assets` reports *"Asset does not exist"* and
  `EditorAssetLibrary.save_asset` / `save_loaded_asset` both return False. **`save_packages` takes
  package objects and never consults the registry**, which is the whole difference:
  `EditorLoadingAndSavingUtils.save_packages(list(get_dirty_content_packages()), False)` returned
  True and the `.uasset` changed on disk. This was filed as *"cannot be saved from either
  scripting surface, C++ is the untested third"* — **wrong on both counts**: the fourth Python
  route was never tried, and C++ was never needed. `get_dirty_map_packages` is the same shape for
  levels.
- **InputMappingContext** *(confirmed 2026-08-21)* — UE 5.8 reads `defaultKeyMappings.mappings`;
  top-level `mappings` reads empty on `IMC_Combat` while its input works, which is the proof.
- **GameplayEffect modifier attribute** *(confirmed 2026-08-21, corrected)* — writing
  `attributeName` + `attributeOwner` does not re-resolve the `FProperty`, and it does **not** leave
  it null as recorded: it **keeps the previous attribute**. Wrote `Health` on a duplicate;
  `attribute` stayed `...:Stamina` while `attributeName` read `Health`, so the effect modifies the
  old one while reading as the new — worse than an empty field. Re-pick in the details panel.
- **A GameplayEffect's inline tag containers cannot be written** *(confirmed 2026-08-10)* —
  `inheritableOwnedTagsContainer` and `ongoingTagRequirements` accept writes and read back empty; UE
  5.8 moved this to `gEComponents`. Numeric properties on the same asset write fine, so a
  partially-configured effect is the likely outcome. **Adding a GEComponent is scriptable from C++**
  *(corrected 2026-08-24)* — `UGameplayEffect::GEComponents` is a public array with a header-inline
  `AddComponent<T>()`, and `gE_components` already reads from Python. Only the *reflection* route is
  shut.
- **Object references need the full path** *(confirmed 2026-08-21)* — `/Game/Path/Asset.Asset`. A
  short path is refused outright; this one fails loudly rather than silently.
- **Array edits** *(confirmed 2026-08-21, corrected)* — changing an element and adding one in one
  call is **refused atomically**, not partially written as recorded; the container is untouched and
  the error names the reason. **Empty it, then write it whole**, as two calls.
  `FGameplayTagContainer`'s array is `gameplayTags`.
- **TMap keys** — the misleading `added key ... not found in map` log **no longer reproduces**
  *(2026-08-21)*; a key added to `abilityInputActions` landed silently and read back.
- **`AssetTools` path functions answer `false` while PIE is running** *(cause isolated 2026-08-24)*.
  `exists`, `is_dirty`, `get_asset_class` and **named `save_assets`** all fail during a play session
  — `exists` returning a bare `false`, the rest *"Asset does not exist"* — and every one answers
  correctly the moment PIE stops. **The MCP layer is not the variable**:
  `EditorAssetLibrary.does_asset_exist` behaves identically through `run-in-editor.py`, returning
  `False` for all five paths tried **including the currently loaded level**, then `True` for all
  five with PIE stopped. **Registry-backed calls are unaffected** — `find_assets` and `load_asset`
  keep answering throughout, which is what makes this look like a path-form problem and sends you
  hunting the wrong thing. **Check `IsPIERunning` before concluding anything about a path**, and
  note that a session inherited from the user may already have PIE up. The 2026-08-21 bullet
  recording these as working stands, measured with no PIE running.
- **`StopPIE` reporting success is not proof PIE stopped** *(2026-08-24)*. It returned cleanly and
  `IsPIERunning` still read `true` well afterwards; a second call cleared it. **Poll `IsPIERunning`**
  rather than trusting the return — the same shape as process-up not meaning editor-ready.
- **The two `save_assets` forms do different jobs, and picking the wrong one bakes a trap**
  *(2026-08-20)*. The empty list saves everything dirty **including the level**, which is how the
  stale-override trap below gets created after a CDO session. **Naming the assets works and scopes
  the write** — re-tested 2026-08-20, contradicting the older advice to always pass an empty list —
  **but it fails like every other path call while PIE is up**, which is a reason to stop PIE, never
  a reason to reach for the empty list. **There is no discovery call** *(enumerated 2026-08-21)*:
  the empty list saves rather than lists, and `is_dirty` takes one `asset_path`, so it can only
  confirm a file you already suspect. Name the paths, and **`git status` either way** — seeing the
  files listed is the check, calling save is not. Against a **clean tree** it doubles as the audit
  showing whether the level got caught up, so commit or stash before saving.
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
  overrides used to be the one thing nobody had a list of, and
  `EditorAssetLibrary.is_editor_property_overridden` now enumerates them *(refuted 2026-08-27)* —
  and note the transform and label. Expect the
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
second scripting surface into the editor, wider than the toolsets — **every limit marked
*(toolset)* is a candidate to be lifted through it**, and one marked *(inherited)* was never tested
against anything.

**`ProgrammaticToolset` is not that surface** *(confirmed 2026-08-22)* — its sandbox refuses
`import unreal`; six stdlib modules only.

Needs a human in the editor — **shorter than it was, and every removal below was a surface confusion
rather than an engine limit** *(swept 2026-08-24)*:

- **Creating levels, BlendSpaces and AnimBlueprints from scratch — refuted.** The mark was
  *(inherited)*, meaning nobody had ever observed it, and every factory is in Python:
  `LevelFactory`, `WorldFactory`, `BlendSpaceFactoryNew`, `BlendSpaceFactory1D`,
  `AnimBlueprintFactory`, `AnimCompositeFactory`, `CurveFactory`. **The claim was already
  contradicted by its own neighbour** — `Docs/Anim-Pipeline.md` creates montages with
  `AnimMontageFactory` through `AssetTools.create_asset`, which is the same route.
- Placing or configuring AnimNotifies *(toolset)* — `get_properties` on `AM_Attack` answers *"could
  not be read: notifies"* *(re-tested 2026-08-19, held)*. **The toolset's limit, not the engine's**:
  C++ reads `UAnimMontage::Notifies` (`UTDParryAbility::FindGestureTime`), and the editor's own
  `AnimationBlueprintLibrary` — Python `unreal.AnimationLibrary` — writes and reads them:
  `add_animation_notify_state_event(montage, track, start, duration, cls)` on any
  `UAnimSequenceBase`; `get_animation_notify_events` with `get_anim_notify_event_trigger_time` /
  `_duration` to read back; `add_animation_notify_track` for the track. It validates the track and
  `0 ≤ start ≤ play length`, outers the notify to the montage, and **does not dirty the package** —
  `modify()` dirties it; `EditorLoadingAndSavingUtils.save_packages` saves it; `git status`.
  *(confirmed 2026-08-22: written, read back and saved on a scratch montage — not yet fired at
  runtime.)* So the split is now **the script places it, C++ reads it, the trace line verifies
  it** — a screenshot answers a question the game answers itself every run.
- A montage's **`compositeSections` as a reflected property** *(toolset)* — neither readable nor
  writable *(re-confirmed 2026-08-21)*, and `sequenceLength` is read-only and does not recompute
  after a reflection write. **Sections themselves are not blocked** *(corrected 2026-08-24)*:
  `get_num_sections` and `get_section_name` read them from Python, and `ENGINE_API
  AddAnimCompositeSection(FName, float)` writes them. **`slotAnimTracks` reads back in full
  structural detail**, not only writes whole: every segment's `animReference`, `startPos`,
  `animStartTime`, `animEndTime` and `animPlayRate` come back *(2026-08-21)*.
- **`UCurveFloat`'s keys — refuted from C++** *(2026-08-24; the reflection half of the 2026-08-13
  entry stands)*. `FloatCurve` is a bare `UPROPERTY()` the reflection layer cannot see, **and it is a
  public `FRichCurve` member**, so `AddKey` from an editor module authors keys directly. The old
  split — *script the asset, have a human author the keys* — was a fact about reflection.
- **BlendSpace sample removal — refuted from C++** *(2026-08-24)*. The warning that a reflection
  write corrupts the cached triangulation is **correct and worth keeping**; what was missing is that
  `UBlendSpace` exposes `AddSample`, `DeleteSample`, `EditSampleValue` and `ReplaceSampleAnimation`
  as `ENGINE_API`, which is the editor's own path and rebuilds properly.
- **Adding a GEComponent — refuted** *(2026-08-24)*. `gE_components` **reads from Python already**,
  and `UGameplayEffect::GEComponents` is a public array with a header-inline `AddComponent<T>()`.

**A montage is the exception and is ~90% scriptable** *(2026-08-15)*. `AssetTools.duplicate` clones
one with its skeleton intact, and the segment repoints by writing **`slotAnimTracks` whole**. That
write is **live, not a round-trip** — two montages sharing a parent rendered visibly different poses
through `CaptureAssetImage`. What stops it is derived state: `sequenceLength` keeps the *source's*
value, and **opening the montage recomputes it unaided**, so the human step is open-and-save.
**Multi-section montages were never out, and the "design constraint" framing was the damage**
*(refuted 2026-08-24)*. Sections **read from Python today** — `get_num_sections` and
`get_section_name` on any montage, and `AM_Dodge` returns eight: `Fw FR R BR Bw BL L FL`, so the
project has held a multi-section montage since Dodge shipped. `ENGINE_API
AddAnimCompositeSection(FName, float)` writes them. **Filed as design rather than tooling it went
unre-tested, and it shaped a slice plan three days later** — the rule that a limit is a measurement
with a date bites hardest on the ones that have stopped looking like limits.

**An animation asset's skeleton pointer is read-only, and the bulk route deletes things**
*(2026-08-24, the Skeleton Merge slice)*. `Skeleton` refuses a reflection write on **`AnimSequence`,
`AnimMontage` and `SkeletalMesh` alike**, and the anim types carry no `SetSkeleton` UFUNCTION — so
there is **no per-asset repoint** for the clips and montages that make up most of a merge. Three
routes exist and none generalises: `SkeletalMesh` reaches `SetSkeleton` through **`call_method`**,
which the property itself refuses; **`AnimBlueprint.target_skeleton`** is a plain writable property;
and **`EditorAssetLibrary.consolidate_assets(target, [sources])`** repoints every referencer at once.
Consolidate is **per-skeleton, not per-asset** — there is no way to scope it to a subset — and it
**deletes the sources**, leaving `ObjectRedirector`s. **It rewrites only packages already loaded**:
4 files changed on disk against 103 referencers, the rest resolving through the redirector until
each is loaded, `modify()`d and saved. There is **no `FixupReferencers`** on `AssetTools` or
`EditorAssetLibrary`; load-and-re-save is the fixup. **Referencer counts stay stale until
`scan_paths_synchronous(force_rescan=True)`**, and read wrong long after the files are right —
`git status` is the check, not the registry.

**Duplication carries the source's notifies — the *rule* stands, the blindness does not**
*(blindness refuted 2026-08-27)*. A cloned attack montage brings its **Release Window** with it, and
`UAnimNotifyState_MeleeWindow` emits `RELEASE BEGIN`/`END`, which `s1-*` asserts timing against — so
a stray one poisons the checker while reading as a timing bug. **Never clone an attack montage to
make a non-attack one.** But you can now *see* what you copied: `unreal.AnimationLibrary` reads
notifies off any montage — `get_animation_notify_events`, `get_animation_notify_event_names`,
`get_anim_notify_event_trigger_time`, `_duration`. Measured on `AM_Attack`: **one event,
`MeleeWindow`**. **Read a duplicate before trusting it**; the check is one call.

**But creation is per-toolset, not a blanket limitation** *(confirmed 2026-08-12)*. `MaterialTools`
and `MaterialInstanceTools` create and build whole graphs end to end. Check the toolset that owns the
asset type before concluding a thing cannot be made.

**Renaming an AnimNotify class is expensive** — placed notifies serialize against the class path.

**A state machine is not a human job, and the claim that it was survived two re-tests because both
used the same surface** *(measured 2026-08-24, superseding everything recorded 2026-08-15)*.

**Reading is fully open, and the old `[]` was a bad address rather than a wall.**
`BlueprintEditorLibrary.list_graphs` in **Python** returns every graph in an AnimBlueprint as an
object — both state machines, every state graph, every transition graph — and `graph.get_outer()`
hands back the `AnimStateNode` / `AnimStateTransitionNode` that owns each. **`find_nodes` and
`get_node_infos` then work on a state's interior when the graph is addressed by its full object
path**, which is what the earlier test did not do: addressing by *name* returns `[]`, so proving the
instrument on `AnimGraph` and then reading empty on a nested graph changed two things at once.
`ObjectTools.list_properties` / `get_properties` / `set_properties` also work on state and
transition nodes by path — `bAlwaysResetOnEntry`, the `stateEntered` / `stateLeft` /
`stateFullyBlended` notifies, `priorityOrder`, `blendMode`, `logicType`. A transition's
`priorityOrder` and `blendMode` were written and read back through a different layer.

**Structure is closed to both scripting surfaces, for one precise reason.** `create_node` and
`find_node_types` resolve the owning Blueprint from the graph's **immediate** outer, which for a
nested graph is a node — *"Cannot cast type 'AnimGraphNode_StateMachine' to 'Blueprint'"*. Identical
by path and by name, so it is a too-shallow outer walk rather than an addressing problem.
`EdGraph.Nodes` refuses reflection both directions, `EdGraph` exposes no methods to Python, and
`new_object` will construct an `AnimStateNode` that nothing can register. There is no clipboard or
graph-text route anywhere in the API.

**C++ lifts it, and the engine's own entry point is exported.**
`FEdGraphSchemaAction_NewStateNode::PerformAction` is `ANIMGRAPH_API` with a public header-inline
`SpawnNodeFromTemplate<>` over it; `UCLASS(MinimalAPI)` on the node classes does not bite, because
`NewObject` needs only `StaticClass()` and everything else is a virtual or a data member.
**`UEdGraph::Nodes` is public to C++** — "protected" is a reflection fact only, the same shape as
`Skeleton::Sockets`. `FBlueprintEditorUtils::FindBlueprintForGraph` walks the *whole* outer chain,
which is exactly what the two failing tools do not.

`Source/TheDreamEditor/` is the module that does it — editor-target-only, exposed to Python and MCP
as `UTDStateMachineTools`. **Proven 2026-08-24:** reading a machine's 15 nodes, creating a named
state with its `AnimationStateGraph` and `StateResult`, creating a transition with its rule graph
and `TransitionResult`, and compiling the result to a valid `AnimBlueprintGeneratedClass`, verified
through MCP rather than the layer that wrote it. **Not yet proven: populating interiors** — a
`SequencePlayer` into a state, a rule into a transition — and **nothing built this way has driven a
mesh in PIE**, which by this file's own rule about assets with a build step is the confirmation that
counts. **The module is engine-version-coupled**; `SpawnNodeFromTemplate` already carries a
deprecating vector parameter.

**Transition direction is still unreadable from Python or MCP** *(re-tested 2026-08-27, held —
`AnimStateTransitionNode` exposes neither `get_previous_state` nor `get_next_state` to Python)* — `AnimStateTransitionNode` is not a
`UK2Node`, so `get_node_infos` fails on `get_node_title` and `list_all_pins` refuses it. From C++ the
pins are in hand, so this falls whenever it is worth five lines.

`list_graphs` enumerates states and transitions regardless, so the tree is visible even where its
graphs are not.

**An anim node's On Update / On Become Relevant can be bound to a C++ function, and it needs no
custom `UAnimInstance`** *(proven 2026-08-25 on both stun tells)*. `UAnimGraphNode_Base`'s three
function properties carry **`AllowFunctionLibraries`**, so a static method on a
`UBlueprintFunctionLibrary` resolves — the ABP keeps whatever parent it has and no graph is
authored. The contract the compiler enforces is the `PrototypeFunction` named in that property's
metadata — `AnimExecutionContextLibrary.Prototype_ThreadSafeAnimUpdateCall`, i.e.
`(const FAnimUpdateContext&, const FAnimNodeReference&)` — plus `meta=(BlueprintThreadSafe)`.
`ValidateFunctionRef` errors on a reference that will not resolve, a mismatched signature or a
function that is not thread-safe, so **a clean compile is the proof the binding took**.

**Writing the binding needs C++** — `FMemberReference`'s members are private `SaveGame`
UPROPERTYs, so `get_editor_property` returns an opaque empty struct in both directions, while
`SetExternalMember(FName, TSubclassOf<UObject>)` is `ENGINE_API` and the property itself is
public. `UTDStateMachineTools::SetNodeUpdateFunction` is that, and the reference reaches the
runtime node only at **compile**, so recompile the AnimBlueprint after writing one.

**What such a function may do to a sequence player is first-class, not a reflection hack**:
`USequencePlayerLibrary` exposes `SetAccumulatedTime`, `SetPlayRate`, `SetSequence` and
`ComputePlayRateFromDuration`, all `BlueprintThreadSafe`. Note that
`FAnimNode_SequencePlayerBase::UpdateAssetPlayer` hands the accumulator's address to the tick
record, which advances it by `delta * rate` **after** the update function runs — so driving the
playhead explicitly requires setting the rate to zero, or the write drifts every frame.

**A mechanic's animation may therefore be invisible to every search you would think to run**
*(2026-08-24, wrong twice in one exchange)*. Blockstun's tell is a Locomotion **state**, so there is
no asset named for it and no `*Montage` property pointing at it — `find Content -iname "*lockstun*"`
and a source grep both return nothing while the animation plays perfectly on screen. **Absence of a
montage is not absence of an animation.** Before concluding a mechanic has no tell, ask the designer
what they see, or check whether its state getter is `BlueprintPure` — that is the tell that the state
machine can reach it.
 **Prove the instrument before believing an empty `find_nodes`** — it returns 8 nodes
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

**`CaptureViewport` renders the *editor* world, not PIE** *(2026-08-24)*. Given a `captureTransform`
it draws that view of the editor level — capsule wireframes, light gizmos, the axis widget, and no
player pawn, because the player exists only in the PIE world. It also returns the PNG **inline as
base64**, which overflows the response limit; the payload lands in a tool-results file and has to be
decoded out of it.

**`AutomationLibrary.take_high_res_screenshot` is the PIE route** *(2026-08-24)*. It captures the
**game** viewport with the debug HUD live — HP and stamina bars, state tags, the facing readout —
and **writes straight to `Saved/Screenshots/WindowsEditor/`**, so nothing needs decoding. Paired with
time dilation below, any moment in combat is capturable: slow the world, drive the input, poll for
the state, shoot.

`CaptureAssetImage` and the Slate `Screenshot`/`CaptureEditorImage` still cover one asset's
thumbnail and windows, and **nothing can navigate to a state graph's interior**. *Untested hybrid:
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

**The `TimeDilation` route is open, and the closed verdict tested the wrong two things**
*(refuted 2026-08-24)*. `AWorldSettings::TimeDilation` does reject reflection writes and
`AActor::CustomTimeDilation` does not scale world timers — both true, and neither is the API.
**`GameplayStatics.set_global_time_dilation(world, x)`** is `BlueprintCallable` and works from
Python during PIE: set to 0.15, game time advanced **0.47 s against 3.81 s of wall clock**, a
measured 0.12 ratio. **0.04 turns a 0.55 s hitstun into nearly fourteen seconds of wall time**, which
is what makes a window that short observable at all. Restore it to 1.0 before drawing any timing
conclusion — every trace timestamp is game time.

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
