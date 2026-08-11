# Working in Unreal on this project

Practical notes for driving this project through the Unreal editor and its MCP toolset.
Read before writing assets or C++.

**Confidence marks.** Items tagged *(confirmed)* were observed directly and re-checked on
2026-08-09. Items tagged *(reported once)* come from a single past incident and have not
been reproduced since — trust them enough to work around, but re-test rather than treat
as settled if one blocks you or looks wrong. Do not promote a mark without re-observing
the behaviour; an unverified claim in here is worse than an absent one.

## Driving the editor itself

*(confirmed 2026-08-11, full cycle tested end to end)* **The assistant can open and close the
editor.** Sessions before this one asked the user to do both, or discovered the editor was still
running by watching a build fail. Neither is necessary.

| Step | How |
|---|---|
| Is it running? | `tasklist \| grep -i UnrealEditor.exe` |
| Open | `nohup "/c/Program Files (x86)/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "<abs path>/TheDream.uproject" >/dev/null 2>&1 &` |
| Save everything dirty | `AssetTools.save_assets` with `asset_paths: []` |
| Close | `taskkill //F //IM UnrealEditor.exe` — exits in ~2s |

**Always save-all before closing.** This is what makes a forced kill safe rather than a gamble,
and it was the user's suggestion: with nothing dirty there is no "save changes?" prompt to strand
a half-closed editor, and nothing to lose if the process dies instantly. Verified after the test
kill — no crash dump was produced, and `Saved/Autosaves/PackageRestoreData.json` read
`RestoreEnabled: false, Packages: []`, so the next launch had no restore prompt.

**There is no graceful quit.** Checked across the whole toolset registry;
`EditorToolset.EditorAppToolset` is PIE control, selection, camera and capture only, with no
quit function anywhere. So closing is always a forced kill, and **save-all covers *assets* only** —
anything the editor holds outside the asset system, such as in-progress asset-editor state, still
dies without asking. Low risk on a clean session, real risk when something half-built is open.

**Say so before closing, every time.** Opening is non-destructive and needs no announcement;
closing can destroy work only the user knows about, so it gets a sentence first.

**But it is an announcement, not a request** *(clarified by the user 2026-08-11)*. Saying "I am
about to close the editor" is itself what makes it safe — the user stops working in it the moment
they read that — so stopping again to ask permission buys nothing and costs a round trip. Announce
and proceed in the same turn. The sentence is still mandatory; only the pause after it is not.

**Process up is not ready.** The process appears within ~1s and the port can be listening while
the engine is still booting — an MCP call in that window fails with `Unable to connect`. The only
reliable readiness signal is **an actual MCP call returning a result**; poll one rather than
trusting `tasklist` or the port.

Note this project's MCP server initialises on engine startup, so a reopened editor reconnects on
its own. That is a project configuration, not engine default — do not assume it elsewhere.

**The automation makes the build check *more* important, not less.** The whole close/build/reopen
cycle can now run without the user in it, which removes the natural pause where a missing build
would have been noticed. On 2026-08-11 a fix was described, the user was asked to test it, and it
had never been compiled — they reported it still broken and were right. Run the `-newer` check
after every build, without exception.

## Before you start

**Open the editor before starting Claude Code.** `unreal-mcp` is an HTTP server hosted by
the in-editor plugin (`127.0.0.1:8000`, see `.mcp.json`), so it exists only while the
editor is running. Two cases behave differently, and conflating them has caused a wrong
correction in this file already:

- **Editor not running at Claude Code startup** *(reported twice)* — the tools never
  register for that entire session. Opening the editor afterwards starts the server, but
  the toolset stays absent and no search finds it. Restart Claude Code.
- **Editor closed and reopened mid-session** *(confirmed)* — fine. On 2026-08-09 the
  editor was closed for a full rebuild and reopened, and the tools resumed with no
  session restart. Expect a brief delay while the server reconnects; `/mcp` forces it.

The distinction is registration versus connection: tool schemas are picked up once, at
session start, and the live connection can drop and re-establish afterwards.

