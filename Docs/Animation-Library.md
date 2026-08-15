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

**But the split is between archetypes, never inside one** *(checked 2026-08-10 across all
6,576 rows of the index)*. No archetype uses both spellings:

| Spelling | Archetypes |
|---|---|
| `Defense` | **`SwordShield` (17)**, `Unarmed` (5) |
| `Defence` | `DaggerCombatAnimationV1` (8), `GreatSword` (6) |

So **within `SwordShield` — the only archetype this project uses — it is uniformly `Defense`,
and the hazard does not apply.** That matters for the block slice, which searches this exact
term. Search both spellings when surveying the bundle; search one when working in an archetype.

Those counts are substring matches, so they include `Defense1`, `DefenseStart` and friends. The
token vocabulary above counts *exact tokens* and therefore reads `Defense(10)` for the same
clips — the two disagree because they measure different things, not because either is wrong.
This is the same mismatched-granularity trap that produced one of the wrong absence claims
below; when two counts here conflict, check what each one is counting before believing either.

**None of the misspelled clips are in this project** *(checked 2026-08-10)*. All ten live in
archetypes that have never been migrated — `Compelte` in `GreatSword`, `Shealth` and
`ToPostition` in `TwoHandSword`, `Puch` in `Unarmed`. Confirmed with:

```bash
find Content/GDHBundle -iname "*Compelte*" -o -iname "*Puch*" \
                       -o -iname "*Shealth*" -o -iname "*ToPostition*"   # returns nothing
```

### Do not rename vendor content to fix these

Considered and rejected 2026-08-10. **Treat the bundle as read-only and absorb its
irregularities at the search layer** — this table is the fix, and it costs a lookup.

Renaming in `AnimLibrary` does not stick: that project is disposable and unversioned, so the
next Fab re-download restores the typos with no record a rename ever happened. It would also
corrupt `Animation-Library-Index.tsv`, which is *generated* from the library — regenerate after
a rename and the checked-in index describes our mutated copy, so a contributor on a fresh
install regenerates it back and the diff reads as corruption. Worst of all it breaks the
guarantee this file exists to make: *"if a word is not here, the bundle does not contain it"* is
a claim about **the bundle**, and renaming quietly downgrades it to a claim about our copy.

Renaming *migrated* copies is separately bad: the library↔project correspondence is name-based
(see "What is in the library vs. what is in this project"), and a later migrate would land the
vendor's spelling beside our corrected one, leaving two conventions where there was one.

**Anything we author gets the correct spelling.** That is naming our own work, not renaming
theirs — the same distinction that keeps `AM_Dodge` named for the mechanic rather than for the
`Dash` clips it is built from.

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
| `Dash` | 16 | All eight directions in both packs. **The evade this project uses** — `AM_Dodge` is V3's eight `Dash_*_RM`. |
| `Roll` | 8 | All eight directions. Chosen for the dodge on 2026-08-10 and replaced by `Dash` the same day; still the fallback if a dash reads too weightless for a 50-stamina cost. |
| `Dodge` | 10 | Backward and lateral only, no forward. **Not what the Dodge mechanic plays** — the name is the vendor's, not ours. |
| `Flip` | 16 | Unused so far. |
| `Hit` / `Death` | 5 / 5 | Hit reactions and knockdown, for **Stun**. |
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

## The attack clip schema, and why `Stage` clips are not attacks

*(established 2026-08-11 by inspecting the clips, after measurements alone pointed the wrong way)*

Every `AttackN` in the bundle exists in four forms, and only two of them are usable as a
standalone attack:

| Form | What it is | Usable alone? |
|---|---|---|
| `AttackN_StageM` | The strike **fragment**. Begins and ends mid-motion. | **No** — it snaps in and out of pose |
| `AttackN_StageM_Back` | That stage's return to idle | No, it is a tail |
| **`AttackN_StageM_Complete`** | **Stage + Back: a strike that settles, lands, and holds.** Same shape as the unarmed punch this project already used. | **Yes** |
| `AttackN` | The whole multi-stage attack, idle to idle | Yes, but long (2.1–4.9 s in V3) |

