# Working in Unreal on this project

**Read this file front to back before planning or executing work that touches the engine.** It is
not a reference for when
something breaks; it is what must already be in your head before you touch the editor, because most
of what it describes **fails silently** — a write that returns true and changes nothing, a build
that never happened, a log that lies about absence. It is kept short enough for that to be
reasonable: **anything that can be compressed to its rule has been**; the per-capability answers
live in `Docs/Unreal-Findings.md`, and the incidents in git and `Docs/Combat-Decisions.md`.

**Confidence marks.** *(confirmed)* was observed directly; *(reported once)* comes from a single
unreproduced incident; ***(inherited)* means nobody has observed it** — recorded from an error
message, a snapshot, or an assumption, and indistinguishable from a confirmed claim without this
mark. Never promote a mark without re-observing the behaviour, and never by repetition.

***(toolset)* narrows a limit to the MCP surface.** Python is a wider one — see
`Docs/Unreal-Findings.md` — so a `(toolset)` wall is a claim about what these tools reach, not about
the engine.

**An undated mark predates the dating convention**; **three remain** *(counted 2026-08-27)*, all in
the oldest material.

**Re-test any limit that blocks you, whatever its mark, and record the result** — a fresh date if it held, a correction if it did not. **A limit is a measurement with a date, not a property of the
engine.**

---

## How to treat a limit — read this before believing anything below

**Everything in this file is a claim about a surface, not about the engine.** That sentence used to
be a caveat on the `(toolset)` mark; on 2026-08-24 a deliberate sweep refuted **eight** recorded
walls in one session, and it is now the operating assumption. The findings are in
`Docs/Unreal-Findings.md`; what follows is the method, which is worth more than any of them.

**There are four rungs, widest last.** Test in this order and record which one you tested:

| | Reaches | Costs |
|---|---|---|
| **MCP toolset** | whatever a tool wraps | free; narrowest, and resolves objects by name |
| **Editor Python** | the whole reflection surface, plus `BlueprintCallable` and non-reflected Python methods | free; cannot obtain some handles |
| **The engine's headers** | whether C++ *would* lift it, and the exact symbol | free; answers the question without answering the need |
| **C++ in `TheDreamEditor`** | everything the engine exports | a rebuild, and engine-version coupling |

**The header row is the one people skip, and it is the cheap one.** At the C++ surface the probe
*is* the build — there is no introspection call — so the question *"would C++ lift this?"* looks
expensive and gets deferred forever. Reading the header answers it for the cost of a grep:
`grep -rn SymbolName "/c/Program Files (x86)/UE_5.8/Engine/Source"`, and an `ENGINE_API`,
`UNREALED_API`, `ANIMGRAPH_API` or `UE_API` on a public member settles it. **Record the symbol and
stop**; build only when a slice needs it.

**Every capability claim names a surface and a date, and `docs-check` fails without them.** A claim
that something cannot be done is safe only when the reader can see *which surface was tried* and
*when* — otherwise an MCP-only result reads identically to one tested everywhere, and nothing
invites a re-test. `Tools/DocsCheck/claim-scan.pl` shortlists the offenders; if the honest answer is
that no surface is involved, say **engine behaviour**, **fixture behaviour** or **machine fact**,
which is a complete answer and still carries its date.

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

**A limit found by exhausting your own reach is the narrowest kind there is.** Every surface a
session can drive may refuse what a control one keystroke outside it does — four routes came back
empty against the MCP registration wall, and a `/mcp` click went through *(2026-08-28)*. Exhausting
what you can drive measures *you*, not what is possible. **Say which of the two you measured.**

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
**`CLAUDE.md` carries the startup registration rule** *(confirmed 2026-08-28)*, because it has to be
known before this file is triggered.

- **Editor closed and reopened mid-session** *(confirmed 2026-08-27 — quit and relaunched, the tools
  answered again with no Claude Code restart)* — fine, tools resume by themselves.
