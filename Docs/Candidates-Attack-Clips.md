# Candidate attack clips — the audit list (work-in-flight)

**Delete this file when the audit is done.** It exists to be pruned: it lists every clip still
worth a designer's eyes, with everything measurable already filled in, so the pass is judgement
only. Nothing here is durable.

**Where the verdicts go when it is deleted:**

| Outcome | Destination |
|---|---|
| What a clip *is* (previewed description, use/no-use) | `Docs/Animation-Library.md`'s review table — extend it to V1 and V2, same columns as the V3 block |
| A clip chosen for a slot | `GA_Attack`'s CDO, and the montage authoring loop in `Docs/Plan-Light-String.md` |
| The criteria changes of 2026-08-18 (below) | `Docs/Combat-Decisions.md`, dated; they supersede named sections |
| A family dismissed wholesale | `Docs/Animation-Library.md`, beside families 3 and 6 |

---

## The two jobs

**Job A — lights 2 and 3.** Light String's only remaining blocker. Needs two clips that work as
mid-string swings, chosen from `_Complete_IP` stage clips.

**Job B — heavy and charged.** Recon banked ahead of the bespoke-windup slice, which is **its own
slice and not planned yet**. Needs one heavy and one charged, judged on anticipation rather than
on windup match.

---

## Criteria

### Job A — string swings

`_Complete` is correct for every hit, non-final ones included (the designer, 2026-08-18): the
player can stop attacking after any hit, so every swing must be able to resolve on its own.
*This supersedes `Docs/Animation-Library.md`'s "a combo string is Stage1 → Stage2 →
StageN_Complete", which recommends the fragments mid-chain.*

**The light as shipped is the reference, and it is a floor rather than a spec** (the designer,
2026-08-18): timing distortion of that magnitude is effortlessly acceptable. It does not say more
is unacceptable — only that more is unverified.

| Reference | `AM_Attack` = `AS_SwordSwordAnimV3_Attack4_Stage1_Complete_IP` |
|---|---|
| Length | 0.967 s |
| Section rates | windup **1.500** / release **1.000** / recovery **0.588** |
| Shape | impact at 31% in, contact window 16%, tail 52% |

Holding those proportions, all three rates scale linearly with length, so **length ÷ 0.95 is a
clip's distortion factor** — the `× ref` column below. It is a confidence gradient, **not a
filter**: proportions cannot be read from length, so a clip inside the band is not thereby good
and a clip outside it is not thereby bad.

**No proven length dealbreaker exists.** The one clip that felt wrong (`AM_Attack_S2`, 2.03×) was
the blend-out bug, and its own review note reads *"reads too slow at 1×, wants speeding up."*

### Job B — heavy and charged

**Changed 2026-08-18, and it widens the pool.** The bespoke pass deprecates the coil, replacing a
rate freeze with a blended transition into a real anticipation. The tell window is unchanged —
**350 ms** light→heavy (0.150 → 0.500) and **300 ms** heavy→charged (0.450 → 0.750), noting a
heavy that escalates only got 300 ms of its own.

- **Windup match is no longer required.** A heavy that migrates to a different plane reads as a
  tell rather than a glitch when the blend performs the migration. *This supersedes
  `Docs/Animation-Library.md`'s "Windup compatibility is the selection criterion, not duration."*
- **Length has stopped mattering** for these two tiers — entry is partway into the clip, so the
  front is discarded. The long singles are back in play and are the likeliest charged material.
- **What to note instead: the anticipation apex** — a distinct moment of maximum wind-up that
  holds long enough to be a blend target. The frame you would pick if asked for a still that says
  *a big hit is coming*. A clip that flows through its wind-up with no apex is a poor target
  however good the strike.
- **Also note the tail**: recovery must cover 0.500 s (heavy) / 0.600 s (charged).
- Entry position is a per-clip dial — early entry keeps the vendor's arcs, late entry asks more of
  the blend and risks foot sliding. Not a policy; decide per clip.

---

## Already ruled out — do not spend time on these

**By preview** (`Docs/Animation-Library.md`, 2026-08-11, all 23 V3 clips reviewed):

| Clip | Recorded reason |
|---|---|
| V3 `Attack3` S1, S2 | *"Rapid small Spartan-style manoeuvres. No use here"* — family unusable outright |
| V3 `Attack6` S1, S2, S3 | *"HEMA-style mixed melee, heavy on kicks, little sword-and-shield. No use here"* — family unusable outright |
| V3 `Attack7_Stage3` | *"Execution animation. No use here"* |
| V3 `Attack8_Stage1` | *"Fencing-like flourishes. No use here"* |
| V3 `Attack5`, `Attack9` | Canned-animation / counter-attack material only |
| V3 `Attack10` | Defensive double-stab, shield held up |

