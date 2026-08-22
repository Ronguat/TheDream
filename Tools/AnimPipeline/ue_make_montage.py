"""Make a single-segment montage from a clip and place its Release Window — E, scripted.

Edit the constants; run through run-in-editor.py. Prints the derived phase rates against
Plan-Knockdown's authored phases and the blend-out check for each phase.
"""
import unreal

AL = unreal.AnimationLibrary
CLIP = "/Game/TheDream/Combat/Animations/Scratch/AS_GetUpAttack_Rough"
DST, NAME = "/Game/TheDream/Combat/Animations/Scratch", "AM_GetUpAttack_Rough"
WINDOW_START_F, WINDOW_END_F, FPS = 19, 24, 30.0
AUTHORED = {"windup": 0.30, "release": 0.35, "recovery": 0.60}   # Plan-Knockdown.md, sub-slice F
TRACK = "ReleaseTrack"

clip = unreal.load_asset(CLIP)
sk = clip.get_editor_property("skeleton")
tools = unreal.AssetToolsHelpers.get_asset_tools()
montage = unreal.load_asset(f"{DST}/{NAME}")
if not montage:
    factory = unreal.AnimMontageFactory()
    factory.set_editor_property("target_skeleton", sk)
    factory.set_editor_property("source_animation", clip)
    montage = tools.create_asset(NAME, DST, unreal.AnimMontage, factory)
print("MONTAGE", montage.get_path_name() if montage else None)
tracks = montage.get_editor_property("slot_anim_tracks")
seg = tracks[0].get_editor_property("anim_track").get_editor_property("anim_segments")[0]
print("  segment ->", seg.get_editor_property("anim_reference").get_name(), "start=%.3f end=%.3f rate=%.2f" % (
    seg.get_editor_property("anim_start_time"), seg.get_editor_property("anim_end_time"), seg.get_editor_property("anim_play_rate")))
length = AL.get_sequence_length(montage)
bo = montage.get_editor_property("blend_out"); bt = bo.get_editor_property("blend_time")
print("  length=%.4f blendOut=%.2f trigger=%.2f autoBlendOut=%s" % (length, bt, montage.get_editor_property("blend_out_trigger_time"), montage.get_editor_property("enable_auto_blend_out")))

montage.modify()
if TRACK not in [str(t) for t in AL.get_animation_notify_track_names(montage)]:
    AL.add_animation_notify_track(montage, TRACK, unreal.LinearColor.WHITE)
AL.remove_animation_notify_events_by_track(montage, TRACK)
start, dur = WINDOW_START_F / FPS, (WINDOW_END_F - WINDOW_START_F) / FPS
AL.add_animation_notify_state_event(montage, TRACK, start, dur, unreal.AnimNotifyState_MeleeWindow)
for ev in AL.get_animation_notify_events(montage):
    print("  NOTIFY %s trigger=%.4f duration=%.4f" % (ev.get_editor_property("notify_name"), AL.get_anim_notify_event_trigger_time(ev), AL.get_anim_notify_event_duration(ev)))
print("  SAVED", unreal.EditorLoadingAndSavingUtils.save_packages([montage.get_outermost()], False))

# the clip-conforms model: each phase's clip span divided by its authored duration
spans = {"windup": start, "release": dur, "recovery": length - start - dur}
for phase, span in spans.items():
    rate = span / AUTHORED[phase]
    boundary = length - bt * rate
    need = {"windup": start, "release": start + dur, "recovery": length}[phase]
    print("  %-8s clip %.3f s / authored %.2f s -> rate %.2fx ; blend-out boundary %.3f vs needed %.3f %s" % (
        phase, span, AUTHORED[phase], rate, boundary, need, "OK" if boundary >= need or phase == "recovery" else "FAILS"))
print("DONE")
