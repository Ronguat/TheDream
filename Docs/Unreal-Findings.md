# Unreal findings — what the surfaces reach, and what was learned trying

**Trigger: about to conclude something cannot be done, or about to record that it cannot.** This is
the lookup half of `Docs/Working-In-Unreal.md`, split out 2026-08-27 because that file is read front
to back before engine work and this material is not pre-read — it is consulted with a specific
question in hand.

**The split is by use, not by size.** `Working-In-Unreal.md` carries what must already be in your
head, because its failures are silent and you would never think to look them up. This file answers
*"can I do X, and by which surface"* — thirty-odd per-capability answers nobody retains and nobody
needs to.

**How to read it.** The **capability register** below is the working section; the **dated findings**
under it are an archive reached by search, never read front to back. `Working-In-Unreal.md`'s
opening section — how to treat a limit, and the four surfaces — governs everything here and is not
repeated.

**Every claim names a surface and a date**, and `Tools/DocsCheck/claim-scan.pl` fails the build
without them. An absence with neither reads identically whether it was tested once against one
toolset or everywhere, which is the defect that produced this file.

**Silence here is not a limit.** A capability absent from the register has not been tested, not been
refuted — the standing rule that a search finding nothing proves only that the filter did not match
applies to this file about itself.

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
missing. **Python carries the system in full** *(confirmed 2026-08-27)* — 129 `IKRig`/`IKRetarget`
classes are exposed, so the absent toolset is a routing fact rather than a limit.

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

**`ProgrammaticToolset` is not that surface** *(MCP, confirmed 2026-08-22, re-confirmed 2026-08-27)*
— its sandbox refuses `import unreal` and allows exactly
`{math, datetime, re, time, json, copy}`. **A sandbox policy, not an API limit**: editor Python
reaches everything it does not, so this never falls to a wider surface — it falls to using the
right one.

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
  **The `(toolset)` mark was too narrow** *(Python, 2026-08-28)*: `composite_sections` is refused
  from Python too — *"is protected and cannot be read"*, the reflection verdict rather than an
  access one — so **`FCompositeSection::NextSectionName` cannot be read from any scripting surface**
  and whether a multi-section montage chains its sections is unknown without C++. `CompositeSections`
  is a public member with `ENGINE_API GetAnimCompositeSection(int32)` beside it, so `TheDreamEditor`
  lifts it for the cost of a build. **Deliberately not built** — the dodge's tail was capped at the
  section length instead, which makes the answer irrelevant; see the 2026-08-28 entry.
- **`UCurveFloat` and `UCurveVector` keys — the route is built** *(C++, 2026-08-28)*.
  `UTDCurveTools::SetFloatCurveKeys` / `SetVectorCurveKeys` in `TheDreamEditor` write them, and
  `GetFloatCurveMean` reads the strength-curve contract's one number. Creation stays with
  `AssetTools`; only the keys needed C++. The Python half was re-confirmed shut the same day —
  `FloatCurve` answers *"is protected and cannot be read"*, there is no `AddKey` UFUNCTION and no
  curve-editing library.
- **Bone positions in world space, live in PIE, confirmed from Python** *(2026-08-28)*.
  `SkeletalMeshComponent::GetSocketLocation` resolves bone names, not only sockets, and answers
  during a play session. With `set_global_time_dilation` it charts any bone through any event. The
  method and its sampling trap are in `Docs/Debug-Instruments.md`.
- **A root motion source can carry an authored arc, confirmed from C++** *(2026-08-28)*.
  `FRootMotionSource_MoveToDynamicForce::PathOffsetCurve` is a `UCurveVector` evaluated at the
  move fraction and rotated into the direction of travel with pitch zeroed, so its Z is world up.
  Sampled **after** `TimeMappingCurve`, so a pacing curve and an arc on one source share a time
  base and stay in step. `IgnoreZAccumulate` must be off, or the source's Z is discarded.
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
`new_object` will construct an `AnimStateNode` that nothing can register. **The clipboard route does
exist, in C++** *(refuted 2026-08-27)*: `FEdGraphUtilities::ExportNodesToText`, `ImportNodesFromText`
and `CanImportNodesFromText` are all `UNREALED_API` — `EdGraphUtilities.h:110-127`. *"Anywhere in the
API"* was a claim about two surfaces of three. Nothing needs it while `UTDStateMachineTools` covers
state creation, but a copy-paste route is there if node kinds it does not cover ever come up.

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
through MCP rather than the layer that wrote it. **Populating interiors is proven too, and this
paragraph was already wrong when it was written** *(corrected 2026-08-27)*: `1391d54`, the same day,
built the hitstun state's **sequence player** on `AS_SwordSwordAnimV3_Hit_Fw_RM` **and three
transition rules** entirely through the module, taking the machine from 15 nodes to 19 with a clean
compile — and the designer judged the flinch legible **in play** on 2026-08-25, which is the
driving-a-mesh confirmation this paragraph asked for. **The module is engine-version-coupled**;
`SpawnNodeFromTemplate` already carries a deprecating vector parameter.