**This rule survives the assistant being able to open the editor itself** (see the section
above). Launching it mid-session restores the *connection*, which is the case that already
worked; it cannot retroactively *register* a toolset that was absent when Claude Code started.
So "open the editor first, then start Claude Code" still holds — what changed is that nobody has
to keep it open through a rebuild.

If asset writes are needed, confirm the tools actually respond before promising any.

**Stop PIE before compiling a Blueprint or saving an asset.** While PIE runs, actor
lookups return the `UEDPIE_0_` duplicated world's actors — which is exactly what you
want for inspecting live state, and exactly wrong for authoring.

## Building C++

**Live Coding patches exist only in the memory of the process that compiled them.** A
new `UCLASS` patched that way works until the editor restarts, then ceases to exist, and
every Blueprint parented to it fails to load. It surfaces as
`Failed to load Class /Script/... as Parent for BlueprintGeneratedClass` — which looks
like asset corruption, not a build problem, and it shows up sessions later.

Live Coding is *at best* safe for `.cpp` bodies with no reflection change. **Anything
touching reflection — new classes, new or renamed `UPROPERTY`s, new module dependencies —
needs a full editor-closed rebuild.** New module dependencies cannot be Live Coded at all;
it refuses outright.

**Live Coding crashed the editor on 2026-08-09** *(reported once)* on exactly the change
it is supposed to handle best: adding `UE_LOG` statements and two file-static helpers to
two `.cpp` files, no reflection touched, triggered just after stopping PIE. A `patch_0`
exe and pdb were left in `Binaries\Win64`, so the compile appears to have succeeded and
the crash came during injection. Nothing was lost — source, assets and git were all
intact after restarting.

Treat Live Coding as a convenience that can cost you the editor, not as the cheap path.
If the change matters or the editor holds unsaved work, prefer the full rebuild.

State up front, before starting, whether a change touches a header. That single fact
decides whether the editor has to close, and it is easy to lose track of mid-discussion.

**Build from Bash, not PowerShell** *(confirmed 2026-08-10)*. Every PowerShell tool call
rewrites `HKCU\Console\%SystemRoot%_System32_WindowsPowerShell_v1.0_powershell.exe`, clobbering
the user's console font: it writes `FaceName` (forced to Lucida Console) without writing
`FontSize`, so both the face and the size reset. It is not the build or `dotnet` that does it —
a bare `Get-ItemProperty` reproduced it — so it is the tool invocation itself, every time.

`Build.bat` cannot be called from Git Bash: the space in `C:\Program Files (x86)` survives every
quoting form tried, including `cmd //c` with an explicitly quoted path. Skip the batch file and
call UnrealBuildTool directly, which is all `Build.bat` does after its lock and dotnet lookup.
Use the **bundled** dotnet — the system one tops out at .NET 9 and UBT needs 10:

```bash
cd "/c/Program Files (x86)/UE_5.8/Engine/Source" && \
"/c/Program Files (x86)/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "C:/Program Files (x86)/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  TheDreamEditor Win64 Development \
  -project="C:/Users/rross/Documents/Unreal Projects/TheDream/TheDream.uproject" -waitmutex
```

The CWD matters: UBT must run from `Engine/Source`.

Roughly 15–30s incremental. **Verify by confirming `Binaries\Win64\UnrealEditor-TheDream.dll`
is newer than every source file** — not merely that the build reported success. From Bash:

```bash
# Empty output means the DLL is newer than every source. This is the check.
find Source \( -name "*.cpp" -o -name "*.h" \) -newer Binaries/Win64/UnrealEditor-TheDream.dll -print
ls Binaries/Win64/UnrealEditor-TheDream.patch_* 2>/dev/null || echo "no patch files"
```

Use `-newer` rather than eyeballing timestamps. Two things make a manual comparison lie, and both
were hit within an hour of writing this down: `ls` and `find -printf %T` report in **different
timezones** here (local vs UTC, a six hour gap), and a `%TH:%TM:%TS` format omits the **date**, so
a file from yesterday reads as newer than a binary built minutes ago. `-newer` compares mtimes
directly and is immune to both.
`UnrealEditor-TheDream.patch_*` files accumulate from Live Coding runs; sweep them after
a full rebuild so the binary state is unambiguous.

