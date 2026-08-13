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

**Always save-all before closing, and then check `git status` before pulling the trigger.**
Calling `save_assets` is not the check — *seeing the files listed as modified* is. On 2026-08-12 a
`set_properties` write to `GA_Attack`'s CDO was lost entirely because the editor was killed
without a save, and the loss was invisible: the values read back correctly right up until the
restart, and the next play session silently used the old timings. The user detected it from
gameplay feel before any tooling did.

**Two failure modes wear the same face and only the disk check separates them.** A write that is
*saved but not yet live* needs a restart and is perfectly safe. A write that was *never saved* is
already gone. Both look identical from inside the running editor, because the in-memory value
reads back fine either way. `git status` is the only thing that distinguishes them, it costs one
command, and it must happen **before** the kill rather than after.

This is what makes a forced kill safe rather than a gamble,
and it was the user's suggestion: with nothing dirty there is no "save changes?" prompt to strand
a half-closed editor, and nothing to lose if the process dies instantly. Verified after the test
kill — no crash dump was produced, and `Saved/Autosaves/PackageRestoreData.json` read
`Packages: []`, so the next launch had no restore prompt. **`Packages` is the field that matters,
not `RestoreEnabled`** — this was first recorded as `RestoreEnabled: false` and read `true` after an
equally clean kill on 2026-08-12, so that flag is not a stable signal. An empty package list is what
means nothing was stranded.

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

`SceneTools.get_current_level` is the cheapest probe — no arguments, and its answer is worth having
anyway. **Poll it rather than sleeping a fixed interval** *(the rule was already here and was not
followed on 2026-08-12: two reopens each blind-waited 40s, and boot is nearer 30)*. A blind wait is
wrong in both directions — it wastes the difference when boot is quick, and returns too early when
something makes it slow, which is the case that produces a confusing `Unable to connect` rather than
a slow success.

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

**And exactly wrong for measuring a placement** *(confirmed 2026-08-13)*. A PIE actor's transform is
where it has *ended up*, not where it was placed: it has settled under gravity, and anything that
can be pushed has been pushed. `BP_TrainingDummy` read `x=175.81, z=98.15` in a live PIE world and
`x=200, z=100` in the editor world — a 24 cm drift accumulated during play. The PIE figure was
written up as a correction to a documented 200 cm spacing, and the documentation was right.

The tell is available and easy to miss: `z` had also moved, by the couple of centimetres a capsule
settles. **If z is not the placed value either, nothing about that transform is the placed one.**
The `UEDPIE_0_` prefix is in the ref path of every such reading, so the check costs nothing —
**re-read it in the editor world before writing any placement number down.**

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

