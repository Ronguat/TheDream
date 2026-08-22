# Plan — Animation authoring

**Objective: the capability to author animation this project does not have, with `AM_GetUpAttack`
as its first output.** Knockdown's sub-slice F is blocked on that montage. The library has no
single clip for it — searched exhaustively twice, by the designer and by a prior session.

**The justification is not the one clip.** It is that Polish's bespoke windup pass is currently
implemented as *selection under constraint* — the 2026-08-18 entry asks a candidate for "a legible
anticipation apex to blend into" and notes a read that "widens the pool considerably." That is the
shape of a search problem. Authoring dissolves it, and the pass stops being bespoke-by-compromise.
Past the roster, a second weapon makes this permanent infrastructure.

## What exists today, measured

Confirmed 2026-08-21 unless noted.

- **The Unreal Python API is reachable.** `PythonScriptPlugin` is an engine default — enabled while
  absent from the uproject's plugin array. `py import unreal; print(...)` returned `5.8.1` through
  the status-bar Cmd box, read back via `LogsToolset` on category `LogPython`.
- **`IKRig` is enabled** and runs its own `init_unreal.py` at startup, so that API is loaded.
- **There is no IK Rig or IK Retargeter MCP toolset.** Retargeting is not reachable through the
  toolsets at all; Python is the only candidate route.
- **Multi-segment montages are precedented.** `AM_Dodge` holds eight `animSegments` in one slot
  track, sequential from `startPos` 0 to 5.833. Read off the asset 2026-08-21.
- **No clip in this project is trimmed.** All eight of those segments run `animStartTime: 0` to
  `animEndTime` at full clip length. Trimming is one field, not a structure, but it has no
  precedent here.
- **`slotAnimTracks` reads back in full structural detail**, not only writes whole.
- **Montages are ~90% scriptable** — `AssetTools.duplicate` clones with the skeleton intact and the
  segment repoints by writing `slotAnimTracks` whole. Multi-*section* montages are not scriptable,
  and `compositeSections` is unreadable — confirmed again 2026-08-21.
- **Notify placement is a human step**, and `notifies` is not readable by any toolset route. The
  runtime drift warning is what verifies placement.
- **Neither Cascadeur nor Meshy is set up.** No install, no licence, no account.

## Not measured, and everything rests on the first

- **Can Python drive an IK retarget end to end?** `IKRetargetBatchOperation` is the canonical route
  and is untested here. If this fails, the slice needs a different source of motion and the plan
  changes shape.
- **Is a clip's baked root displacement readable off the asset?** The `_RM` / `_IP` filename states
  it for library clips. Generated clips carry no such convention.
- **Does Cascadeur's MCP toolset exist and register?** Dev-authored, roughly a week old.

## The constraint that shapes every output

**Displacement is authored where the distance drives gameplay** — `Combat-Spec.md`. The get-up
attack is stationary by design, so its authored distance is zero and the clip must not carry
travel of its own.

The failure is silent and total: animation root motion suppresses every root motion source, so a
montage carrying root motion produces **zero** lunge rather than a doubled one. `bEnableRootMotion
= false` is not enough — the displacement stays baked and the mesh drifts off the capsule.
`bForceRootLock = true` is the flag that makes an `_RM` clip behave in place.

**A force-locked clip can foot-slide.** Its body motion was choreographed to cover its own
distance; pinning the root while the capsule travels differently sells a stride nobody is taking.
For a stationary move this does not arise, but it will the moment this pipeline serves a lunging
attack.

## Sub-slices

**A — prove the retarget path.** Retarget one throwaway library clip onto `SKM_Manny` through
Python. Ends in an `AnimSequence` that exists and previews. **This gates everything below**; run it
before any setup work, because a failure here changes what the rest of the plan is.

**B — Cascadeur.** Install, licence, and register its MCP toolset. Ends with the toolset appearing
in `list_toolsets` and answering a call. Human steps throughout — installation, licensing and
account creation are not mine to do.

**C — Meshy.** Account and access. **This has no use case in this slice and none in the rest of
this work.** It is set up now because art enters the conversation if the project survives, and
because the setup cost does not fall later. Deliberately speculative; drop it without consequence
if B runs long.

**D — author the get-up attack clip.** Route determined by A and B. Stationary, in place, no baked
travel. The designer's eye decides when it reads as coming off the floor.

**E — assemble `AM_GetUpAttack`.** Single-segment montage from D's clip, fitted to the authored
duration at derived rates. Human places the Release Window notify; the drift warning verifies it at
runtime.

**F — unblock Knockdown's sub-slice F.** The get-up attack commits and its hitbox opens.

**G — route the docs.** `Animation-Library.md` gains whatever the pipeline establishes about
authored clips; `Working-In-Unreal.md` gains the Python retarget route if A succeeds.

## The regression obligation, decided at plan time

Sub-slice F shipping a real capability means this package owes **`regression-check.sh` scenarios in
the same package**: a get-up attack thrown from the floor, asserting the hitbox opens and the
attack commits. Not a dated trap — the trap list already carries *"the get-up options are built and
none of them is tested,"* whose stated trigger is the animations landing. This package is that
trigger, so it discharges rather than extends it.

## Order, and what is deferred

A first, alone, and read its result before committing to the rest. Then B, D, E, F, G. C wherever
it fits or not at all.

**Deferred: the two-clip composition.** Rise front-half plus `Attack2_Stage2`'s 360° spin is a
documented recipe with a real, unspoken-for candidate, and it is **buildable** — eight segments in
`AM_Dodge` prove the structure and the trim is one field. It is deferred on quality, not
difficulty: the designer's assessment is that it would read badly, and a placeholder reaching
Interplay teaches the wrong tell for a move whose whole read is *from the floor*. It survives as a
**test fixture**, worth building solely if generation stalls long enough that mechanical
verification would otherwise sit idle. It never reaches a feel verdict.
