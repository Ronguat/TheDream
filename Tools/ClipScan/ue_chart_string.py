"""Chart the real skeleton through a real string, slowed, the way the knockdown pass did.

No model of the blend: the anim graph renders it, this samples what it produced. Records every
tick unconditionally for a wide window; finding the transitions inside it is a post-processing
job, because a gap detector that never fires costs a whole run to discover.
"""
import os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "p4_chart.tsv")
DILATION = 0.15
MAX_TICKS = 2600
BONES = ["hand_r", "foot_l", "foot_r", "pelvis"]
M = "/Game/TheDream/Combat/Animations/"

sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
gw = sub.get_game_world()
pawn = None
for a in unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.Actor):
    if a.get_name() == "BP_TrainingDummy_C_2":
        pawn = a
if pawn is None:
    raise RuntimeError("attacker not found")
mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
ai = mesh.get_anim_instance()
mont = [unreal.load_asset(M + n + "." + n) for n in ("AM_Attack", "AM_Attack_S2", "AM_Attack_S3")]

unreal.GameplayStatics.set_global_time_dilation(gw, DILATION)
st = {"rows": [], "h": None}


def finish():
    if st["h"] is not None:
        unreal.unregister_slate_post_tick_callback(st["h"])
        st["h"] = None
    unreal.GameplayStatics.set_global_time_dilation(gw, 1.0)
    with open(OUT, "w") as fh:
        fh.write("t\tS1\tS2\tS3\tslotW\t" + "\t".join(BONES) + "\n")
        for r in st["rows"]:
            fh.write(r + "\n")
    unreal.log("chart wrote %d rows" % len(st["rows"]))


def on_tick(delta):
    try:
        pos = [ai.montage_get_position(m) if ai.montage_is_playing(m) else -1.0 for m in mont]
        cells = [f"{unreal.GameplayStatics.get_time_seconds(gw):.4f}"]
        cells += [f"{p:.4f}" for p in pos]
        cells.append(f"{ai.blueprint_get_slot_montage_local_weight('DefaultSlot'):.3f}")
        for b in BONES:
            v = mesh.get_socket_transform(b, unreal.RelativeTransformSpace.RTS_COMPONENT).translation
            cells.append(f"{v.x:.2f},{v.y:.2f},{v.z:.2f}")
        st["rows"].append("\t".join(cells))
        if len(st["rows"]) >= MAX_TICKS:
            finish()
    except Exception as exc:
        unreal.log_error("chart: %s" % exc)
        finish()


st["h"] = unreal.register_slate_post_tick_callback(on_tick)
print("chart armed, dilation", DILATION, "ticks", MAX_TICKS)
print("DONE")
