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

## What is in the library vs. what is in this project

**These are different sets, and confusing them is the obvious way to waste time.** The index
and vocabulary below describe the **library** — everything available to migrate. Only a
subset has actually been brought into this repository.

| | Assets | Where |
|---|---:|---|
| Library (available to migrate) | 6,576 | `AnimLibrary`, not in this repo |
| **Migrated into this project** | **715** | `/Game/GDHBundle/` |

Migrated so far, as of 2026-08-10 — `SwordShield` only:

- `SwordShieldAnimV1/Animation/RM` (152) and `IP` (135)
- `SwordShieldAnimV3/Animation/RM` (189) and `IP` (166)
- `DEMO/` dependencies (~70): the Manny skeleton and mesh, materials, textures, and the
  `SM_Sword` and `Shield_Heater` props
- **Not migrated:** `SwordShieldAnimV2`, and every other archetype

So: if a clip appears in the index but not under `/Game/GDHBundle/` in this project, it
exists and needs migrating — it is not missing. Check with
`find Content/GDHBundle -iname "*<term>*"` before assuming either way.

## The complete index

`Docs/Animation-Library-Index.tsv` lists every one of the 6,576 assets —
`Archetype`, `Asset`, `ContentPath` — so the library remains searchable even on a machine
where it is not installed. Grep it rather than reading it.

Regenerate with, from `AnimLibrary/Content`:

```bash
find GDHBundle -name "*.uasset" -printf "%p\n" | sed 's|\.uasset$||' | sort \
  | awk -F'/' '{n=split($0,p,"/"); printf "%s\t%s\t/Game/%s\n", $2, p[n], $0}'
```

It is a cache of an immutable purchase, so it does not drift in practice; regenerate only
if the bundle is ever updated or extended.

## Complete token vocabulary

**This list is exhaustive.** It is every underscore-separated token appearing anywhere in
any of the 6,576 asset names, with the number of assets containing it — directions
(`Fw` `Bw` `L` `R` `FL` `FR` `BL` `BR`), `RM`/`IP`/`AS` and bare numbers stripped. Pack
names, material tokens and bone names are left in rather than filtered, because filtering
is what caused the errors this list exists to prevent.

**If a word is not here, the bundle does not contain it. If it is here, it exists.** That is
the whole point: absence is now checkable instead of inferred.

