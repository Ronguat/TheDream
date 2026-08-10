# Working in Unreal on this project

Practical notes for driving this project through the Unreal editor and its MCP toolset.
Read before writing assets or C++.

**Confidence marks.** Items tagged *(confirmed)* were observed directly and re-checked on
2026-08-09. Items tagged *(reported once)* come from a single past incident and have not
been reproduced since — trust them enough to work around, but re-test rather than treat
as settled if one blocks you or looks wrong. Do not promote a mark without re-observing
the behaviour; an unverified claim in here is worse than an absent one.

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

**Verify against the artefact, not the tool's return value.** Two of these tools have now
reported success while changing nothing that mattered, in different ways. Whatever the
write was meant to produce — a value some system actually reads, a file gone from disk —
check *that*, and prefer a check that does not go back through the API that did the
writing. A read-back through the same layer can confirm a write that never landed.

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

The mechanism was not proven. The likely candidate is Blueprint reinstancing rebuilding the CDO
from serialized data and discarding the in-memory write, which would explain why the value
survived to disk but not to runtime. Do not treat that explanation as settled; treat the rule as
settled.

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

This is why renaming an AnimNotify class is expensive: placed notifies serialize against
the class path, so a rename breaks them and they must be re-placed by hand.

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
- Template locomotion still works, whenever input code was touched
- `LogAbilitySystem` is free of new warnings

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