- **Session started with no editor** *(MCP, confirmed 2026-08-28)* — the schemas never arrive on
  their own, however healthy the endpoint becomes. **Ask for `/mcp` → Reconnect**: the tools land
  inside the turn, no restart. Was *(reported twice)*, and was wrongly read as terminal.

The distinction is **registration versus connection**: schemas are picked up once at session start,
the connection can drop and re-establish. So closing the editor for a rebuild is safe; starting
without one costs a reconnect. If asset writes are needed, confirm the tools respond before
promising any.

**Four server states, and only one is stuck** *(Bash, 2026-08-28)*: **connecting** delivers its tools
when ready, **connected-then-dropped** redials itself, **disconnected** withdraws them, and **failed
at the initial connect** stays failed until something reconnects it. Only the last needs a human.

**The harness replays that failure verbatim** *(MCP, confirmed 2026-08-28)* — it reported
`ConnectionRefused` while `claude mcp list` reported the same server connected, the same minute.
**The message is a recording of session start, not a probe.** `claude mcp list` is the live check.

**Unattended, go around it**: `Tools/McpBridge/ue-mcp.sh` speaks MCP to the plugin over HTTP and
needs no registration — `casc-run.sh`'s shape, for Unreal *(Bash, confirmed 2026-08-28)*.
**Registered tools first whenever they exist**: the allowlist applies to them, and the bridge hands
back raw JSON-RPC.

**Diff the registry against `Docs/Toolset-Snapshot.tsv`** — one `list_toolsets` call. A new row
means the surface grew and this file's limits deserve re-reading. **Toolset-level only** *(MCP,
2026-08-27)*: the snapshot cannot see a new tool inside an existing toolset, which is the price of
staying cheap enough to maintain. `describe_toolset` does enumerate a toolset's tools, so
tool-level diffing is available when a question needs it — it is simply not what this file stores.

---

## Driving the editor

*(confirmed 2026-08-11, full cycle tested)* The assistant opens and closes the editor itself.

| Step | How |
|---|---|
| Running? | `tasklist \| grep -i UnrealEditor.exe` |
| Open | `nohup "/c/Program Files (x86)/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "<abs path>/TheDream.uproject" >/dev/null 2>&1 &` |
| Save | `AssetTools.save_assets` naming the paths — **stop PIE first**, or every path call fails; see the empty-list trap below |
| Close | `run-in-editor.py -c "import unreal; unreal.SystemLibrary.quit_editor()"` — graceful; `taskkill //F //IM UnrealEditor.exe` is the fallback |

**Save, then read `git status`, then kill.** Calling `save_assets` is not the check; *seeing the
files listed* is. **Two failure modes wear the same face:** a write that is saved but not yet live
needs a restart and is safe, and a write that was never saved is already gone. Both read back fine
from inside the editor. `git status` is the only thing that separates them and it must happen
*before* the kill.

**Closing: `unreal.SystemLibrary.quit_editor()` is the graceful route and it works** *(Python,
exercised 2026-08-27)*. The MCP registry carries no quit tool; `taskkill` is the fallback. The
call **returns before teardown** — the runner printed `RESULT` and exited 0 — and the shutdown is
orderly: the log ends `LogExit: Exiting.` then `Log file closed`, and
`PackageRestoreData.json` is **rewritten** with the clean marker rather than left stale, which is
what a forced kill leaves behind. **A dirty map does not prompt it** *(confirmed 2026-08-28)*.
**Dirty content is untested and deliberately stays that way** *(the designer, 2026-08-28)*: closedown
saves first every time, so the case should not arise, and the mechanism this replaced was
`taskkill //F`, which discards dirty content without asking — **the swap can only be neutral or
safer, so no new risk was introduced and there is nothing to prove.** **Saving covers assets only**
either way: in-progress asset-editor state dies unasked.
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