Batch all the C++ for a slice while the editor is closed, do one rebuild, then open the
editor once and stay in it for the whole content pass.

## Editing assets through the toolset

**Read a property off the asset rather than guessing from filenames** *(confirmed 2026-08-11)*.
Looking for the mesh's physics asset, `find -iname "*PHYS*"` returned nothing across all of
`Content/`, which reads as "there is no physics asset" and is wrong — it is
`PA_Mannequin`, under the bundle's `Mannequins/Rigs/`. `ObjectTools.get_properties` on
`SKM_Manny` returned the reference immediately. This is the standing absence rule in its
tooling form: **a naming-convention guess is a filter, and a filter that misses proves
nothing.** The call shape is easy to get wrong too — the schema is
`{"instance": {"refPath": "..."}, "properties": [...]}`, not `object_path`/`property_names`;
calling it wrong returns the full input schema, which is the fastest way to learn any of these.

**Verify against the artefact, not the tool's return value.** Two of these tools have now
reported success while changing nothing that mattered, in different ways. Whatever the
write was meant to produce — a value some system actually reads, a file gone from disk —
check *that*, and prefer a check that does not go back through the API that did the
writing. A read-back through the same layer can confirm a write that never landed.

**For assets with derived data, the artefact check is not enough either** *(confirmed
2026-08-11)*. `BS_SwordShield_Locomotion` had its 27 `SampleData` entries written through
`set_properties`. The write returned true, the read-back was correct, the `.uasset` grew to
18 KB on disk, the binary physically contained every clip reference, and the samples appeared
correctly positioned on the grid when the asset was opened. **Every available check was green
and the blendspace still produced no pose at all** — the character T-posed the instant it
moved, while an Idle state fed by a plain `SequencePlayer` on the same skeleton worked
perfectly.

A BlendSpace stores the authored `SampleData` *and* a derived interpolation grid built from
it. Reflection writes populate the list and never trigger the rebuild, so there are samples
and nothing to interpolate between. `GridSamples` is not readable through this layer, so the
gap is invisible from outside.

The tell was the asymmetry: **a node with only an asset reference worked, a node with computed
data did not.** The fix is to open the asset in the editor, make any real edit (nudge a sample
and put it back), and save — which regenerates and serializes the grid.

So the rule generalises: **binary presence proves a write landed, not that anything derived
from it was rebuilt.** Suspect this for any asset type that caches computed data — blend
spaces, and probably anything else with a build step. Only play confirms those.

**`ObjectTools.set_properties` can return true, and `get_properties` can read the value
back intact, while the write accomplishes nothing** *(reported once, twice in one
session)*. Round-trip verification is the obvious check and it is not sufficient — the
write lands, just somewhere nothing reads.

**A Blueprint CDO property set this way is not live in the current editor session**
*(confirmed 2026-08-10)*. `bBlockedWhileAirborne` was set on `GA_Dodge`'s CDO, read back true,
saved, and confirmed changed on disk by `git status` — and PIE ignored it completely, twice, in
a clean session started afterwards. The editor was then closed for an unrelated rebuild, and on
reopening the same asset and *functionally identical code* the rule worked immediately. Nothing
about the logic changed between the two runs; only the restart did.

So the practical rule: **after setting a property on a Blueprint CDO, restart the editor (or
recompile the Blueprint) before believing anything you see in PIE.** All three of the obvious
verifications — the return value, the read-back, and the file on disk — were green while the
running game used the old value, which makes this strictly worse than the trap above: there is
no cheap check that catches it. Only play does.

**Reproduced deliberately the same day, on a second and much older property.** `DodgeSeconds` was
changed 0.5 → 0.3 through `set_properties`, read back as 0.30000001, and confirmed changed on
disk; PIE was then started in that same session and the ability's own trace reported
`want=0.500s` on **eleven consecutive dodges**. This was run specifically to challenge the rule
after the first observation, and it confirmed it instead — and rules out the tempting narrowing
that only *newly added* properties are affected. `bBlockedWhileAirborne` was new in its build;
`DodgeSeconds` had existed for days. Both failed identically.