**The repair, now that it is known** *(2026-08-12 — the user's preset is **Consolas, size 14**)*.
Nothing on disk records what the font was, so this is the value to restore, not a value to
derive. From Bash:

```bash
K="HKCU\Console\%SystemRoot%_System32_WindowsPowerShell_v1.0_powershell.exe"
reg add "$K" //v FaceName   //t REG_SZ    //d Consolas //f
reg add "$K" //v FontSize   //t REG_DWORD //d 917504   //f   # 0xE0000: height 14, auto width
reg add "$K" //v FontFamily //t REG_DWORD //d 54       //f   # 0x36, TrueType
```

`FontSize` packs height in the high word and width in the low word, so 14pt is `14 << 16`.
Only **new** console windows read it. **Note `//v` rather than `/v`** — MSYS rewrites a leading
single slash as a path and `reg` fails with a bare `Invalid syntax`; this is the same escaping
`taskkill //F //IM` needs.

Before this was worked out, a search across `Docs/`, `CLAUDE.md` and every commit
(`git log -S "FaceName" --all`, `-S "font" --all`) found no repair recorded anywhere — only the
prevention. Hence writing it down.

**"Bash cannot do this" is nearly always wrong, and believing it is what breaks the rule.** This
is the failure mode in practice — not forgetting the rule, but hitting something Bash seemed
unable to do and reaching for the other shell. It happened on 2026-08-12 decoding a base64 PNG
out of a large MCP result. **There is no Python on PATH** *(machine fact, hit twice)*:
`python -c ...` returns the Microsoft Store shim message. But Git Bash ships a full toolbox, and
these are all confirmed present here:

| Need | Use |
|---|---|
| Decode base64 | `base64 -d`, at `/usr/bin/base64` |
| Read or write the registry | `reg query` / `reg add` |
| Hex dump, byte slicing | `xxd`, `cut -c`, `tr` |
| Certificates, base64 fallback | `certutil` |
| HTTP | `curl` |

Check for the binary before concluding Bash cannot do something. The cost of being wrong is a
clobbered font every time, and it is paid by the user, not by the session that caused it.

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

**The rule is about Blueprint CDOs, and does not extend to plain assets** *(confirmed 2026-08-12)*.
`bEnableRootMotion` was written `false` on an AnimSequence through `set_properties` with no editor
restart, and the very next PIE session behaved accordingly — the character provably stopped
travelling during its attack. Both observations behind the restart rule were Blueprint CDO
properties (`bBlockedWhileAirborne`, `DodgeSeconds`), which fits the suspected mechanism of
Blueprint reinstancing discarding the in-memory write. A plain `UObject` asset has no reinstancing
step, and empirically the write is live immediately.

**This is worth knowing precisely because of what a null result means.** When the evidence you are
collecting is *"I changed it and the symptom did not move"*, the restart rule makes that ambiguous
between a refuted hypothesis and a write that never landed. **Prove the instrument first: make one
write whose effect is numerically measurable, confirm it, then trust the null results that follow.**
Disabling root motion is a good probe for animation work — the actor visibly stops travelling, which
is a number, not an impression.

**`reset_properties` resets to the property's *default*, not to the inherited archetype value**
*(confirmed 2026-08-12)*. Called on a Blueprint CDO's mesh component to clear a `relativeLocation`
override and let the C++ default apply, it wrote `(0, 0, 0)` — not the parent's value, and further
from correct than the override it removed. There is no archetype-aware reset in the toolset; the
details panel's yellow revert arrow has no scriptable equivalent. **Set the inherited value
explicitly instead**, which also stops it serialising as an override, since a CDO records only
deltas from its parent.

**A component property set in C++ reaches nothing if a Blueprint or a placed actor overrides it**
*(confirmed 2026-08-12)*. Moving the mesh from Z −90 to −96 in `ATheDreamCharacter`'s constructor
changed nothing in PIE: `BP_PlayerCharacter` and `BP_TrainingDummy` each carried a serialized −90
on their **inherited** mesh component, and the placed dummy in `L_CombatTest` carried a third copy.
Three writes were needed. This is the same family as the stale `EditDefaultsOnly` trap below, but
milder — a component transform is `EditAnywhere`, so a direct write sticks and the actor does not
have to be deleted and re-placed. **After any C++ default change to an inherited component, read the
value back off a live PIE actor before believing it took.**

**But check the running value before spending an editor restart on this rule** *(2026-08-12)*.
`ReleaseStartSeconds` was written 0.3046 → 0.30 through `set_properties`, saved, and **not**
restarted — and the ability's own `COMMIT` line reported the derived windup rate as **1.500**
(0.30 ÷ 0.20) rather than 1.523. The new value was live. The user had opened the Blueprint editor
in between, which can recompile and reinstance, so this does **not** overturn the rule above: it
is one case where the rule's remedy was unnecessary, with a confound that cannot be separated
after the fact.

The transferable part is the method, not the result. **Where a derived value is already printed,
reading it settles the question faster than reasoning about which write path was taken** — this
took one PIE run against an argument that was heading for a full editor cycle. It is the same
principle that made `DodgeSeconds` diagnosable: *where a value drives behaviour, print the value.*

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

**But creation is per-toolset, not a blanket limitation** *(confirmed 2026-08-12)*. `AssetTools`
having no create function says nothing about the others. `MaterialTools.create_material`,
`create_function`, `create_parameter_collection` and `MaterialInstanceTools.create` all work, and a
whole material graph can be built end to end — `add_expression`, `connect_expressions`,
`connect_to_output`, then `recompile`, which raises if the shader fails. `M_TestFloorGrid` and its
instance were authored that way with no human in the loop. Check the toolset that owns the asset
type before concluding a thing cannot be made.

This is why renaming an AnimNotify class is expensive: placed notifies serialize against
the class path, so a rename breaks them and they must be re-placed by hand.

### AnimGraph *editing* is scriptable *(confirmed 2026-08-11)*

Only asset **creation** needs a human. Graph editing does not.

`BlueprintTools` has `list_graphs`, `find_nodes`, `get_node_infos`, `create_node`,
`connect_pins`, `set_pin_value`, `retarget_node_class`, `read_graph_dsl` / `write_graph_dsl`,
`compile_blueprint`, and **`delete_node`** *(confirmed 2026-08-12, takes `{"node": {"refPath": …}}`)*.
There is no disconnect function — `disconnect_pins` and `break_pin_links` do not exist — but
deleting a node breaks its links and leaves the downstream pin on its literal, which is usually what
you want anyway.

`read_graph_dsl` returned an **empty string** for `ABP_Combat:AnimGraph` while `find_nodes` with
`title: ""` listed all ten nodes. Use `find_nodes` + `get_node_infos` to read a graph; the latter
returns each pin's connections in both directions, which is what you need to establish node order.

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
- **The attack still plays its montage**, whenever anything touches meshes, skeletons or
  animation assets. The tell that it is *not* playing is the absence of `RELEASE BEGIN`/`END`:
  those come from a notify on the montage, so they only fire if the montage really ran, while
  everything else in `LogTDCombatTiming` reports the ability's own state and looks perfectly
  healthy either way. **An attack that silently deals no damage is the failure mode here.**

  *This used to warn that `AM_LightAttack_01` played only by grace of a reverse
  `CompatibleSkeletons` entry, our mesh being on `GDHBundle`'s skeleton and the montage on
  Epic's. Discharged 2026-08-12: `AM_Attack` is built from its clip and so carries GDH's
  skeleton directly. The check stays because the silent-failure shape does — any skeleton or
  mesh change can still break montage playback without a single error.*
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

## Diagnosing what you can see

**The level viewport is an instrument, and it is the cheapest one here** *(learned the hard way,
2026-08-12)*. A visual defect that turns out to be *static* — a bad offset, a wrong attachment, a
mesh that does not sit where it should — is fully visible on the placed actor in the editor, with no
PIE, no animation and no ability running. The character hover bug was chased for two sessions
through skeletons, root motion, root locks and montage playback, and the training dummy was
displaying it in the level viewport the entire time.

So before deciding *which* dynamic system causes a visual problem, establish **whether it is dynamic
at all**: look at the placed actor at rest. If the defect is there, everything animation-shaped is
already eliminated and the cause is in the actor's own setup. If it is not, you have learned
something real too, and cheaply.

The general form, which applies well beyond visuals: **before testing whether a symptom depends on
X, test whether it depends on anything at all.** It is a strictly cheaper question and it partitions
the search space far more brutally than any comparison between two candidate causes.

**And a corollary about reporting:** what the user glosses over is often the decisive observation.
"The dummy hovers in the preview too" and "the dodge hovers as well" each reframed this bug
instantly, and both were volunteered casually rather than in response to a question. When a bug
resists, ask explicitly what else shows the symptom — including in states nobody thinks of as part
of the system.

## Diagnosing timing

`TD.DebugCombatTiming` gives a per-attack trace of the attack phase model: windup rate wanted
versus applied, coil start and derived rate, commit position and target, each release window edge
with the montage position it fired at, and the facing error at every ability facing lock.

**It defaults to ON** *(corrected 2026-08-12 — this section and the header comment both said "off
by default" while `TDCombatDebug.cpp` set it to `1`)*. Turn it **off** with
`TD.DebugCombatTiming 0` once combat stops being the thing under test. It costs nothing when off,
because the macro skips its arguments entirely.

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

**`GetLogEntries` silently defaults `category` to `"LogsToolset"`** *(2026-08-12)*, which is not a
real category, so any call omitting it fails with `Log category 'LogsToolset' not found` — an error
that reads like the log system is broken rather than like an argument is missing. **Pass
`category: ""` explicitly, every time.**

**`StartPIE` takes a `startTransform` that overrides the player spawn for that session only**
*(2026-08-12)*. This is how to measure something that a player pawn is standing in the way of,
without editing the level: nothing is dirtied and nothing needs reverting. It was worth finding —
the alternative on the day was moving `PlayerStart` in `L_CombatTest`, which would have dirtied a
level to run one experiment.

**`L_CombatTest`'s floor is one scaled `Engine/BasicShapes/Plane`, and its size is a measurement
constraint** *(enlarged 2026-08-12: scale 20 → 100, so 2000×2000 becomes 10000×10000 centred on the
origin, edges at ±5000)*. It was enlarged because accumulating displacement over several attacks is
how attack travel is measured, and the dummy walked off the old floor mid-run — at 77 cm a swing it
had ~15 attacks of runway, and Lunge is expected to author nearer 230, which would have given it
five. Verify any change with two `SceneTools.trace_world` probes, one inside the floor and one
beyond it, so the check can actually fail.

A kill volume and a teleport-home volume were both considered and rejected for this: the first
injects death, ragdoll and the debug revive into measurement runs, which are ability-gating states,
and the second would duplicate `ReturnToDebugAutoAttackHome`, which already *is* the teleport-home
mechanism. The gameplay question — what should happen when a *player* falls off — is deliberately
left to the Stun slice, where respawn rules are already open.

**Measuring an actor's own movement requires nothing else touching it, and a capsule counts.**
*(2026-08-12, caught by the user watching the viewport.)* Measuring the training dummy's attack
travel by sampling its position gave a confidently wrong number, because the dummy was pressed
against the player's capsule — blocked while in contact, released as it slid past, deflected
sideways. Two 42 cm radii touch at 84 cm of separation, so the contamination began long before the
two looked close. **This is "an assumed control is worse than no control" in its spatial form:** the
displacement was real, measured and not the thing being measured.

**Editor log timestamps are UTC; git commits are local.** *(2026-08-12.)* A log line reading
`2026.08.13-02.07` and a commit reading `2026-08-12 17:26 -0600` are the same evening. Check before
dating a doc entry from a log timestamp — an entry dated a day ahead of the commit that carries it
is the kind of small wrongness this project's dated claims depend on not having.

**The debug auto-attacker is a measuring instrument, and it has a configuration that silently
invalidates it** *(2026-08-12, after it produced a wrong number that was reported before anyone
checked)*. Travel is measured by letting it swing and reading where it stops, which needs
`DebugAutoAttackResetDelaySeconds` **plus the attack's full length** to fit inside
`DebugAutoAttackInterval`. Exceed it and the reset fires *mid-attack*, teleporting the attacker home
part-way through a swing — and the numbers still look plausible.

A charged runs ~1.45 s, so at the 3 s interval a 2.0 s reset delay overruns by 0.45 s. The heavy
overruns too, at 1.15 + 2.0. **A heavy travel figure measured that way was reported as fact and had
to be withdrawn.** 1.0 s clears all three tiers. Interval is read once when the timer is set in
`BeginPlay`, so changing it at runtime does nothing — only the delay is live.

**And a periodic world will alias against a periodic sampler.** Polling actor position through
round-trips that happen to land near a multiple of the 3 s attack cycle returns the same phase every
time: nine consecutive samples came back on the same plateau, which reads exactly like "the
character never moves" and is really "I only ever looked at one moment". The fix is to vary the
spacing deliberately, or fire a batch in one message and a single afterwards — not to take more
samples at the same cadence.

**The `TimeDilation` route is closed.** Slowing the world would make all of this easy, and
`AWorldSettings::TimeDilation` is present in reflection but rejects writes through
`ObjectTools.set_properties`. `AActor::CustomTimeDilation` is writable but is the wrong tool for
anything timer-driven, since world timers do not scale with it — the montage would crawl while the
ability's checkpoints fired on schedule.

**Simulate mode stalled where PIE did not, and this is unexplained** *(2026-08-12)*. In
`bSimulate: true` the dummy's looping auto-attack timer stopped firing after ~30 s of world time and
never resumed, with no `ACTIVATE` and no `REFUSED`; the ability had ended cleanly every time and the
timer does not depend on anything that was changed. Normal PIE, unfocused, ran 13 attacks unattended
in the same conditions. **Prefer normal PIE for anything timed** until somebody works out what
Simulate is doing. Note the intermediate conclusion — "the sim only ticks when the editor has focus"
— was wrong and was contradicted by data already collected in the same session; the editor being in
the background is not the variable.

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
