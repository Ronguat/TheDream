# Clip scan — what a candidate animation can be screened on without an eye

Four editor-Python scripts, run through `Tools/AnimPipeline/run-in-editor.py`. They answer the
*measurable* half of clip selection and are explicit about not answering the rest.

| Script | Answers |
|---|---|
| `ue_clip_profile.py` | hand speed profile of named clips: strike time, windup apex |
| `ue_clip_screen.py` | every migrated SwordShield attack clip against the timeline windows; writes `p2_screen.tsv` |
| `ue_clip_blend.py` | blend cost from each light into each survivor at its fitted entry; writes `p2_blend.tsv` |
| `ue_pose_distance.py` | the same comparison for a named shortlist, with entry sweeps |

**The windows come from the ladder and are not free.** A tier clip is entered partway and replaced
at the next escalation, so it must carry its entry to its strike in exactly that branch's window —
0.250 s for heavy, 0.450 s for charged. Entry is therefore *derived*, `strike - T`, not chosen.

**Blend cost is normalised against each light's own hand travel over the same 150 ms**, so 1.00
means the blend asks exactly what that clip was already doing. Under 1.00 is not "good", only
"asks no more than the animation already does".

**What none of this decides.** Whether a clip reads as a heavy, whether its anticipation is legible
as a tell, and whether a V1/V2 clip sits beside V3 lights. Hand cost and foot cost also rank
differently and nothing here adjudicates between them. Treat the output as a shortlist for the eye,
never as a ranking to pick from.