Note what made the second test conclusive where feel would not have been: the `DODGE` trace
prints `want=<DodgeSeconds>` as the *running ability* reads it. Judging a 0.2s difference by hand
is exactly the kind of thing a person will talk themselves into either way. Where a value drives
behaviour, print the value.

The mechanism was not proven. The likely candidate is Blueprint reinstancing rebuilding the CDO
from serialized data and discarding the in-memory write, which would explain why the value
survived to disk but not to runtime. Do not treat that explanation as settled; treat the rule as
settled.

**The cheap way out:** set the value in the editor's details panel by hand. *(confirmed
2026-08-10)* `DodgeSeconds` was changed 0.3 → 0.4 that way and the very next PIE session reported
`want=0.400s`, with **no editor restart** — MCP stayed connected throughout, so the editor
demonstrably never went down.

That is the precise difference, and it is worth stating as a contrast because the two look
identical from outside:

| Change made by | Takes effect after |
|---|---|
| Details panel | the next **PIE** restart |
| `set_properties` | the next **editor** restart — a PIE restart is not enough |

The PIE-restart-is-not-enough half is not an assumption: the eleven dodges that reported
`want=0.500s` were in a PIE session started *after* the `set_properties` write.

So reserve `set_properties` for cases where a human edit is impractical, prefer the panel for
anything a designer would touch anyway, and restart the editor before trusting a programmatic
write.

This cost a false bug investigation — an hour spent believing the airborne check was broken when
it was correct from the moment it was written. The tell, in hindsight: *the same code behaved
differently across an editor restart with no rebuild in between.* That is never a logic bug.
When configuring an asset type for the first time, **diff it against a known-good asset
of the same type.** Comparing `IMC_Combat` against the stock `IMC_Default` is what
exposed the input bug; nothing about our own asset looked wrong in isolation.

Confirmed traps:

- **InputMappingContext** *(confirmed)* — UE 5.8 evaluates `defaultKeyMappings.mappings`.
  The top-level `mappings` array still exists and accepts writes, but nothing reads it. A
  mapping written there silently never fires.
- **GameplayEffect modifier attribute** *(reported once)* — setting `attributeName` +
  `attributeOwner` leaves the underlying `FProperty` pointer null, so the effect applies
  and modifies nothing (`LogAbilitySystem: Warning: <GE> has a null modifier attribute`).
  Re-picking the attribute in the details panel fixes it. The `attribute` field reads
  `/Script/TheDream.TDAttributeSet:Health` when correct, empty string when broken.
- **Object references need the full path** *(confirmed)* — `/Game/Path/Asset.Asset`, not
  `/Game/Path/Asset`. This one at least errors rather than failing silently.
- **Array edits** *(confirmed 2026-08-10)* — changing an existing element and adding one in
  the same call fails with `ArrayAdd: elements changed alongside the size change; insertion
  points are ambiguous`. It errors rather than failing silently, and it aborts only that
  property while others in the same call still apply — so a partial write is the likely
  state afterwards. **Set the container to empty first, then write the full contents**, as
  two calls. Applies to `FGameplayTagContainer` too, where the array is `gameplayTags`.
- **TMap keys** *(reported once)* — setting a map property logs `added key ... not found
  in map after import` while the entry is in fact correct at runtime. Misleading, not
  fatal.
- **`AssetTools.save_assets` can reject a path that demonstrably exists** *(reported once,
  2026-08-10)* — it failed with `Asset does not exist:
  /Game/TheDream/Combat/Abilities/GA_Attack` while `exists` returned true, `find_assets`
  listed that exact path, and the `.uasset` was on disk. Both the package form and the
  `Package.Object` form failed. `is_dirty` failed the same way on the same path, and the
  same call had succeeded earlier in the session. **Passing an empty list — save every dirty
  asset — works**, so use that as the fallback. Note it also saves anything else left dirty,
  which is worth a `git status` afterwards to see what actually got written.