**Transition direction is unreadable from Python or MCP, and C++ lifts it** *(re-tested 2026-08-27;
the Python half held, the C++ half is a header citation not a guess)*. Python sees neither
`get_previous_state` nor `get_next_state`, while `UAnimStateTransitionNode::GetPreviousState()` and
`GetNextState()` are **public and `ANIMGRAPH_API`** — `AnimStateTransitionNode.h:175-176`. So this is
a routing fact: **add five lines to `UTDStateMachineTools` when something needs direction**, and
nothing does today. — `AnimStateTransitionNode` is not a
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
`retarget_node_class`, `compile_blueprint`, `delete_node` — and **`break_pins`, which is the
disconnect function this file said did not exist** *(refuted 2026-08-27, read off the live toolset
schema; it takes an `output_pin` and an `input_pin` as `PinID`s)*. Deleting a node still breaks its
links, but it is no longer the only way. **Reconnecting an exec output replaces its existing link**, which
is how a node is spliced into a running chain without one. `read_graph_dsl` returned empty for
`ABP_Combat:AnimGraph` — use `find_nodes` with `title: ""` plus `get_node_infos`, which reports pin
connections both ways. Change a node by a **partial** write to its `Node` struct; a full write
clobbers pin-backed fields. `describe_toolset` on it is too large to return — grep a saved dump.

**Creating a Blueprint asset *is* scriptable, and the wall was never real** *(MCP, 2026-08-18)*.
`BlueprintTools.create` takes `folder_path`, `asset_name` and an `asset_type` class reference and
returns the new Blueprint — `GA_Parry` was made from `/Script/TheDream.TDParryAbility` that way,
inheriting every C++ default correctly. `set_parent` and `get_parent` reparent an existing one. This
does not contradict "AnimGraph creation is not scriptable" above, which is about *graphs*; it is the
asset that can be made.

What hid it: the snapshot recorded *"describe_toolset too large to return"* for `BlueprintTools`,
which is a fact about the **description** and says nothing about the capability. The absence was
inherited rather than measured — see the enumeration recipe at the top of this section.

**`add_variable` does not make a variable live; compiling does** *(MCP and Python alike, confirmed
2026-08-15 — it is the Blueprint's commit path, not the caller's surface, that is missing)*. It
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
thumbnail and windows. **Navigating to a state graph's interior is a C++ route, not an absence**
*(refuted 2026-08-27, after a first pass wrongly called it held by testing only Python)*.
`FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(const UObject*)` is `UNREALED_API`
(`KismetEditorUtilities.h:442`), and `FBlueprintEditor` exposes `OpenGraphAndBringToFront`,
`JumpToHyperlink` and `JumpToPin` (`BlueprintEditor.h:260-265`). Python reaches none of them —
`AssetEditorSubsystem.open_editor_for_assets` opens the asset and `BlueprintEditorLibrary` offers
only `refresh_*` — so this is **five lines in `TheDreamEditor` whenever a capture needs it**, and
asking the user to open the graph is a convenience rather than the only way.

---

## Older tooling entries still in the combat log

Findings predating this file stayed where they were rather than being moved, because entries are
append-only and other rows already point at their dates. Search `Docs/Combat-Decisions.md` for:

| Date | Entry |
|---|---|
| 2026-08-24 | The limit sweep: seven walls, one pattern, and the loop stops needing a human |
| 2026-08-24 | The state machine was never a human job, and the wall survived because every re-test used the same surface |
| 2026-08-24 | Skeleton Merge ships: two skeletons, because the pointer is read-only and prune-later is the ethos |
| 2026-08-21 | The tests a documentation audit actually runs on |
| 2026-08-15 | The chore sitting re-tests its own walls, and two of four fall |
| 2026-08-12 | The attack montage hovers because it is bound to the wrong skeleton |

