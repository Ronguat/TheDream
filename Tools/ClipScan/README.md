# Clip scan — what a candidate animation can be screened on without an eye

Editor-Python scripts, run through `Tools/AnimPipeline/run-in-editor.py`.

| Script | Answers |
|---|---|
| `ue_clip_profile.py` | hand speed profile of named clips: strike time, windup apex |
| `ue_clip_screen.py` | every migrated SwordShield attack clip against the timeline windows |
| `ue_rank_per_light.py` | **the ranking.** Handover gap per light, against the shipping reference |
| `ue_chart_string.py` | charts the real skeleton through a real string in PIE, slowed |
| `ue_handover_profile.py` | velocity profile through each handover, in 25 ms bins |
| `ue_crossfade_cost.py`, `ue_clip_blend.py`, `ue_pose_distance.py` | superseded; see below |

**The timeline window is derived, not chosen.** A tier clip is entered partway and replaced at the
next escalation, so it carries its entry to its strike in exactly that branch's window — 0.250 s
heavy, 0.450 s charged — making the entry point `strike - T`.

## The metric, and the two that were wrong

**Measure the gap at the handover, not motion across the blend.** The blend-attributable window is
roughly the first **100 ms**; past that the incoming clip's own motion dominates and swamps any
window total. `ue_crossfade_cost.py` measured peak velocity across the whole blend and ranked the
two shipping handovers *backwards* — it was reporting how fast the incoming attack is, not how far
the hand had to jump. `ue_clip_blend.py` and `ue_pose_distance.py` normalise against "natural
travel", which has no relation to what looks acceptable.

**The reference is what already ships.** Both string handovers are judged good in play:

| handover | hand gap | worst foot gap |
|---|---:|---:|
| L1 → L2 | **65.2 cm** | **27.3 cm** |
| L2 → L3 | 29.7 cm | 12.9 cm |

A candidate at or under those is asking no more of the blend than the game already asks. **Two
reference points is a thin calibration** — widen it before trusting the metric far from that range.

**Verify against the rendered skeleton, not the model.** `ue_chart_string.py` samples bone
positions per tick during PIE under time dilation, the way the knockdown pass did. Every analytic
model of the blend carries approximation error — the weight curve, the anim graph's locomotion
layer, the recovery rates — and charting the real thing removes all of it at once. It is also what
caught both wrong metrics.

**What none of this decides.** Whether a clip reads as a heavy, whether its anticipation is a
legible tell, and whether a V1/V2 clip sits beside V3 lights. Note also that a top pick from the
*same family* as a light may be the same motion under another name, which would be a coil by
definition — the ranking flags siblings but does not check content overlap.