`Stage + Back ≈ Complete` arithmetically, which is what first suggested the structure; a preview
confirmed it. `AttackN_StageM_Complete_React` also exists — that is the same clip including the
*recipient's* reaction, for showcase purposes, not for us.

**The fragments are not useless, they are just not standalone.** A clip that begins and ends
mid-motion is exactly right for a **mid-string hit**, where the next hit continues the movement
rather than resolving it. So a combo string is `Stage1` → `Stage2` → `StageN_Complete`, with the
terminal hit resolving. That is how the pack is built, and it means a family's stage count *is*
meaningful for a string even though it is not a count of usable standalone attacks.

**Duration matters more than it looks.** `_Complete` clips run 0.900–3.000 s in V3; the standalone
`AttackN` forms run 2.133–4.867 s. Since the play rate is derived from where the impact frame
sits, a 0.9 s clip lands near the rate this project already ships, while a 2.1 s one implies
roughly 3×. **Reach for `_Complete`, not the standalone form.**

### Every complete single attack in `SwordSwordAnimV3` RM

**23 clips.** This is the candidate pool for Attack Swap and beyond — everything in V3 that is one
self-contained strike. Durations measured 2026-08-11. The **What it is** column is filled in by
preview only; blank means nobody has looked yet.

**Duration is not a filter.** It is an input to a derived play rate, and a clip authored slowly
can be sped up — `Attack4_Stage2_Complete` runs 1.933 s and reads *too slow* at 1×. Judge the
motion, not the number. Recorded because the opposite assumption drove clip selection for most of
a session and ruled out the eventual winner's whole family on arithmetic.

Ordered by attack number so a family can be cleared in one sitting, not by duration — the
review is per-family and the durations are in the table anyway.

**Triage a family by watching its standalone `AttackN` clip first.** That file is all of the
family's stages played as one fluid sequence, so a single preview tells you roughly what every
stage contains. `Attack3` and `Attack6` were dismissed wholesale that way. This turns an N-clip
review into one clip plus follow-ups only where something looked promising.

| Clip | Length | What it is |
|---|---:|---|
| `Attack1_Stage1_Complete` | 3.000 | Two consecutive spinning overheads, lots of forward motion. Two attacks in one clip — awkward, though multi-hit singles are not unheard of. |
| `Attack1_Stage2_Complete` | 1.367 | Forward lunging uppercut **with the shield** |
| `Attack1_Stage3_Complete` | 1.567 | Stationary left-to-right slash |
| `Attack2_Stage1_Complete` | 1.333 | Step-forward jab **with the shield** |
| `Attack2_Stage2_Complete` | 1.633 | **360° spinning left-to-right slash. Third-light-in-chain candidate** |
| `Attack3_Stage1_Complete` | 1.767 | Rapid small Spartan-style manoeuvres. **No use here** |
| `Attack3_Stage2_Complete` | 1.133 | As above. **No use here** |
| **`Attack4_Stage1_Complete`** | 0.967 | **Diagonal slash, top-right → bottom-left.** Steps forward, settles. **CHOSEN LIGHT** |
| **`Attack4_Stage2_Complete`** | 1.933 | **360° swing.** Reads too slow at 1×, wants speeding up. **Second-light candidate** |
| `Attack4_Stage3_Complete` | 1.000 | **Shield bash**, fairly stationary |
| `Attack4_Stage4_Complete` | 2.300 | Heavy stab + flourish on withdraw. No forward travel |
| `Attack5` | 2.333 | Idle → brief defensive stance → step-forward left-to-right slash. Only plausible as a **canned counter-attack**, sped up |
| `Attack6_Stage1-3_Complete` | 2.000 / 0.900 / 2.333 | HEMA-style mixed melee, heavy on kicks, little sword-and-shield. **No use here** |
| `Attack7_Stage1_Complete` | 1.767 | Big stepping spinning overhead, top-left → bottom-right. **Heavy candidate**, but its windup resembles no light we have |
| `Attack7_Stage2_Complete` | 0.967 | **Shield bash**, steps forward |
| `Attack7_Stage3_Complete` | 2.500 | Execution animation. **No use here** |
| `Attack8_Stage1_Complete` | 2.000 | Fencing-like flourishes. **No use here** |
| `Attack8_Stage2_Complete` | 1.500 | Stepping forward, standard left-to-right slash. Solid but unremarkable; **windup crosses the body**, so it blends with less |
| **`Attack8_Stage3_Complete`** | 2.233 | **Stepping overhead, top-right → bottom-left.** Windup cocks the arm back on the **right**, then swings forward over the right shoulder — **the most standard windup found, and the closest match to the chosen light** |
| `Attack9` | 2.533 | Steps into a leaned-forward 3-hit combo but never travels. Canned-animation material only |
| `Attack10` | 2.133 | Two stabs over and under the shield while advancing, shield held up defensively |

