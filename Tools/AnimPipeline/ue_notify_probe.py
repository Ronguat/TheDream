"""A3: prove AnimationLibrary writes and reads a Release Window on a montage.

Duplicates AM_Rise (a non-attack montage, so no inherited window) into Scratch/, adds a
Release Window at 0.30 s for 0.35 s, reads it back, saves, and prints what it found.
"""
import unreal

SRC = "/Game/TheDream/Combat/Animations/AM_Rise"
DST_PATH, DST_NAME = "/Game/TheDream/Combat/Animations/Scratch", "AM_NotifyProbe"
TRACK, START, DURATION = "ReleaseTrack", 0.30, 0.35
AL = unreal.AnimationLibrary

tools = unreal.AssetToolsHelpers.get_asset_tools()
existing = unreal.load_asset(f"{DST_PATH}/{DST_NAME}")
montage = existing or tools.duplicate_asset(DST_NAME, DST_PATH, unreal.load_asset(SRC))
print("MONTAGE", montage.get_path_name(), "existing" if existing else "duplicated")

tracks = list(AL.get_animation_notify_track_names(montage))
print("TRACKS before", [str(t) for t in tracks])
montage.modify()
if TRACK not in [str(t) for t in tracks]:
    AL.add_animation_notify_track(montage, TRACK, unreal.LinearColor.WHITE)
AL.remove_animation_notify_events_by_track(montage, TRACK)
notify = AL.add_animation_notify_state_event(montage, TRACK, START, DURATION, unreal.AnimNotifyState_MeleeWindow)
print("ADDED", type(notify).__name__ if notify else None)

events = AL.get_animation_notify_events(montage)
for ev in events:
    print("EVENT name=%s class=%s trigger=%.4f duration=%.4f" % (
        ev.get_editor_property("notify_name"),
        type(ev.get_editor_property("notify_state_class")).__name__,
        AL.get_anim_notify_event_trigger_time(ev),
        AL.get_anim_notify_event_duration(ev)))
print("TRACKS after", [str(t) for t in AL.get_animation_notify_track_names(montage)])

# modify() above already dirtied the package
saved = unreal.EditorLoadingAndSavingUtils.save_packages([montage.get_outermost()], False)
print("SAVED", saved)