- **A GameplayEffect's inline tag containers cannot be written** *(confirmed 2026-08-10)* —
  `inheritableOwnedTagsContainer` and `ongoingTagRequirements` are present in reflection and
  accept a write without error, but read back **empty**. UE 5.8 has moved this behaviour to
  `gEComponents` (`UTargetTagsGameplayEffectComponent`,
  `UTargetTagRequirementsGameplayEffectComponent`), and the inline properties are vestigial.
  Numeric and enum properties on the same asset — `durationPolicy`, `period`, `modifiers`,
  even the modifier's attribute path — write fine, so a partially-configured effect is the
  likely outcome if you do not check. **Adding a GEComponent is not scriptable**; it needs a
  human in the details panel. Read tag containers back after writing them, always.
- **A placed actor can hold stale `EditDefaultsOnly` values that silently override its Blueprint**
  *(confirmed 2026-08-10)* — the placed `TrainingDummy` in `L_CombatTest` read
  `DefaultAbilities: []`, `StaminaRegenPausedTag: None` and `ExhaustedTag: None` while the
  Blueprint CDO held the correct values, so at runtime it was granted **no abilities at all** and
  could not attack. The instance was showing the *C++ class defaults*, which is the signature:
  the actor was placed before the Blueprint authored those values, and the old values serialized
  into the level as overrides.

  What makes it nasty is that it is unreachable. `EditDefaultsOnly` properties are not editable
  on an instance, so the details panel does not show them and `ObjectTools.reset_properties`
  fails on exactly those names while succeeding on `EditAnywhere` ones in the same call — which
  is also the cheapest way to *confirm* this diagnosis. **The fix is to delete the placed actor
  and re-place it from the Blueprint**; fresh serialization inherits the CDO. Note the transform
  and label first.

  It hides for a long time, because a dummy only needs to *receive* damage until the first slice
  that needs it to act. Whenever a placed actor behaves as though its Blueprint were empty, read
  the instance and the CDO separately and compare — `GetGrantedAbilities` returning `[]` against
  a populated `DefaultAbilities` is the tell.
- **`AssetTools.exists` false-negatives, and `AssetTools.duplicate` fails with a bare `false`**
  *(confirmed 2026-08-10)* — `exists` returned **false** for
  `/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run` while `find_assets` listed that
  exact path, `ObjectTools.get_properties` read its contents in full, and `load_asset` resolved
  it and handed back a valid object. It returned false for a folder holding a committed asset
  too. Both the package form and the `Package.Object` form behaved identically.

  `duplicate` failed the same way on the same asset in both path forms — return value `false`,
  no error, nothing written. Since `load_asset` proves the source resolves, the path is not the
  problem, and there is no diagnostic to work from.

  **Use `load_asset` as the existence check** — it returns a usable object or errors, which is
  a real answer. Treat `exists` as advisory only, and never conclude an asset is missing from
  it. For duplication, have a human copy the asset in the content browser; writing *into* the
  copy through `ObjectTools` works fine, so the split is creation by hand, authoring by script.

- **`AssetTools.delete` is inconsistent about removing the `.uasset` from disk**
  *(confirmed 2026-08-10, both ways)* — deleting `GA_LightAttack` cleared the registry and
  left the file untouched with its original timestamp, which would have resurrected the
  asset on the next directory rescan; `exists` still returned true while `find_assets` no
  longer listed it. Deleting `GE_StaminaRegen` and `GE_StaminaRegenPause` the same day
  removed both from disk properly. The difference was not identified — the deleted assets
  differed in age, in whether they had ever been loaded, and in whether they were committed.
  **So always check the directory afterwards** and `git rm` only what is actually still
  there. Never assume either outcome.

## Not scriptable at all

*(reported once)* These need a human in the editor:

- Creating levels (`SceneTools` loads but cannot create)
- Creating AnimMontages
- Placing or configuring AnimNotifies on a montage timeline — a montage's `notifies`
  property is not even readable, so notify placement can only be verified at runtime