**All 23 reviewed, 2026-08-11.** Families 3 and 6 are unusable for this project outright.

### Windup compatibility is the selection criterion, not duration

The single most useful thing the review surfaced, and it is a *design* constraint rather than an
art one. This project's ladder **shares one windup across light, heavy and charged** — that is
what makes the light unreactable (it never coils, so there is no tell) and makes reactability a
property of the coil rather than of the press. Tiers are separated by hold duration and the
commit checkpoint, not by different animations.

So a heavy sourced from a clip whose windup looks nothing like the light's would announce itself
from frame one, which **breaks the light's unreactability and the reactability model with it.**
Clips must therefore be judged on whether their *wound-up approach* matches, not on how good the
strike is in isolation:

- `Attack4_Stage1_Complete` (light) — top-right → bottom-left, right-side windup
- `Attack8_Stage3_Complete` — top-right → bottom-left, arm cocks back **right**, over the right shoulder

Those two are the same swing family and are the strongest pairing found. By contrast
`Attack8_Stage2_Complete` winds up *across the body* and `Attack7_Stage1_Complete` comes from
top-left, so both would read as different attacks before they landed.

**Combo families, for Light String.** Stage counts are `Attack4` **4**; `Attack1`/`6`/`7`/`8` **3**;
`Attack2`/`3` **2**; `Attack5`/`9`/`10` none. A string uses the *fragments* mid-chain and a
`_Complete` to terminate — so a family's stage count is a usable hit count for a string, unlike
for standalone attacks. **`Attack4` is the only four-stage family**, and it is where the chosen
light came from, so one coherent set can serve Attack Swap and Light String together.

The bare `AttackN` form of a family *with* stages (e.g. `Attack4` at 4.867 s) is the entire
multi-stage combo idle-to-idle, not a single strike. Not a candidate here.

### Clips identified as something other than a plain swing

Worth recording, because a name gives no hint and re-discovering costs a preview each time:

- **`AS_SwordSwordAnimV3_Attack7_Stage2_Complete_RM` is a shield bash** — starts from idle, steps
  forward into a bash, holds the new position. Wrong for a sword light, and a strong candidate if
  a **shield bash ability** is ever wanted (out of scope now; abilities and specials are).

## Known gaps and what they imply

These are facts about the bundle, **checked 2026-08-10 across all 6,576 rows of
`Animation-Library-Index.tsv`, unfiltered**, not impressions. Individually dated items below were
checked later and say so.

- **No clip *named* `Dodge` goes forward.** Every `Dodge` set is `Bw`, `L`, `R`, `BL`, `BR` —
  backward and lateral only, in every archetype. `Roll` and `Dash` both cover all eight directions
  including forward.

  **The design question this used to pose is settled in practice, as of 2026-08-13.** The line
  read "the library is expressing a convention: you do not dodge forward, you roll forward… it has
  never been recorded as a decision." Our Dodge plays `Dash`, which has a `Fw`, and all eight
  directions are play-verified at `DodgeTargetDistanceCm`. So the project dodges forward and the
  vendor's convention was never binding — it was a naming artefact of one clip family. Still true
  that no entry *argues* for a forward evade; it was simply never in doubt once the clip family
  changed.
- **Every dodge is root motion.** There are zero in-place dodge clips. Root motion can be
  switched off on a montage and displacement driven in code, so this constrains the default
  rather than the ceiling, but authored displacement is what ships.
- **`OneHandSword` has no evasive animations at all.** If the project moves to a one-handed
  sword archetype, its dodges have to come from another folder — most plausibly
  `SwordShield`, which is the nearest stance.
