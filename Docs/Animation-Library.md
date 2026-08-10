# The animation library

Where the project's animations come from, how to find one, and how to get it into this
repo without dragging half a gigabyte behind it.

**The library is not in this repository and must never be.** It is a ~6.6 GB GameDevHero
bundle installed into a separate throwaway project:

```
C:\Users\rross\Documents\Unreal Projects\AnimLibrary     (UE 5.8, not under version control)
    Content\GDHBundle\<Archetype>\...
```

Keeping it out is not a `.gitignore` question. Ignored assets referenced by tracked ones
give a fresh clone dangling references that read as corruption rather than a missing file,
and Unreal's asset registry scans everything under `Content/`, so an in-project library
would slow every editor start forever to hold assets nothing uses. It is also disposable:
Fab/Vault content re-downloads from the Epic account indefinitely, so it needs no backup.

Both projects are on **UE 5.8**. Migrating between different engine versions is where this
gets messy, so keep them in step.

## What is in it

6,576 assets, of which 5,319 are AnimSequences (`AS_`). The rest are demo maps, characters,
materials and control rigs that exist to showcase the animations and are not wanted here.

| Archetype folder | Assets | Notes |
|---|---:|---|
| `SwordShield` | 1046 | Largest set. The obvious source for block, having an actual shield. |
| `DaggerCombatAnimationV1` | 891 | |
| `DualSword` | 780 | |
| `TwoHandSword` | 663 | |
| `Spear` | 662 | |
| `OneHandSword` | 626 | Offense-heavy; **no dodges or rolls at all** (see gaps). |
| `Unarmed` | 485 | Matches what the prototype currently is. |
| `ArcheryCombatAnimV1` | 476 | Ranged; out of scope for now. |
| `SpellCombatAnimV1` | 333 | Out of scope. |
| `GreatSword` | 321 | |
| `Katana` | 293 | |

## Naming convention

This is the part worth knowing, because it is what makes 5,319 animations searchable from
a shell instead of by eye:

```
AS_<PackName>_<Move>[_<Direction>][_<Qualifier>][_RM]
```

- **Direction** is one of `Fw` `Bw` `L` `R` `FL` `FR` `BL` `BR`.
- **`_RM` is root motion and `_IP` is in place.** Both are stated explicitly — 2,774 against
  2,450 — so never infer "in place" from the absence of `_RM`. 95 clips carry the marker
  mid-name rather than at the end (archery is `..._IP_Bow`), so match the token, not the
  suffix.
- **Qualifiers** modify a variant, e.g. `_FacingFw` on a backward dodge that keeps facing
  forward, `_Stage1` / `_Complete` on the pieces of a multi-part combo, or `_React` on a
  version that includes the recipient's reaction.

So finding candidates is a `find` away, and asking for a preview is only needed to choose
between clips that are already the right kind.

## Known gaps and what they imply

These are facts about the bundle, checked across all 6,576 assets, not impressions:

- **There is no forward dodge anywhere.** Every `Dodge` set is `Bw`, `L`, `R`, `BL`, `BR` —
  backward and lateral only, in every archetype. `Roll` covers all eight directions
  including forward. The library is expressing a convention: you do not dodge forward, you
  roll forward. That is a design question for us, not a defect — see the open question in
  `Docs/Combat-Decisions.md`.
- **Every dodge is root motion.** There are zero in-place dodge clips. Root motion can be
  switched off on a montage and displacement driven in code, so this constrains the default
  rather than the ceiling, but authored displacement is what ships.
- **`OneHandSword` has no evasive animations at all.** If the project moves to a one-handed
  sword archetype, its dodges have to come from another folder — most plausibly
  `SwordShield`, which is the nearest stance.
- **Props do not follow the `SM_` / `SKM_` naming convention, so do not search by prefix.**
  `SwordShield/DEMO/StaticMesh/` holds both `SM_Sword` *and* `Shield_Heater` — the shield
  carries no prefix at all. A prefix-filtered search for shield meshes returned nothing and
  produced a confidently wrong claim here that the bundle contained no shield, corrected only
  because a migrate dependency list showed otherwise. Match on substring; the animations are
  regularly named, the props are not.

## Props and reference worth knowing about

`GDHBundle/SwordShield/DEMO/` carries more than animations, and it is the archetype this
project uses:

- `StaticMesh/SM_Sword` and `StaticMesh/Shield_Heater` — the actual props, with materials
  (`M_Sword`, `LightSteel`, `DarkWood`, `LightWood`).
- `Blueprint/BP_CharacterSample_SwordShield` — the pack's own rigged sample. Worth opening
  as a **reference for socket placement** rather than as something to inherit from; it shows
  where they expect the sword and shield to attach.
- `BP_ANS_Sheath` / `BP_ANS_Unsheath` — sheathing notify states, unused for now but relevant
  if weapon swapping ever comes into scope.

## Getting an animation into this project

Use **Asset Actions → Migrate** from `AnimLibrary`, targeting `TheDream/Content`. Migrate
copies the whole dependency chain, which is the thing to be careful about.

**The skeleton caveat.** Every sub-pack ships its *own copy* of `SK_Mannequin`, at its own
path — e.g. `GDHBundle/Unarmed/DEMO/Character_Unarmed/Mannequins/Meshes/SK_Mannequin`. This
project already has one at `/Game/Characters/Mannequins/Meshes/SK_Mannequin`. They are the
same UE mannequin, but they are different *assets*, and Unreal binds an AnimSequence to a
skeleton by path. So a naive migrate lands a duplicate skeleton in the project, and clips
bound to the wrong one will not play on our character.

*(Unverified — the fix has not been exercised yet.)* The intended resolutions, cheapest
first: mark the two skeletons as **compatible skeletons**, which is the UE5 feature built
for exactly this and avoids duplicating anything; or migrate, reassign the clips to our
skeleton, and delete the duplicate. Confirm the bone hierarchies actually match before
trusting either — the names agreeing is not proof.

Migrate only what a slice needs. The discipline is the same one that applied when
animations were bought individually; it has just moved from purchasing to what enters the
repository.
