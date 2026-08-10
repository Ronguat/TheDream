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

```
& "C:\Program Files (x86)\UE_5.8\Engine\Build\BatchFiles\Build.bat" TheDreamEditor Win64 Development -project="<path>\TheDream.uproject" -waitmutex
```

Roughly 15–30s incremental. **Verify by confirming `Binaries\Win64\UnrealEditor-TheDream.dll`
is newer than every source file** — not merely that the build reported success.
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
- **Array edits** *(reported once)* — changing an existing element and adding one in the
  same call can fail on ambiguous insertion points. Set the array to `[]` first, then
  write the full desired contents.
- **TMap keys** *(reported once)* — setting a map property logs `added key ... not found
  in map after import` while the entry is in fact correct at runtime. Misleading, not
  fatal.
- **A GameplayEffect's inline tag containers cannot be written** *(confirmed 2026-08-10)* —
  `inheritableOwnedTagsContainer` and `ongoingTagRequirements` are present in reflection and
  accept a write without error, but read back **empty**. UE 5.8 has moved this behaviour to
  `gEComponents` (`UTargetTagsGameplayEffectComponent`,
  `UTargetTagRequirementsGameplayEffectComponent`), and the inline properties are vestigial.
  Numeric and enum properties on the same asset — `durationPolicy`, `period`, `modifiers`,
  even the modifier's attribute path — write fine, so a partially-configured effect is the
  likely outcome if you do not check. **Adding a GEComponent is not scriptable**; it needs a
  human in the details panel. Read tag containers back after writing them, always.
- **`AssetTools.delete` leaves the `.uasset` on disk** *(confirmed 2026-08-10)* — it
  returns true, force-deletes the object from memory and drops it from the asset
  registry, so `find_assets` stops listing it. The file is untouched, with its original
  timestamp. The next editor start rescans the directory and the asset comes back.
  `AssetTools.exists` still returns true, so it is the check that catches this; a registry
  query is the one that agrees with the lie. **Delete the file on disk as well** (`git rm`,
  since Content is tracked) and confirm with a directory listing.

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
  silently disables all future attacks
- Template locomotion still works, whenever input code was touched
- `LogAbilitySystem` is free of new warnings

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
