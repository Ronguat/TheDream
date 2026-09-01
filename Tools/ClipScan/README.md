# Clip scan — what a candidate animation can be screened on without an eye

Editor-Python scripts, run through `Tools/AnimPipeline/run-in-editor.py`. They answer the
*measurable* half of clip selection and are explicit about not answering the rest.

| Script | Answers |
|---|---|
| `ue_clip_profile.py` | hand speed profile of named clips: strike time, windup apex |
| `ue_clip_screen.py` | every migrated SwordShield attack clip against the timeline windows |
| `ue_crossfade_cost.py` | **the blend metric.** Velocity of the rendered pose across a crossfade |
| `ue_clip_blend.py` | *superseded, kept for the screen it drives.* See the warning below |
| `ue_pose_distance.py` | *superseded, same fault* |

**The timeline window is derived, not chosen.** A tier clip is entered partway and replaced at the
next escalation, so it carries its entry to its strike in exactly that branch's window — 0.250 s
heavy, 0.450 s charged — which makes the entry point `strike - T`.

## The blend metric, and why the first one was wrong

**Both clips move during a crossfade.** `ue_pose_distance.py` and `ue_clip_blend.py` compare a
source pose against the target's *entry* pose, which is where the target carries almost no weight —
and the target is racing away from it. A light reaches its release at 0.200 s while its montage's
`BlendIn` is 0.25 s, so at 3.344x the incoming clip has covered 0.418 s of its own timeline by the
blend's midpoint. Those two scripts sample the wrong end and their numbers do not mean what they
appear to.

`ue_crossfade_cost.py` models what is actually rendered — `lerp(source(t), target(t), w(t))` over
an eased weight — and reports the **velocity of that mix**. A pop is the mix moving faster than
either clip does alone.

**Read it against the shipping string blends, which are the calibration.** L1→L2 and L2→L3 measure
**3241** and **5316** cm/s of blended hand movement, and both are judged good in play. That band is
what "fine" looks like; the absolute figures are large because a short blend between two fast clips
is inherently fast, so only the comparison carries meaning.

**What none of this decides.** Whether a clip reads as a heavy, whether its anticipation is legible
as a tell, and whether a V1/V2 clip sits beside V3 lights. The ratio column divides by
`max(peak(src), peak(dst))`, which varies per pair, so prefer the raw cm/s when comparing across
pairs. Treat the output as a shortlist for the eye, never as a ranking to pick from.