**Process up is not ready** *(MCP, confirmed 2026-08-27 — an editor started this session answered
only after several seconds)*. The port can listen while the engine boots and a call in that window
fails with `Unable to connect`; the only reliable signal is **an MCP call returning a result**.
Poll `SceneTools.get_current_level` rather than sleeping — a blind wait is wrong in both directions.

**`StartPIE`'s error on a long warmup is the MCP call timing out, not PIE failing to start**
*(MCP, confirmed 2026-08-28)*. `warmupSeconds` past roughly 25 makes the call return
*"Timed out waiting for PIE to start"* while the session comes up and runs normally — `IsPIERunning`
answers `true` straight after. **Poll it before believing the error**, and expect a second `StartPIE`
to refuse with *"A play session is already running."*

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
*(Python, 2026-08-27)*. `unreal.SystemLibrary.execute_console_command(None, "Cvar.Name 1")` sets one
and `get_console_variable_int_value` reads it back — measured `TD.DebugCombatTiming` 1 → 0 → 1
through `run-in-editor.py`. `EditorAppToolset` searching but not setting is true of **MCP only**
*(re-confirmed 2026-08-28 — its tools are `SearchCVars` and no setter, though the toolset's own
description advertises "modifying ... console variables")*.

**The Slate console still matters for anything that is not a console command** *(Slate, confirmed
2026-08-15)*. `Observe` the main window, `Snapshot` for the status-bar textbox
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

**Slate does not reach the game, but Enhanced Input does** *(Slate, confirmed 2026-08-15; the
injection route, 2026-08-24)*. `PressKey` delivers to the focused **accessible** widget and the PIE
viewport is absent from the accessibility tree, in-viewport and floating alike. Enhanced Input ships
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
there. The *editor's* Python is a separate thing and does work; see `Docs/Unreal-Findings.md`. Git
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

`Build.bat` cannot be called from Git Bash *(Bash, 2026-08-19; not re-tested since — it is a
quoting fact about the shell rather than a claim about the engine)* — the space in
`Program Files (x86)` survives every
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
  property write does not. **Use the engine's own path instead** *(C++, 2026-08-24)*: `UBlendSpace`
  exposes `AddSample`, `DeleteSample`, `EditSampleValue` and `ReplaceSampleAnimation` as
  `ENGINE_API`. Reflection is the wrong tool here, not the wrong idea.
- **CDO writes are property-dependent and the CDO cannot tell you** *(2026-08-13)*. Two writes to
  `GA_Attack` seconds apart: a direct object reference did **not** reach the live instance, object
  references inside a struct array did, and both read correctly off the CDO throughout.

**For a CDO, the artefact is the runtime instance** — not the CDO, not the file. Reach it during PIE
via `ActivatableAbilities` on the ASC; each spec's `nonReplicatedInstances` holds the live ability's
`refPath`. The GAS **inspector toolset** returns names only (`GetGrantedAbilities`) and offers no
mechanism for why properties differ — **an MCP limit, not a project one** *(Python, 2026-08-27)*.
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
explicitly** rather than resetting. **Detecting an override *is* scriptable** *(Python, 2026-08-27)*:
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
  *(Python, 2026-08-25)*. `AS_SwordAndShieldAnimV1_Defense_Hit_Fw_RM` answers **False** to
  `does_asset_exist` while `load_asset` returns it and `set_editor_property` takes, so every
  path-based save refuses: `AssetTools.save_assets` reports *"Asset does not exist"* and
  `EditorAssetLibrary.save_asset` / `save_loaded_asset` both return False. **`save_packages` takes
  package objects and never consults the registry**, which is the whole difference:
  `EditorLoadingAndSavingUtils.save_packages(list(get_dirty_content_packages()), False)` returned
  True and the `.uasset` changed on disk. `get_dirty_map_packages` is the same shape for levels.