**New tooling findings land here instead.** A combat entry that happens to name a tool stays in the
combat log; a finding *about a surface* belongs in this file.

---

## Dated findings — newest first

## 2026-08-28 — `/mcp` Reconnect registers a failed server mid-session

**Supersedes the entry below, filed the same day.** Its measurements hold; its scope did not. Every
route *inside* the session is still shut — but the client's `/mcp` panel carries a **Reconnect**
control, and using it delivered both servers' tools inside the turn with no restart *(MCP, confirmed
2026-08-28 — `get_current_level` answered `/Game/TheDream/Maps/L_CombatTest` through the registered
tool, matching the bridge and `run-in-editor.py`)*.

**Four server states, and one is stuck** *(Bash, 2026-08-28, CLI bundle v2.1.235)*. A
`deferred_tools_delta` attachment is recomputed each turn from the live client list and announces
tools **added**, **readded** (*"MCP server reconnected"*), **removed** (*"server disconnected"*) and
**pending** (*"will appear shortly"*). Reconnection is the MCP SDK's `StreamableHTTPClientTransport`
— `initialReconnectionDelay`, `reconnectionDelayGrowFactor`, `maxReconnectionDelay`, a *"Maximum
reconnection attempts exceeded"* ceiling — and it repairs **a stream that dropped on an established
session**. A failed initial connect never built one, which is the whole asymmetry.

**`reconnectMcpServer` is an IPC delegate method behind a UI control**, beside `setMcpServers` and
`toggleMcpServer`, guarded by `no mcpDelegate wired`. **The model cannot reach it** *(Bash, confirmed
2026-08-28 — no tool exposes it, no `claude.exe` TCP listener, no matching named pipe among 213)*.
So the move is to **ask**: *"type `/mcp`, hit Reconnect on unreal-mcp"* — specific, and it keeps the
session where a restart discards it.

**A config write does not reach the running session.** A fresh name on the same URL, added with
`claude mcp add -s local` and health-checked connected, stayed invisible and was never named even in
the failure list — the client list is built at startup, so a later entry constructs no client
*(Bash, confirmed 2026-08-28)*. `.mcp.json`'s mtime is not watched. **Whether `/mcp` Reconnect
re-reads config is untested**: this measured the session's own view, not the panel's.

**The bridge carries writes, not just reads** *(Bash, confirmed 2026-08-28)* — `list_toolsets`,
`describe_toolset`, and `call_tool` on both `get_current_level` and `SetCameraTransform`. The write
was verified through `run-in-editor.py` rather than its own return, which is `null` and settles
nothing; the camera was restored afterwards and `get_dirty_content_packages` stayed empty, so the
proof cost no package. A stale session id answers **404 / -32600 `Unknown session id ... client
should reinitialize`**, which the script retries through once.

**Why the original verdict stood**: one negative test, never re-run. **Why this correction nearly
repeated it**: every surface reachable from inside the session was tested and none worked, and
*"nothing I can reach"* was written down as *"nothing can"* — the same error, one rung up.

## 2026-08-28 — A session that starts without an editor keeps the toolset, and loses only the tools

**Superseded the same day by the entry above** — the measurements hold; the conclusion that nothing
recovers registration does not.

**The standing rule held; the conclusion drawn from it did not.** `CLAUDE.md`'s registration rule —
tools register only if the editor was open when Claude Code started — was marked *(reported twice)*
and is now **confirmed** *(MCP, 2026-08-28)*: the editor was launched mid-session, the endpoint
answered `200` within ~40 s, and `mcp__unreal-mcp__*` never appeared. Nothing reachable from inside
the session recovers it.

**What was wrong was the second half — that this leaves the session locked out of the editor.** It
does not. The plugin is an ordinary MCP streamable-HTTP server and `curl` speaks to it directly
*(Bash, confirmed 2026-08-28)*:

```bash
# initialize -- keep the Mcp-Session-Id response header; every later call needs it
curl -s -i -X POST http://127.0.0.1:8000/mcp \
  -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"c","version":"1"}}}'
# then notifications/initialized, then tools/list, then tools/call
```