**By structure:** the 19 bare `AttackN` clips of *staged* families are whole combos idle-to-idle,
never montage sources. They appear below only as recon vehicles.

**Reopened 2026-08-18** by the windup-match criterion being dropped — these are candidates again:
V3 `Attack7_Stage1` (top-left overhead, the exact shape the blend reading was reasoned about) and
V3 `Attack8_Stage2` (*"windup crosses the body, so it blends with less"* — a blend-cost note that
still applies rather than being nullified).

---

## Watch order

Roughly 70 s of new footage. Pass 2 serves both jobs from the same clips.

**Pass 1 — eight unreviewed single moves (Job B).** Self-contained strikes, the natural
heavy/charged pool. ~27 s.

**Pass 2 — twelve unreviewed `AttackN` whole clips (Jobs A and B).** One clip triages a whole
family's stages; the terminal stage is usually the family's biggest hit, so note heavy candidates
while you are there. ~38 s. **Two exceptions where the whole clip misrepresents its stages** —
watch those stages directly (see the ⚠ rows).

**Pass 3 — three reopened V3 clips (Job B).** ~5.5 s.

---

## Job A — string swing candidates

`× ref` is length ÷ 0.95. **Reference: `AM_Attack` 0.967 s = 1.02×.**

### Already flagged for this slot — cheapest wins

| # | Clip | Length | × ref | Recorded | Verdict |
|---|---|---:|---:|---|---|
| 1 | V3 `Attack2_Stage2_Complete` | 1.633 | 1.72 | **"360° spinning left-to-right slash. Third-light-in-chain candidate"** | |
| 2 | V3 `Attack1_Stage2_Complete` | 1.367 | 1.44 | "Forward lunging uppercut with the shield" | |
| 3 | V3 `Attack1_Stage3_Complete` | 1.567 | 1.65 | "Stationary left-to-right slash" | |
| 4 | V3 `Attack2_Stage1_Complete` | 1.333 | 1.40 | "Step-forward jab with the shield" | |
| 5 | V3 `Attack4_Stage4_Complete` | 2.300 | 2.42 | "Heavy stab + flourish on withdraw. No forward travel" | |
| 6 | V3 `Attack8_Stage2_Complete` | 1.500 | 1.58 | "Stepping forward, standard left-to-right slash. Solid but unremarkable" | |

### Unreviewed families — ranked on structure and length only

No preview information exists for any of these. Ordering is stage count first, then how many
stages sit near the reference.

| # | Family | Recon clip | Stage lengths (`_Complete`) | × ref | Verdict |
|---|---|---|---|---|---|
| 7 | **V2 `Attack5`** | ⚠ 2.767 — **1.03 s shorter than its stages; watch stages directly** | 1.167 / 1.033 / 1.300 / 1.200 | 1.23 / 1.09 / 1.37 / 1.26 | |
| 8 | **V2 `Attack1`** | 4.667 | 1.433 / 1.033 / 1.300 / 1.333 / 2.033 | 1.51 / 1.09 / 1.37 / 1.40 / 2.14 | |
| 9 | V1 `Attack4` | 2.300 | 1.300 / 1.500 | 1.37 / 1.58 | |
| 10 | V2 `Attack2` | 2.333 | 1.233 / 1.567 | 1.30 / 1.65 | |
| 11 | V2 `Attack6` | 2.900 | 0.667 / 1.233 / 2.133 | 0.70 / 1.30 / 2.25 | |
| 12 | V2 `Attack3` | 2.833 | 1.500 / 1.867 | 1.58 / 1.97 | |
| 13 | V1 `Attack6` | 2.900 | 1.433 / 1.900 | 1.51 / 2.00 | |
| 14 | V1 `Attack3` | 2.967 | 1.167 / 2.333 | 1.23 / 2.46 | |
| 15 | V1 `Attack10` | 2.933 | 1.167 / 2.400 | 1.23 / 2.53 | |
| 16 | V2 `Attack8` | 3.100 | 1.667 / 2.067 | 1.75 / 2.18 | |
| 17 | V2 `Attack7` | ⚠ 3.800 — **1.5 s longer than its stages; contains motion no stage covers** | 2.333 / 2.133 | 2.46 / 2.25 | |
| 18 | V1 `Attack7` | 4.700 | 2.333 / 1.500 / 2.667 | 2.46 / 1.58 / 2.81 | |