```
01ID(50) 02ID(50) A(1) ABP(21) ANS(16) AORouMet(1) ASAOMASK(20) ASAOPMASK(22) AddWeapon(3)
Aim(24) AimFL(8) AimFR(8) AimFw(8) AirGrab(2) AnimStart(8) Archery(2) ArcheryAnimV1(356)
Arrow(1) ArrowArm(2) Assassination1-13(4 each) Attack(58) Attack1(98) Attack10(48) Attack11(8)
Attack12(8) Attack13(8) Attack14(4) Attack15(4) Attack16(4) Attack17(4) Attack2(66) Attack3(86)
Attack4(88) Attack5(52) Attack6(102) Attack7(102) Attack8(88) Attack9(20) AttackCombo(2)
AttackCombo1(192) AttackCombo10(190) AttackCombo11(38) AttackCombo12(42) AttackCombo13(26)
AttackCombo14(18) AttackCombo15(26) AttackCombo16(4) AttackCombo17(18) AttackCombo18(18)
AttackCombo19(18) AttackCombo2(224) AttackCombo20(26) AttackCombo21(4) AttackCombo22(18)
AttackCombo23(12) AttackCombo3(172) AttackCombo4(172) AttackCombo5(204) AttackCombo6(184)
AttackCombo7(216) AttackCombo8(182) AttackCombo9(238) BN(42) BP(37) Back(504) Base(1)
BaseColor(1) BaseColorFallOff(11) Block(4) Block1(5) Block2(4) BlockConstant(3)
BlockConstantHit(1) BlockCy(1) BlockDeath(3) BlockHit(4) BlockOnce(3) Blocking(1) Body(10)
Bow(54) BowAnim(1) BuiltData(43) CA(11) CCRCCPlastic(42) CR(21) Character(2) CharacterSample(3)
Charged(16) Chest(4) ChromaticCurve(11) Color(1) Compelte(4) Complete(1110) Counter(1) D(42)
DEMO(24) Dagger(2) DarkWood(4) Dash(103) Dash1(8) Dash2(8) Dead(2) Death(48) Defence(6)
Defence1(4) Defence2(4) Defense(10) Defense1(2) Defense2(3) DefenseEnd(1) DefenseStart(1)
Deflect(5) Demo(2) Diffraction(11) DirectX(1) Dodge(54) Dodge1(5) Dodge2(5) End(242)
FacingBw(2) FacingFw(48) Flip(18) FlipBL/BR/Bw/FL/FR/Fw/L/R(3 each) Gem(2) Gold(1) Grab(4)
GreatSword(2) Haduken(3) Head(4) Heal(2) Heater(1) Heavy(1) Hit(69) Hit1(8) Hit2(8) HitDeath(4)
Idle(39) Idle1(8) Idle2(8) Idle3(1) Jump(70) Jump1(3) Jump2(3) JumpCy(1) Katana(3) Kick(9)
Kick1(6) Kick2(6) Kick3(3) Kick4(8) KnockDown(2) Land(3) LightSteel(4) LightWood(4) Logo(11)
Loop(6) Loopable(160) M(45) MF(22) MI(42) ML(11) MSK(126) MSR(42) Mannequin(65) Manny(501)
Metallic(1) Mirror(39) Montage(1) Move(72) MoveBL(4) MoveBR(4) MoveBw(4) MoveFL(4) MoveFR(4)
MoveFw(4) MoveL(6) MoveR(6) MovingLoop(4) N(42) Normal(2) OnAir(1) PA(12) Parry(1)
PostProcess(21) Procedural(11) Projectile(1) Puch(3) Punch(3) Punch1(9) Punch2(9) Punch3(3)
Punch4(2) PunchCombo1(3) PunchCombo2(2) Quinn(451) React(526) Red(1) RemoveWeapon(3)
Resurrection(2) Resurrection1(2) Resurrection2(2) RiggedBow(3) Rise1(5) Rise2(5) Rise3-9(2 each)
Roll(84) Roll1(9) Roll2(8) Roughness(1) Run(593) Run1(64) Run2(64) RunCy(2) SK(12) SKM(36) SM(9)
SampleCharacter(6) Shealth(1) Sheath(23) Sheath1(1) Sheath2(1) Shield(2) Shoryuken(4) Shoulder(4)
Showcase(14) Showcase1(3) Simple(14) Spear(1) Squat(133) Squat1(23) Squat2(19) SquatAttack(4)
Stage1(1080) Stage2(792) Stage3(342) Stage4(82) Stage5(20) Start(246) Steel(4) Stomachake(2)
Style2(8) Sword(15) SwordShield(1) T(316) Tan(42) Throw1(6) Throw2(6) ThrowWeapon(1)
ToPosition(28) ToPostition(2) TwoHanded(1) UE(11) Unshealth(1) Unsheath(21) Unsheath1(1)
Unsheath2(1) UnsheathIP(1) Unshield(1) Up(2) V1(9) V2(9) Walk(609) WalkCy(2) White(1) anim(294)
calf(84) clavicle(84) foot(84) hand(84) inside(1) lambert1(5) logo3layers(11) lowerarm(84)
outside(1) pose(294) rbd(3) thigh(84) upperarm(84)
```

Pack-name tokens (`SwordAndShieldAnimV1`, `KatanaCombatAnimationV1`, and so on) are omitted
above only because they identify the folder rather than the move; every other token is present.

### Vendor typos — search both spellings

The bundle contains genuine misspellings, so a *correct* search still misses assets:

| Intended | Also appears as |
|---|---|
| `Complete` (1110) | **`Compelte`** (4) |
| `Punch` | **`Puch`** (3) |
| `Sheath` / `Unsheath` | **`Shealth`** (1) / **`Unshealth`** (1) |
| `ToPosition` (28) | **`ToPostition`** (2) |
| `Defense` (10) | `Defence` (6) — both spellings used, in different packs |

`Defence`/`Defense` is the dangerous one: it is not a typo but a genuine split, so searching
one spelling silently halves the results.

## Move vocabulary

What each archetype can actually *do*, which is the question worth answering quickly. The
number is how many clips share that move name across directions, `RM`/`IP` and qualifiers.

> **This table reads only the first token after the pack name, so it is blind to
> qualifiers.** `AS_..._Block1_Parry_RM` is counted under `Block1`, and the parry is
> invisible. That blind spot already produced one confidently wrong claim here — that the
> bundle contained no parry animation, when `SwordShield` has one. **Never conclude
> something is absent from this table.** Use it to find what exists; grep
> `Animation-Library-Index.tsv` for a substring before claiming anything does not.

**`SwordShield` — the archetype this project uses:**

| Move | Clips | Why it matters |
|---|---:|---|
| `Attack1`–`Attack10` | 12–72 each | Offense. Heavily varied, so tier selection is a judgement call needing preview. |
| `Defense` / `DefenseStart` / `DefenseEnd` | 10 / 1 / 1 | **Block.** A start-loop-end structure, exactly what a held guard needs. |
| `Block1` / `Block2` | 4 / 3 | Block impact reactions — and the nearest thing to a parry (see gaps). |
| `Roll` | 8 | All eight directions. The evade this project uses. |
| `Dodge` | 10 | Backward and lateral only, no forward. |
| `Flip` / `Dash` | 16 / 16 | Unused so far. |
| `Hit` / `Death` | 5 / 5 | Hit reactions and knockdown, for focus item 3. |
| `Walk` / `Run` | 192 / 192 | Full directional locomotion set. |
| `Idle1` / `Idle2` | 3 / 3 | |
| `Sheath` / `Unsheath` | 3 / 3 | Only relevant if weapon swapping comes into scope. |