Measured: `list_toolsets` returned the full registry, `describe_toolset` enumerated
`editor_toolset.toolsets.scene.SceneTools`, and `call_tool` on `get_current_level` returned
`/Game/TheDream/Maps/L_CombatTest`. `run-in-editor.py` cross-confirmed the same level from Python,
which is the check that does not go back through the layer being tested.

**Three things about the route worth keeping.** Responses arrive as plain JSON, not SSE, so no
event-stream parsing is needed. **The session id survives across separate shell calls** — one
`initialize` covers a working session. And **`toolset_name` wants the full dotted path**
(`editor_toolset.toolsets.scene.SceneTools`), which is what `list_toolsets` prints if you read the
whole line rather than the tail.

**The harness's error text is stale by construction** *(MCP, 2026-08-28)*. It replayed
`ConnectionRefused` while the endpoint was answering, because what it reports is session start.
**Anything derived from that message is a claim about the past** — the port is the only live signal,
and this is the same shape as process-up not meaning editor-ready.

**Why it was believed settled**: one negative test, never re-run, against hundreds of positive
closed-and-reopened cases. The rule it produced is right; the scope inferred from it was not — which
is the distinction this file exists to keep, *which surface* and *when*.

## 2026-08-27 — The unqualified capability claim becomes a build failure

**The designer's ruling, after three waves of refutations in one session**: *"documentation drift
corrupts planning, which is inefficient at best and sabotages at worst"* — and the docs were
*"well-documented misinformation, which is maximally harmful."* The instruction was to exterminate
the pattern within the session rather than file it.

### What the pattern actually was

Not wrong facts. **Claims that omit which surface was tried and when.** An MCP-only result reads
identically to one tested across MCP, editor Python and C++, so nothing invites a re-test and the
claim hardens into a constraint. Every instance today had that shape, and in each case the
refutation was already in the repo.

### The mechanism: `Tools/DocsCheck/claim-scan.pl`, wired as a FAIL

A block is shortlisted when it carries an absence phrase, names a **callable**, and lacks either a
surface word or an ISO date. **A FAIL rather than a WARN, deliberately** — a warning is what the
old marks already were, and they were ignored for a fortnight. The backlog was cleared first so it
starts green; anything new breaks the build.

**Three "no surface reaches this" categories satisfy it** — `engine behaviour`, `fixture behaviour`,
`machine fact`. Declaring one is a complete answer to *which surface*, because it tells the reader
not to hunt a wider one, and the date still dates it.

**Scope is the tooling docs**, not everything. `Combat-Spec`'s *"cannot"* is a gameplay rule and the
decision log's is code behaviour; neither is a claim about a scripting surface, and including them
produced 51 hits of which almost none were actionable. **The archive is excluded on principle too**:
entries are append-only and each already sits under a dated header, which is what `--working-only`
exists for.

### What it does not catch, stated so nobody trusts it further than it goes

**A capability claim phrased without a callable slips through**, and so does a wrong claim that
carries a surface and a date. This check enforces *form*, not truth — it guarantees a reader can
see what was tested and when, which is exactly what was missing, and nothing more. **It is a
heuristic with false negatives**, and calling it complete would be the pattern wearing a new hat.

### Two things the build caught while being built

`docs-check`'s manifest failed because the scanner escapes the space in `engine\ behaviour` under
`/x`, so the literal phrase the doc promises was absent from the file — **the cross-file pointer
check working exactly as designed**. And an apostrophe in a manifest row terminated the
single-quoted string and broke the script, which `bash -n` caught in one call.

### The header rung, added to the three-surface table

The table priced C++ at *"a rebuild"*, which is true of using it and false of asking whether it
would work. **Reading the engine's headers is free and settles most C++ questions** — it is how
three claims fell today. It is now a row of its own, because the rung people skip is the cheap one.

## 2026-08-27 — The limit sweep finishes: five walls fall, five hold, and the marks were the real defect

**Why it ran.** The Polish brief claimed one of the two stun tells still needed building and supplied
instructions for building it; the state had shipped 2026-08-15. Chasing that back showed the
2026-08-24 sweep had covered exactly one list — *"needs a human in the editor"* — leaving every other
recorded limit dated against an MCP-only workflow with nothing marking it as such. **The designer's
ruling: documentation drift corrupts planning, and that is not a price this project pays for speed.**

### Method