**Note on the current roster:** hits 2 and 3 are V3 `Attack4` `Stage2` (1.933, 2.03×) and `Stage3`
(1.000, 1.05×). `Stage3` is recorded as a **shield bash, fairly stationary** — if the string reads
as not-quite-a-sword-combo, that is why, and it is why the third hit contributes no forward motion.

---

## Job B — heavy and charged candidates

Judge on **anticipation apex**, **strike weight**, and **tail length**. Length is not a criterion.

### Pass 1 — unreviewed single moves

| # | Clip | Length | Apex? | Heavy / charged / no | Notes |
|---|---|---:|---|---|---|
| 1 | V2 `Attack4` | 2.067 | | | |
| 2 | V1 `Attack9` | 1.500 | | | |
| 3 | V2 `Attack9` | 1.400 | | | |
| 4 | V2 `Attack10` | 3.233 | | | |
| 5 | V1 `Attack2` | 3.100 | | | |
| 6 | V1 `Attack5` | 3.667 | | | |
| 7 | V1 `Attack1` | 5.167 | | | Long — likely charged material |
| 8 | V1 `Attack8` | 6.600 | | | Longest in the bundle; most probable charged anticipation |

### Pass 3 — reopened and standing V3 candidates

| # | Clip | Length | Recorded | Verdict |
|---|---|---:|---|---|
| 9 | **V3 `Attack8_Stage3_Complete`** | 2.233 | **"Stepping overhead, top-right → bottom-left. Windup cocks the arm back on the right, then swings forward over the right shoulder — the most standard windup found, and the closest match to the chosen light"** — the standing favourite, now judged on its own merits | |
| 10 | V3 `Attack7_Stage1_Complete` | 1.767 | "Big stepping spinning overhead, **top-left → bottom-right**. Heavy candidate, but its windup resembles no light we have" — **reopened** | |
| 11 | V3 `Attack8_Stage2_Complete` | 1.500 | "windup crosses the body, so it blends with less" — **reopened**, blend-cost note still applies | |

### Pass 2 — terminal stages of unreviewed families

Noted while watching Pass 2 for Job A. The last stage of a combo is usually its biggest hit.

| Family | Terminal stage | Length | Verdict |
|---|---|---:|---|
| V1 `Attack3` | S2 | 2.333 | |
| V1 `Attack4` | S2 | 1.500 | |
| V1 `Attack6` | S2 | 1.900 | |
| V1 `Attack7` | S3 | 2.667 | |
| V1 `Attack10` | S2 | 2.400 | |
| V2 `Attack1` | S5 | 2.033 | |
| V2 `Attack2` | S2 | 1.567 | |
| V2 `Attack3` | S2 | 1.867 | |
| V2 `Attack5` | S4 | 1.200 | |
| V2 `Attack6` | S3 | 2.133 | |
| V2 `Attack7` | S2 | 2.133 | |
| V2 `Attack8` | S2 | 2.067 | |

---

## Provenance

All durations are `sequenceLength` read directly off each asset **2026-08-18**, 81 assets, zero
errors — not from filenames and not remembered. All three packs share `SK_Mannequin`, the same
skeleton the montages use, so any of them can be repointed into a montage segment.

Paths follow `/Game/GDHBundle/SwordShield/<Folder>/Animation/IP/AS_<Pack>_Attack<N>[_Stage<M>_Complete]_IP`,
where folder to pack is `SwordShieldAnimV1` to `SwordAndShieldAnimV1`, `SwordShieldAnimV2` to
`SwordShieldAnimV2`, `SwordShieldAnimV3` to `SwordSwordAnimV3`. The `_RM` twins exist alongside if
travel needs seeing; `_IP` is mandatory for anything that ships (root motion suppresses the lunge).

**Stage counts, counted not remembered:** V2 `Attack1` has **five** stages and V2 `Attack5` has
**four**. `CLAUDE.md` currently says V3 `Attack4` is "the only four-stage family" — true of V1 and
V3 when measured 2026-08-11, false of the migrated set since V2 landed on 2026-08-11. Regenerate
the counts with:

    find Content/GDHBundle -path "*/IP/*" -name "AS_*_Attack*_Stage*_Complete_IP.uasset" \
      | sed 's|.*/AS_||; s|_Stage.*||' | sort | uniq -c