- **Creating** BlendSpaces and AnimBlueprints. `AssetTools` has no create-asset function at
  all, and `duplicate` fails with a bare `false` (see the trap above). A human makes the empty
  asset; everything after that is scriptable.

This is why renaming an AnimNotify class is expensive: placed notifies serialize against
the class path, so a rename breaks them and they must be re-placed by hand.

### AnimGraph *editing* is scriptable *(confirmed 2026-08-11)*

Only asset **creation** needs a human. Graph editing does not.

`BlueprintTools` has `list_graphs`, `find_nodes`, `get_node_infos`, `create_node`,
`connect_pins`, `set_pin_value`, `retarget_node_class`, `read_graph_dsl` / `write_graph_dsl`,
`compile_blueprint`.

- `list_graphs` addresses state machines and individual states, e.g.
  `ABP_Combat:AnimGraph.AnimGraphNode_StateMachine_0.Locomotion.AnimStateNode_2.Walk / Run`.
- `find_nodes` needs a `title`; pass `""` to list everything in a graph.
- Change a node through a **partial** write to its `Node` struct —
  `{"Node":{"blendSpace":{"refPath":"..."}}}`, or `Node.sequence` for a Sequence Player. Partial,
  not full: the full struct contains pin-backed fields like a blendspace's `x`/`y`, and writing
  those risks the connections.
- `describe_toolset` on it returns ~72k chars and is rejected as too large. Grep the saved dump
  for `"name":"..."`, and get argument schemas by calling a function wrong — the error returns
  the full input schema.

## Verifying combat changes

After any structural change — a refactor, a type change, a system swapped out —
**re-verify everything that previously worked, not only the thing that changed.**
Structural changes break things silently and at a distance: migrating ability input from
an integer enum to gameplay tags wiped the `AbilityInputActions` map on
`BP_PlayerCharacter` purely because the value type changed, and nothing announced it.

Name the checks up front, run them in one session, report as a pass/fail table. The
standing set for combat work:

- Damage lands in exact expected multiples, not just "a bar moved"
- Abilities still grant
- Abilities end cleanly (`bIsActive: false` at rest)
- **No stuck state tags** — `State.Attacking` is activation-blocking, so a leaked copy
  silently disables all future attacks. `State.Attacking.Committed` and `State.Dodging` are
  worse: a leaked `Committed` forbids every future defensive action, and a leaked
  `State.Dodging` leaves the character permanently invulnerable.