The three surfaces in the file's own order, driven through `Tools/AnimPipeline/run-in-editor.py` —
the editor's remote-execution pipe, and a far cheaper Python route than the Slate Cmd box the docs
lead with. Instrument proved first: `get_engine_version()` returned 5.8.1.

### What fell, and what held

| Claim | Verdict |
|---|---|
| *"There is no graceful quit"* | **Refuted** — `SystemLibrary.quit_editor` exists. Present, **not yet exercised** |
| *"The console is the only console route; `EditorAppToolset` cannot set a cvar"* | **Refuted** — `execute_console_command` sets one; `TD.DebugCombatTiming` 1 → 0 → 1 |
| *"The GAS inspector returns names only"* | **Refuted** — the ASC answers `get_all_abilities`, `activatable_abilities`, `find_all_abilities_with_tags` |
| *"The toolset cannot read a montage's notifies"* | **Refuted** — `AnimationLibrary`; `AM_Attack` reads one event, `MeleeWindow` |
| *"No scriptable equivalent of the revert arrow"* | **Refuted for detection** — `is_editor_property_overridden` |
| *"Transition direction is unreadable from Python or MCP"* | **Held** |
| *"`EdGraph.Nodes` refuses reflection"* | **Held** |
| *"`ProgrammaticToolset` refuses `import unreal`"* | **Held** — six stdlib modules |
| *"The attribute set cannot be written"* | **Held at two surfaces** — `AbilitySystemLibrary` has 132 members and only getters; C++ `SetNumericAttributeBase` untested |
| *"No usable Python on PATH"* | **Held** — hit again this session |

**Five of ten, not ten of ten.** The prediction going in was that every one would fall. Half is the
honest rate, and the surface-independent rules — *only play confirms an asset with a build step* —
were never at risk. **A selected sample refutes at a higher rate than a full pass**, which is what
8-for-8 on 2026-08-24 was.

### Two test-design errors worth copying

**Four probes returned nothing because the address was wrong, not because the capability was
absent** — a guessed API name where the real one differed, and the Blueprint asset where the CDO was
wanted. Each looked exactly like a wall.

**`is_editor_property_overridden` returns an enum whose every member is truthy.** Tested with
`if r:` it reported every name probed as overridden, including method names; compared against
`EditorPropertyValueState.OVERRIDDEN` it reports none, which is the true answer for both training
dummies and both PlayerStarts across nine combat properties. **A test that cannot return "no" is not
a test** — the same shape as a checker that cannot fail.

### What the marks were actually hiding

Only **7 `(toolset)` marks** existed against roughly 76 dated claims, and both of the two that were
real claims had already been converted to routing facts. Everything else asserted a limit while
saying nothing about the surface it was measured on, so an MCP-only measurement read identically to
one tested everywhere. **That silence, not any single claim, is what let a wrong limit sit inert** —
and it is why the fix is a required surface field rather than a caveat.

### The C++ surface was settled by reading headers, not by building

**Asked the same day: should the five that held be probed via C++?** Mostly no, and the reason is
the asymmetry this file already states. **At the C++ surface the probe *is* the build** — there is no
introspection call, only new code in `TheDreamEditor` and a rebuild. But **reading the engine's
headers is free and decisive**, which is how the 2026-08-24 sweep established `UEdGraph::Nodes` and
`PerformAction`. Two of the five were ever C++ questions, and both fell to a grep:

| | |
|---|---|
| `UAbilitySystemComponent::SetNumericAttributeBase` | public `UE_API`, `AbilitySystemComponent.h:233` |
| `UAnimStateTransitionNode::GetPreviousState` / `GetNextState` | public `ANIMGRAPH_API`, `AnimStateTransitionNode.h:175-176` |

**Neither was built**, per the standing economics. Both are now routing facts naming the exact
symbol, which is what a dead limit should decay into.

**The other three are closed for reasons C++ does not touch**, and saying so stops them being
re-probed: `ProgrammaticToolset`'s sandbox is a policy in the MCP plugin rather than an API limit;
"no usable Python on PATH" is a machine fact; and `EdGraph::Nodes` was already lifted at C++ by
`UTDStateMachineTools`. **Header reconnaissance is the cheap third check** the three-surface table
was missing — it costs a grep and it dates a limit properly.

### Second wave: scanning for what is still phrased as impossible