- **InputMappingContext** *(confirmed 2026-08-21)* — UE 5.8 reads `defaultKeyMappings.mappings`;
  top-level `mappings` reads empty on `IMC_Combat` while its input works, which is the proof.
- **GameplayEffect modifier attribute** *(confirmed 2026-08-21, corrected)* — writing
  `attributeName` + `attributeOwner` does not re-resolve the `FProperty`, and it does **not** leave
  it null as recorded: it **keeps the previous attribute**. Wrote `Health` on a duplicate;
  `attribute` stayed `...:Stamina` while `attributeName` read `Health`, so the effect modifies the
  old one while reading as the new — worse than an empty field. Re-pick in the details panel.
- **A GameplayEffect's inline tag containers read from the CDO and resist reflection writes**
  *(Python, 2026-08-27)* — address `get_default_object(bp.generated_class())` and
  `inheritable_owned_tags_container` / `ongoing_tag_requirements` return proper
  `InheritedTagContainer` / `GameplayTagRequirements` structs. **Addressing the Blueprint instead of
  its CDO is what reads empty.** Writing at the CDO address is untested; through reflection the
  write is accepted and changes nothing, while numeric properties on the same asset write fine, so a
  **partially-configured effect** is the likely outcome. **UE 5.8 moved this configuration to
  `gEComponents`** *(C++, 2026-08-24)* — `UGameplayEffect::GEComponents` is a public array with a
  header-inline `AddComponent<T>()`, and `gE_components` reads from Python. Only the *reflection*
  route is shut.
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
  the write** *(MCP, re-tested 2026-08-20)*,
  **but it fails like every other path call while PIE is up**, which is a reason to stop PIE, never
  a reason to reach for the empty list. **The MCP layer has no discovery call, Python does**
  *(Python, 2026-08-27)*: the empty list saves
  rather than lists and `is_dirty` takes one `asset_path`, but
  `EditorLoadingAndSavingUtils.get_dirty_content_packages()` and `get_dirty_map_packages()` return
  the list outright, with `save_dirty_packages()` beside them. **The bullet above already used one**,
  which is how this was caught. Name the paths, and **`git status` either way** — seeing the
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
  *(reflection and MCP, confirmed 2026-08-10; the detection half lifted 2026-08-27)*. The placed
  dummy read `DefaultAbilities: []` against a populated CDO and
  was granted nothing. The signature is the instance showing **C++ class defaults** — it was placed
  before the Blueprint authored them. `reset_properties` fails on exactly those names while
  succeeding on `EditAnywhere` ones, which is the cheapest confirmation; `set_properties` refuses
  them too *(both confirmed 2026-08-14)*, so **delete-and-re-place is the only route**, not merely
  the tidiest. **Diff the whole instance against the CDO first, not the properties you suspect** —
  `EditorAssetLibrary.is_editor_property_overridden` enumerates them *(Python, 2026-08-27)* —
  and note the transform and label. Expect the
  actor's internal name to change (`_C_1` → `_C_0`), which breaks any doc naming it.

---

## What each surface reaches — `Docs/Unreal-Findings.md`

**The per-capability answers moved out 2026-08-27**, because they are consulted with a question in
hand rather than read before work. *"Can I do X, and by which surface"* — montages, curves,
BlendSpaces, state machines, GameplayEffects, notifies, screenshots, IK — lives in
`Docs/Unreal-Findings.md`, with the dated findings behind each answer under it.

**Its trigger is the moment you are about to conclude something cannot be done.** Read it then, and
record what you learn there rather than here. **What stays in this file is what fails silently** —
the things you would never think to look up, because nothing tells you to.

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

**The `TimeDilation` route is open** *(Python, 2026-08-24)*. Two near misses worth naming so they
are not retried: `AWorldSettings::TimeDilation` rejects reflection writes, and
`AActor::CustomTimeDilation` does not scale world timers.
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
2026-08-14; it grows with combat features, where this file is held to a line budget so that
"read it front to back" stays a real instruction.

---

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