**Everything else, in brief:**

- `Unarmed` — `AttackCombo1`–`10`, `Punch1`–`4`, `Kick1`–`4`, `Roll`, `Dodge`, `Defense1/2`,
  `Block1/2`, `BlockHit`, `KnockDown`, `Rise1/2`, `Grab`, `Throw1/2`, plus novelty
  (`Shoryuken`, `Haduken`). The only archetype with an explicit `KnockDown` *and* `Rise`.
- `DaggerCombatAnimationV1` — the largest move vocabulary: `AttackCombo1`–`23`,
  `Assassination1`–`13`, `Defence1/2`, `Roll1/2`, `Dodge1/2`, `Rise1`–`9`, `Throw1/2`.
- `Spear` — `Attack1`–`13` plus `AttackCombo1`–`10`, and the fullest block set anywhere:
  `Block`, `BlockOnce`, `BlockConstant`, `BlockConstantHit`, `BlockHit`, `BlockDeath`.
- `TwoHandSword` — `AttackCombo1`–`11`, `Block`/`BlockOnce`/`BlockConstant`/`BlockHit`/`BlockCy`,
  `Roll`(18), `Dodge`(10), `Resurrection`.
- `DualSword` — `AttackCombo1`–`10`, a full block set, `Shield`/`Unshield`, `Death`.
- `GreatSword` — `AttackCombo1`–`10`, `Defence`(6), `Resurrection1/2`.
- `Katana` — `AttackCombo1`–`11`, and **`Deflect`(5), the only parry-shaped clips in the
  entire bundle**.
- `OneHandSword` — `AttackCombo1`–`10` and little else; no evades, no block, no defense.
- `SpellCombatAnimV1` — `Attack1`–`17`, `Heal`. Out of scope.
- `ArcheryCombatAnimV1` — aim-directional attacks, `Squat`(98), `Flip` in eight directions.
  Out of scope.

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
- **`_RM` in a filename does not mean root motion is switched on.** It means root motion is
  *baked into the clip*; `bEnableRootMotion` on the asset is **false** out of the box, so a
  clip named `_RM` moves nothing until that is enabled. Checked on
  `AS_SwordAndShieldAnimV1_Roll_Fw_RM`, which also reports `RootMotionRootLock: RefPose`.
  Anything relying on authored displacement has to set this per clip. **Our eight
  `SwordAndShieldAnimV1_Roll_*_RM` clips already have it enabled** — done in `67f4ace` when the
  dodge was built — so re-reading them returns `true` and does not contradict this. The claim
  describes what the bundle ships, not what our migrated copies now hold; any *newly* migrated
  clip still needs the flag set.
- **The Manny skeleton has `weapon_r` and `weapon_l` bones.** For attaching a sword and
  shield these are better than adding a socket to `hand_r`, since they are animated as part
  of the rig and the pack's clips were authored against them.
- **Parry is thin but it exists.** Six clips in the whole bundle: `SwordShield` has exactly
  one, `SwordShieldAnimV3/Animation/RM/AS_SwordSwordAnimV3_Block1_Parry_RM`, and `Katana` has
  five (`Deflect`, `Deflect_Complete`, `Deflect_Block`, `Deflect_Block_Heavy`,
  `Deflect_Counter`). One clip is enough for a parry that has a single success animation, so
  this is not a blocker — but there is no whiff or failed-parry variant in our archetype, and
  Katana's richer set is the wrong stance. Worth knowing before the parry slice, not during.
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

**The fix, exercised and confirmed on 2026-08-10** *(confirmed)*. Both skeletons were
compared bone by bone first: **161 bones each, identical in order and as sets, zero
differences** — they are the same UE5 Manny skeleton at two paths. Names agreeing is not
proof; that comparison is, and it is worth redoing for any future archetype.

Then one property, not 642 reassignments:

```
/Game/Characters/Mannequins/Meshes/SK_Mannequin  →  CompatibleSkeletons
    += /Game/GDHBundle/SwordShield/DEMO/Characters_SwordShield/Mannequins/Meshes/SK_Mannequin
```

Direction matters: the *consumer's* skeleton lists the skeletons whose animations it can
play, so the entry goes on **our** skeleton and points at theirs. Note this edits Epic
template content, which is acceptable but worth knowing when reading a diff.

`set_properties` returning true means nothing here — see the trap above. It was verified by
confirming the `.uasset` on disk was rewritten, that the binary physically contains the
`GDHBundle` reference, and that git saw the file change.

Migrate only what a slice needs. The discipline is the same one that applied when
animations were bought individually; it has just moved from purchasing to what enters the
repository.