- **Turning `bEnableRootMotion` off does not give you an in-place clip** *(learned the hard way,
  2026-08-13)*. It stops the movement component **consuming** the root motion; it does not remove
  it from the pose. The displacement is still in the root bone, so it renders — the mesh visibly
  walks off the capsule and snaps back when the montage ends. Reported from play as *"the animation
  goes about 4x as far as the dodge and is desynced… then resets its position"*, which is exactly
  what an unlocked root bone carrying baked motion looks like.

  **`bForceRootLock = true` is the flag that makes an `_RM` clip behave in place.** It pins the root
  bone to `RootMotionRootLock` (already `RefPose` on these clips) whether or not root motion is
  enabled. The pair to set, for any clip whose displacement is being driven in code instead, is
  `bEnableRootMotion = false` **and** `bForceRootLock = true` — the first stops the double-move,
  the second stops the drift, and setting only the first is worse than setting neither.
  Done on the eight V3 `Dash_*` clips when the dodge moved to authored displacement.
- **`_RM` in a filename does not mean root motion is switched on.** It means root motion is
  *baked into the clip*; `bEnableRootMotion` on the asset is **false** out of the box, so a
  clip named `_RM` moves nothing until that is enabled. Checked on
  `AS_SwordAndShieldAnimV1_Roll_Fw_RM`, which also reports `RootMotionRootLock: RefPose`.
  The claim describes what the bundle ships, not what our migrated copies hold; any *newly*
  migrated clip still needs the flag set.

  **Our copies disagree with the bundle in both directions, so read the flag rather than the
  name.** The eight `SwordAndShieldAnimV1_Roll_*_RM` clips have it **enabled** (`67f4ace`, when
  the dodge was first built on rolls) and are no longer played by anything. The eight
  `SwordSwordAnimV3_Dash_*_RM` clips that `AM_Dodge` actually plays have it **disabled**, plus
  `bForceRootLock = true`, because displacement is authored — see the pair rule above.
- **Attach props to the pack's `Sword` / `Shield` sockets, never to the `weapon_*` bones.**
  *(confirmed 2026-08-10, the hard way)* `GDHBundle`'s `SKM_Manny` carries three authored
  sockets — `Sword`, `Shield`, `Sheath` — parented to `hand_r` / `hand_l`, and they hold the
  grip rotation **and a non-uniform scale**. The shield's is `0.25, 0.20, 0.30`, correcting a
  mesh 256 units tall against a ~180-unit character. **Both props are correct at identity when
  attached to the sockets**, which is why the pack's own `BP_CharacterSample_SwordShield` reads
  scale 1 everywhere and still looks right.

  This **supersedes an earlier claim here** that the `weapon_r` / `weapon_l` bones were the
  better target "since they are animated as part of the rig". That advice fails twice:

  1. Those bones are **absent from Epic's `SKM_Manny_Simple`**, which is what this project's
     character used. `SetupAttachment` given a name it cannot resolve falls back silently to
     the component root, putting both props at the character's midriff with no warning at all.
  2. Only GDH clips animate them, so under any Epic animation they sit at reference pose — the
     props were correct during a dodge and nonsense at every other moment. `hand_r` / `hand_l`
     are driven by every animation there is.

  **The verification lesson, which cost most of a slice:** `get_bone_names()` reports the
  *Skeleton asset's* bone list, while `get_bone_parent` / `get_bone_children` read the
  *SkeletalMesh's* reduced hierarchy. `weapon_r` appears in the first and not the second, and
  the two were read as agreeing. A bone in `get_bone_names()` is **not** evidence you can
  attach to it — confirm with `get_bone_parent`, and check `get_socket_names()` on the mesh
  **first**, because a pack that ships props usually ships sockets for them. Note this is the
  presence-side twin of the absence rule in `CLAUDE.md`: a derived view misleads in both
  directions, and only one of them had a rule.
