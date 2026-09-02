"""SUPERSEDED: written against the TierAnimations / StringSwings layout, which no longer exists.
GA_Attack authors nine cells; write them with Tools/ValuesSnapshot/ue_seed_cells.py, whose
overrides JSON takes a cell's Montage, EntrySeconds and ReleaseStartSeconds. Kept as the record of
how the heavy sockets were first derived: entry = the montage's notify - 0.250, read off the asset.
"""
import unreal

AL = unreal.AnimationLibrary
DST = "/Game/TheDream/Combat/Animations"
WINDOW = 0.250


def socket(name):
    m = unreal.load_asset("%s/%s" % (DST, name))
    ev = AL.get_animation_notify_events(m)[0]
    start = AL.get_anim_notify_event_trigger_time(ev)
    # A notify closer to the clip start than the window forces a negative entry; clamp it,
    # since the montage cannot begin before 0 and the shortfall just costs a little runway.
    return unreal.TDTierAnimation(montage=m, entry_seconds=max(0.0, round(start - WINDOW, 4)),
                                  release_start_seconds=round(start, 4)), name, start


BP = unreal.load_asset("/Game/TheDream/Combat/Abilities/GA_Attack")
cdo = unreal.get_default_object(BP.generated_class())

h1, n1, s1 = socket("AM_Heavy1")
h2, n2, s2 = socket("AM_Heavy2")
h3, n3, s3 = socket("AM_Heavy3")

cdo.set_editor_property("TierAnimations", [h1])          # H1: swing 0, branch 1
sw = cdo.get_editor_property("StringSwings")


def rebuild(old, tiers):
    return unreal.TDStringSwing(
        montage=old.get_editor_property("Montage"),
        release_start_seconds=old.get_editor_property("ReleaseStartSeconds"),
        coil_end_seconds=old.get_editor_property("CoilEndSeconds"),
        damage=old.get_editor_property("Damage"),
        stamina_damage=old.get_editor_property("StaminaDamage"),
        blockstun_seconds=old.get_editor_property("BlockstunSeconds"),
        hitstun_seconds=old.get_editor_property("HitstunSeconds"),
        knockdown_type=old.get_editor_property("KnockdownType"),
        parry_lockout_seconds=old.get_editor_property("ParryLockoutSeconds"),
        recovery_seconds=old.get_editor_property("RecoverySeconds"),
        release_seconds=old.get_editor_property("ReleaseSeconds"),
        lunge_distance_cm=old.get_editor_property("LungeDistanceCm"),
        lunge_duration_seconds=old.get_editor_property("LungeDurationSeconds"),
        hitboxes=old.get_editor_property("Hitboxes"),
        tier_animations=tiers,
    )


cdo.set_editor_property("StringSwings", [rebuild(sw[0], [h2]), rebuild(sw[1], [h3])])
cdo.modify(); BP.modify()
unreal.BlueprintEditorLibrary.compile_blueprint(BP)

print("H1  swing 0 branch 1  ->", end=" ")
t = cdo.get_editor_property("TierAnimations")[0]
print("%s entry %.4f release %.4f" % (t.get_editor_property("Montage").get_name(),
                                      t.get_editor_property("EntrySeconds"),
                                      t.get_editor_property("ReleaseStartSeconds")))
for i, s in enumerate(cdo.get_editor_property("StringSwings")):
    ta = s.get_editor_property("TierAnimations")
    for t in ta:
        print("H%d  swing %d branch 1  -> %s entry %.4f release %.4f"
              % (i + 2, i + 1, t.get_editor_property("Montage").get_name(),
                 t.get_editor_property("EntrySeconds"), t.get_editor_property("ReleaseStartSeconds")))
print("charged sockets: none (branch 2 unpopulated -- heavies will pace to the charged release)")
print("SAVED", unreal.EditorLoadingAndSavingUtils.save_packages(
    list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()), False))
print("DONE")