- Locomotion and jump still work, whenever input or movement code was touched. (This used to
  read "template locomotion"; since 2026-08-11 it is our own `ABP_Combat` and
  `BS_SwordShield_Locomotion`, not Epic's.)
- **The light attack still plays its montage**, whenever anything touches meshes, skeletons or
  animation assets. The character's mesh sits on `GDHBundle`'s skeleton while
  `AM_LightAttack_01` is bound to Epic's, and it only plays because GDH's `SK_Mannequin` holds
  a reverse `CompatibleSkeletons` entry. Lose that and the attack stops **silently** — the
  ability still runs and still logs its timing, because `LogTDCombatTiming` reports the
  ability's own state. The tell is the absence of `RELEASE BEGIN`/`END`, which come from a
  notify on the montage and therefore only fire if the montage is really playing.
- `LogAbilitySystem` is free of new warnings
- **Death and revive leave nothing stranded**, whenever movement, regen or ability state is
  touched. Die *in mid-air* specifically: `DisableMovement` stops the fall, so `Landed()` never
  fires, and anything keyed to landing stays set forever — silently, past the revive. Check
  stamina regen still runs afterwards. Also confirm no `State.Attacking.Committed` survives a
  death that cancelled a swing, since a leak there forbids every future defensive action.

Once the stamina economy is involved, add:

- **Stamina lands on exact values.** A dodge from full reads exactly 50, not "about half".
- **Regen resumes at the right moment** — suppressed for the action's duration *plus*
  `StaminaRegenPauseSeconds` measured from when it ended, then 25/s.
- **Exhaustion triggers at zero and releases**: `State.Exhausted` appears, defensive actions
  and jump refuse, and it clears **when stamina reaches Max** — not on a timer. Regen must
  *continue* while exhausted; it is the only thing that can end it, so if it does not,
  exhaustion is permanent rather than merely long.
- **Attribute base values are clamped, not just current values.** Read `baseValue` as well as
  `currentValue` from `GetAttributeValues`. A base that has drifted above Max is invisible on
  the bar and makes every cost read wrong — stamina base reached 105 against a displayed 100,
  so a dodge from "full" left 55 instead of 50, and two dodges never reached zero.
- **Costs never gate.** Dodging below the cost must still work and empty the bar. If an
  action silently does nothing at low stamina, `CostGameplayEffectClass` has crept back in.

Most of this is checkable without UI via `AbilitySystemInspectorToolset`
(`GetAttributeValues`, `GetGrantedAbilities`, `GetActiveTags`) against the `UEDPIE_0_`
actors while PIE runs — ask for the session to be left running rather than stopped.

**Those calls are separate round-trips, so a snapshot can straddle a state change.** An
ability reading `bIsActive: false` alongside a live `State.Attacking` looks exactly like a
leaked tag and is usually just sampling skew. Take several samples before believing one.

## Diagnosing timing

`TD.DebugCombatTiming 1` turns on a per-attack trace of the attack phase model: windup
rate wanted versus applied, coil start and derived rate, commit position and target, and
each release window edge with the montage position it fired at. Off by default, costs
nothing when off.

**Reach for it early.** Every real bug in that system was found by measuring, and
reasoning about play rates on paper mis-diagnosed several of them confidently — including
one case where the "fix" was applied to a claim that had not actually been falsified.
Where possible, prefer an experiment that *manipulates* the suspected cause (moving the
symptom onto a branch that currently works) over one that merely observes it.

Two warnings on that category are deliberately ungated, because both describe an attack
that silently stops dealing damage rather than crashing: a skipped coil, and a
`ReleaseStartSeconds` that has drifted from the Release Window notify it is copied from.

**`BUFFER` lines trace the input buffer** *(added 2026-08-11)*, on the same cvar: `stored` when a
refused press is remembered, `released after Nms held`, `fired Nms late` with whether the button
was still down, `expired`, and `dropped, superseded by` when a different press replaced it.

Read them before believing an input was dropped — `expired` and no line at all mean different
things, and the first is a window question while the second means the press never reached the
character. Two traps when reasoning about them:

- **A held buffer does not expire, but it does not wait either.** It fires at the first
  opportunity, so holding a button through a lockout will not keep a buffer parked for you to
  test against. Reproducing `superseded by` needs a block that outlasts human reaction, and
  today exhaustion is the only one — dodge twice to zero, then hold dodge and press attack.
- **The hold duration in `released after Nms held` is the value that matters**, not the fact of
  a release. It is bounded by how long the *block* lasted, not by `InputBufferSeconds`, so it
  can exceed a tier boundary. Printing it is what caught a 236 ms hold being flattened to a
  light.

**A montage's `compositeSections` is invisible to the toolset, so segments can be repointed by
script and sections cannot** *(confirmed 2026-08-11)*. `slotAnimTracks[].animTrack.animSegments[]`
reads and writes fine — swapping all eight `AM_Dodge` clips to another pack took one call. But
`compositeSections` is neither readable nor writable, and `sequenceLength` is **read-only** and
does not recompute after a reflection write.

That combination has a specific and ugly failure mode. Swapping 0.733 s clips for 0.833 s ones
moved every segment while the section markers stayed at the old spacing, producing a **cumulative
0.1 s drift**: section 0 was correct, section 1 started 0.1 s into the wrong clip, and by section
7 it was 0.7 s adrift and played almost entirely the *previous* direction's animation. In play
this reads as "forward is fine, and it gets progressively worse round the compass" — which sounds
like an animation quality problem and is an arithmetic one.

**The tell is in the trace, not the animation:** `DODGE` prints `sectionLen=`, which read 0.733
while the segments were 0.833. Any montage whose section length disagrees with its segment length
is misaligned. Note the derived play rate is computed *from* `sectionLen`, so it was wrong too and
corrected itself once the sections were fixed.

**So: script the segments, then have a human place the sections** — or rebuild the montage
outright, which is cleaner when the length has gone stale, since sections cannot be placed past
an end that never recomputed.

**An automated PIE run is one fixed spawn position, so "no damage landed" is not evidence about
hit detection** *(confirmed the hard way, 2026-08-11)*. `StartPIE` spawns both characters at
their placed transforms and nobody moves. After the melee trace moved from `hand_r` to a 100 cm
blade, four swings over a 12 s warmup dealt zero damage — and the inference drawn from that was
that the mechanism was broken. It was not: **the user repositioned and both characters killed
each other immediately.** Moving a hitbox changes *where* it is, and a fixed spawn distance that
used to connect need not still connect.

The general form, and this is the second costume the same error wore in one session: **a single
fixed test configuration is a filter.** Absence of an effect inside it says nothing about the
mechanism, exactly as a filtered search says nothing about existence. Before concluding hit
detection is broken, either move something or turn on `bDrawDebugTrace` and look — do not reason
from a null result in a scene where nothing can move.

**`GetLogEntries` returns a *window*, and a mixed-frequency pattern makes it lie about absence**
*(confirmed 2026-08-11, the hard way)*. `maxEntries` is taken from the **end** of the log, so a
pattern combining a high-frequency event with a low-frequency one spends the whole budget on the
noisy one. `DODGE|BUFFER|DEATH|REVIVE` at `maxEntries: 60` returned a window containing **2**
dodges; the same query as `DODGE` alone at `maxEntries: 0` returned **30**, covering all eight
directions. A regression was reported as half-untested on the strength of the first.

**So: one pattern per event class, and `maxEntries: 0`, whenever the question is "did this ever
happen".** A capped mixed query answers "what happened recently", which is a different question
and looks identical. This is the standing absence rule in its logging form — the filter was mine,
and a filter that misses proves nothing.

**Not every state is traced, so check what the trace covers before reading anything into its
silence.** `TD_TIMING_LOG` emits ACTIVATE, COIL START, ESCALATE, COMMIT, RELEASE, DODGE,
DODGE END, BUFFER, REFUSED, DEATH and REVIVE — and **nothing for exhaustion**. Absence of
exhaustion from the log is not evidence it did not occur; it is evidence nobody logs it. Confirm
exhaustion with `GetActiveTags` during PIE, or infer it from a `BUFFER ...Dodge: expired`, which
is what a dodge pressed while exhausted produces. The list is greppable:
`grep -rn "TD_TIMING_LOG" Source/`.

**The `RELEASE BEGIN`/`END` lines can report the wrong montage** *(found in review 2026-08-10)*.
`AnimNotifyState_MeleeWindow` logs via `GetCurrentActiveMontage()` rather than the attack montage,
so if anything else is playing at higher priority — a dodge cancelling an attack, now that
`AM_Dodge` exists — the position and rate belong to that montage instead. The `DODGE` and
`COMMIT` lines come from the abilities themselves and are unaffected. Cross-check against those
before trusting a release-edge position that looks impossible.

## Running git

**Push through the Bash tool, never PowerShell.** Bash reaches Git Credential Manager and
authenticates to `github.com/Ronguat/TheDream`; the PowerShell tool runs with
`GIT_TERMINAL_PROMPT=0`, the helper returns nothing, and it fails with
`could not read Username for 'https://github.com'`. This is a second, independent reason to
prefer Bash on top of the console-font one above. `gh` is not installed, so no `gh` commands.

A failure in one shell is not a statement about capability: on 2026-08-09 the PowerShell failure
was generalised to "I cannot push" and the user was told to push themselves, which was wrong —
`git push --dry-run` from Bash authenticated immediately.

**A push can hang while the commit has already landed** *(observed 2026-08-10)*. If `git push`
times out, check `git rev-list --left-right --count origin/main...HEAD` before assuming failure;
the commit is usually present and only the transfer stalled. Re-running the push is safe.