- **Parry is thin *by name* and less thin *by function*.** Six clips in the whole bundle carry a
  parry-shaped name: `SwordShield` has exactly one,
  `SwordShieldAnimV3/Animation/RM/AS_SwordSwordAnimV3_Block1_Parry_RM`, and `Katana` has five
  (`Deflect`, `Deflect_Complete`, `Deflect_Block`, `Deflect_Block_Heavy`, `Deflect_Counter`).

  **Re-searched 2026-08-11 at the user's suggestion, by enumerating every distinct `SwordShield`
  move rather than grepping for parry words — and the picture changed.** The name search was
  correct and answered the wrong question: it asked *what is called a parry* rather than *what
  could function as one*. `SwordShield` also has `Block1` and `Block2`, each a **discrete**
  block action with its own `_Idle` and `_Hit`, and those are parry-shaped whatever they are
  called. `Block1_Hit` / `Block2_Hit` are candidate reads for a parry that failed, which the
  earlier note said did not exist. So the honest count is three candidate shapes plus failure
  states, not one clip. **This is the "index guarantees names, not suitability" warning below
  biting in a new way: a name search cannot find a clip that would work under a different name.**

  **The two packs split by idiom, not merely by version**, which is the finding that matters for
  the parry slice:

  | Pack | Idiom | Clips |
  |---|---|---|
  | `SwordAndShieldAnimV1` | **held guard** | `Defense`, `Defense_Loop`, `DefenseStart`, `DefenseEnd`, four directional `Defense_Hit_*`, four `_Death` |
  | `SwordSwordAnimV3` | **discrete block actions** | `Block1`/`Block2`, each with `_Idle` and `_Hit`, plus `Block1_Parry` |

  That maps onto this project's two mechanics almost exactly: **block holds** (V1's idiom) while
  **parry is a 400 ms discrete action** — which is what V3's `Block*` family is. All 19 of these
  clips are **already migrated**; none needs a migrate.

  **This paragraph claimed until 2026-08-14 that V1 "is already the pack the dodge and locomotion
  come from, so Block has no stance problem at all". That premise died on 2026-08-11** — V3 became
  the base stance, and the dodge moved to V3's `Dash_*` clips on top of it. So reaching for V1's
  held guard *is* a pack mix, and whether that reads acceptably is the open question `CLAUDE.md`
  files under Block as an idea *not decided*. The mix is very likely still the right call, for the
  reason the V3 swap itself recorded: V1 turns out to be specifically guard-shaped, which is a
  defect as a neutral stance and exactly right as a guard. What is not true is that it costs
  nothing.

  **The "`SwordSword` might mean dual swords" worry is dead** *(raised and closed 2026-08-11)*.
  The `SwordShield` archetype contains three sub-packs named three different ways for the same
  thing — `SwordAndShieldAnimV1`, `SwordShieldAnimV2`, `SwordSwordAnimV3` — while every dual-sword
  asset is named `DualSwordAnimation*` and lives in the `DualSword` archetype. The name is vendor
  inconsistency, from the vendor that also ships `Compelte` and `Puch`. Left recorded rather than
  deleted because the name will look alarming to the next reader too.

  What still needs a preview rather than a search: whether V3's guard pose reads consistently
  beside V1's. A mismatch costs far less across a 400 ms flash than across a held pose, but less
  is not none.
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

**A retarget-first variant, for future packs.** The `CompatibleSkeletons` fix above works because
both skeletons are the same UE5 Manny at two paths. A pack built on a genuinely different
skeleton cannot be fixed that way: retarget it **inside the library project** and migrate the
*retargeted* results, so the foreign skeleton never enters this repo at all.

## Asking for animations

**If missing animations are genuinely blocking a feature, ask.** Do not silently design around a
real gap, and do not treat one as a hard blocker — the library is a bought pack that lives
outside the repo, so the request is normally "please migrate these", not "please buy these".

Confirm the gap is real first, and check **all** the places content already lives: this bundle,
and the Epic template content still under `/Game/` (`Characters`, `Input` — the rest was deleted
by the 2026-08-15 structure audit). Then name precisely what the current slice needs — specific
moves, how many, root motion or not — and nothing beyond it.

**The index guarantees names, not suitability.** The token vocabulary and
`Animation-Library-Index.tsv` make *absence* checkable, which is what they exist for. They say
nothing about whether a clip is any good for its purpose — that still needs previewing, and it is
how the eight-direction `Dash` set sat migrated and unnoticed while `Roll` was chosen on an
argument about which sets covered all eight directions. Both did.
