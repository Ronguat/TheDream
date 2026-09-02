"""Fit tier montages to the phases they serve. Reads ue_fit_tier_montages.json beside this file:

  [{"montage": "/Game/TheDream/Combat/Animations/AM_Charged1", "clip": "/Game/.../AS_..._IP",
    "notify": 1.600, "release": 0.150, "recovery": 0.600, "blend_in": 0.20, "inertial": true}, ...]

Per entry: the montage is created from the clip when it does not exist (an existing one is not
repointed here; that is a toolset write, see Docs/Anim-Pipeline.md); its ReleaseTrack gets one
Release Window at notify for release seconds; blend-in is set; blend modes go to Inertialization
when asked; and BlendOutTriggerTime is set to length - (notify + release) - recovery, the value at
which the ability's recovery derivation lands on play rate 1.0. Prints the fit and saves by name.
"""
import json, os, unreal

AL = unreal.AnimationLibrary
HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = json.load(open(os.path.join(HERE, "ue_fit_tier_montages.json")))
TRACK = "ReleaseTrack"
tools = unreal.AssetToolsHelpers.get_asset_tools()
saved = []

for e in SPEC:
    path, name = e["montage"].rsplit("/", 1)
    m = unreal.load_asset(e["montage"])
    if not m:
        clip = unreal.load_asset(e["clip"])
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("target_skeleton", clip.get_editor_property("skeleton"))
        factory.set_editor_property("source_animation", clip)
        m = tools.create_asset(name, path, unreal.AnimMontage, factory)
        print("CREATED", m.get_path_name(), "from", clip.get_name())
    m.modify()
    tracks = m.get_editor_property("slot_anim_tracks")
    seg = tracks[0].get_editor_property("anim_track").get_editor_property("anim_segments")[0]
    ref = seg.get_editor_property("anim_reference")
    L = m.get_play_length()
    notify, rel, rec = float(e["notify"]), float(e.get("release", 0.15)), float(e["recovery"])
    # Exactly one Release Window on the whole montage: every track is cleared first, whatever the
    # earlier authoring named it, then the window goes on ReleaseTrack.
    for track in [str(t) for t in AL.get_animation_notify_track_names(m)]:
        AL.remove_animation_notify_events_by_track(m, track)
    if TRACK not in [str(t) for t in AL.get_animation_notify_track_names(m)]:
        AL.add_animation_notify_track(m, TRACK, unreal.LinearColor.WHITE)
    AL.add_animation_notify_state_event(m, TRACK, notify, rel, unreal.AnimNotifyState_MeleeWindow)
    bi = m.get_editor_property("blend_in")
    bi.set_editor_property("blend_time", float(e.get("blend_in", 0.10)))
    m.set_editor_property("blend_in", bi)
    if e.get("inertial", True):
        m.set_editor_property("blend_mode_in", unreal.MontageBlendMode.INERTIALIZATION)
        m.set_editor_property("blend_mode_out", unreal.MontageBlendMode.INERTIALIZATION)
    trig = L - (notify + rel) - rec
    m.set_editor_property("blend_out_trigger_time", round(trig, 4) if trig >= 0.0 else -1.0)
    events = [(str(ev.get_editor_property("notify_name")), AL.get_anim_notify_event_trigger_time(ev), AL.get_anim_notify_event_duration(ev))
              for ev in AL.get_animation_notify_events(m)]
    print("%-12s clip %-46s len %.4f window %.4f+%.4f blendIn %.2f modes %s/%s trigger %.4f%s  events %s"
          % (name, ref.get_name()[-46:], L, notify, rel, m.get_editor_property("blend_in").get_editor_property("blend_time"),
             str(m.get_editor_property("blend_mode_in")).rsplit(".", 1)[-1], str(m.get_editor_property("blend_mode_out")).rsplit(".", 1)[-1],
             m.get_editor_property("blend_out_trigger_time"), "" if trig >= 0 else " (TAIL TOO SHORT for a rate-1 recovery)",
             "; ".join("%s@%.4f+%.4f" % ev for ev in events)))
    if e.get("save", True):
        saved.append(m.get_outermost())
print("SAVED", unreal.EditorLoadingAndSavingUtils.save_packages(saved, False) if saved else "nothing")
print("DONE")