**Asked after the first pass: are any claims left that state something as not possible?** The scan
was `grep -rn -i -E "impossible|no way to|there is no |nothing can |not possible|cannot be
(read|written|saved|set|created|done|reached)" Docs/ CLAUDE.md`. Most hits are design statements or
engine facts. Six were surface claims, and **five fell**:

| Claim | Verdict |
|---|---|
| *"There is no discovery call"* for dirty assets | **Refuted** — `get_dirty_content_packages` / `get_dirty_map_packages` / `save_dirty_packages`. **The bullet four lines above it already used one** |
| *"There is no disconnect function"* in `BlueprintTools` | **Refuted** — `break_pins` is in the live schema, taking two `PinID`s |
| *"There is no clipboard or graph-text route anywhere in the API"* | **Refuted** — `FEdGraphUtilities::ExportNodesToText` / `ImportNodesFromText` / `CanImportNodesFromText`, all `UNREALED_API`, `EdGraphUtilities.h:110-127` |
| *"Inline tag containers ... read back empty"* | **Refuted for reading** — off the CDO they return real `InheritedTagContainer` / `GameplayTagRequirements` structs; the Blueprint was the wrong address. Writing still untested |
| *"Python is the only candidate route [for IK Rig] and is untested"* | **Refuted** — 129 `IKRig`/`IKRetarget` classes exposed |
| *"Nothing can navigate to a state graph's interior"* | **Held** — `open_editor_for_assets` opens the asset; nothing focuses a nested graph |

**Two of those contradicted material inside their own file**, which is the finding that matters more
than any of them. *"There is no discovery call"* sits four lines below a bullet that calls
`get_dirty_content_packages()`; *"there is no disconnect function"* sits beside a tool list that
ships `break_pins`. **Neither needed a test — only a reading.** Both survived because the enumeration
that produced them was of one toolset at one moment, and nothing re-reads a claim once it is
prose.

**The pattern across both waves: an absolute phrased without a surface is the failure mode**, not
any particular wrong fact. *"There is no X"* invites no re-test; *"the MCP layer has no X, Python
does"* dates itself and routes the reader.

### Third wave: the one I called held was the one I tested wrong

**The designer pushed back on the single survivor** — *"things can indeed navigate to a state graph's
interior, because a previous session authored the interior of the locomotion state machine using
C++."* Correct, and the pushback landed on a claim I had just re-tested and marked **held**. My test
checked Python only, one paragraph after this same entry argued that header reconnaissance is the
cheap third check. **Applying a method and skipping it in the next breath is its own failure mode.**

Refuted properly: `FKismetEditorUtilities::BringKismetToFocusAttentionOnObject` is `UNREALED_API`
(`KismetEditorUtilities.h:442`), and `FBlueprintEditor` carries `OpenGraphAndBringToFront`,
`JumpToHyperlink` and `JumpToPin` (`BlueprintEditor.h:260-265`).

**Then the neighbour, which is the worse one.** Fifteen lines above sat *"Not yet proven: populating
interiors — a `SequencePlayer` into a state, a rule into a transition — and nothing built this way
has driven a mesh in PIE."* Commit `1391d54`, **the same day that sentence was written**, created
the hitstun state's sequence player on `AS_SwordSwordAnimV3_Hit_Fw_RM` and three transition rules
entirely through `UTDStateMachineTools`, taking the machine 15 nodes to 19; the flinch was judged
legible in play on 2026-08-25. **Both halves of the caveat were false, one of them within hours.**

### The count, and what it says

Across three waves: **fourteen surface claims tested, twelve refuted, two held** — state-graph
navigation moved from held to refuted, leaving *"writing inline tag containers"* and *"no direct
attribute-base setter below C++"* as the only survivors. **The first pass' five-of-ten estimate was
too generous to the docs**, and the correction came from the designer's memory of what a past
session had actually built rather than from any search of mine.

**Every one of the three waves ended the same way**: the refutation was already inside the repo —
four lines below, in the same tool list, or in that day's own commit message. **The scarce resource
is not testing but re-reading**, and the standing rule that follows is the one this entry exists
for: *a claim about a surface must name the surface, and a claim that something was not proven must
name the date it was last checked.*

### Still untested, and not blocking

Client-world reach under PIE, and **writing** a GameplayEffect's inline tag containers — reading
them works. *(State-graph navigation was on this list and came off it in the third wave below; the
line is corrected rather than kept, since this is a working list and not a record.)*

