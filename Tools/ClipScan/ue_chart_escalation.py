"""Chart a real L2 -> H2 escalation on the player, driving the input rather than reasoning.

The debug attacker holds uniformly across taps and so cannot produce tap-then-hold. UTDInputTools
injects it. Records the rendered hand alongside every montage that could be contributing, so the
source pose at the hand-off is measured rather than assumed -- which is where the last four
attempts went wrong.
"""
import unreal

AL = unreal.AnimationLibrary
DIL = 0.10
M = "/Game/TheDream/Combat/Animations/"
sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
gw = sub.get_game_world()
pc = unreal.GameplayStatics.get_player_controller(gw, 0)
pawn = pc.get_controlled_pawn()
mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
ai = mesh.get_anim_instance()
IA = unreal.load_asset("/Game/TheDream/Combat/Input/IA_Attack.IA_Attack")
MONT = [(n, unreal.load_asset(M + n + "." + n)) for n in
        ("AM_Attack", "AM_Attack_S2", "AM_Heavy1", "AM_Heavy2")]

unreal.GameplayStatics.set_global_time_dilation(gw, DIL)
st = {"rows": [], "h": None, "t0": None, "tap": False, "hold": False, "rel": False}
OUT = __file__.replace("p51_chartH2.py", "p51_chartH2.tsv")


def finish():
    if st["h"] is not None:
        unreal.unregister_slate_post_tick_callback(st["h"])
        st["h"] = None
    try:
        unreal.TDInputTools.stop_hold(pc, IA)
    except Exception:
        pass
    unreal.GameplayStatics.set_global_time_dilation(gw, 1.0)
    with open(OUT, "w") as fh:
        fh.write("t\t" + "\t".join(n for n, _ in MONT) + "\thand\n")
        for r in st["rows"]:
            fh.write(r + "\n")
    unreal.log("H2 chart wrote %d rows" % len(st["rows"]))


def on_tick(delta):
    try:
        now = unreal.GameplayStatics.get_time_seconds(gw)
        if st["t0"] is None:
            st["t0"] = now
            unreal.TDInputTools.inject_action(pc, IA, 1.0)
            st["tap"] = True
            return
        el = now - st["t0"]
        if not st["hold"] and el >= 0.52:
            unreal.TDInputTools.start_hold(pc, IA, 1.0)
            st["hold"] = True
        if st["hold"] and not st["rel"] and el >= 0.74:
            unreal.TDInputTools.stop_hold(pc, IA)
            st["rel"] = True
        pos = [ai.montage_get_position(m) if ai.montage_is_playing(m) else -1.0 for _, m in MONT]
        v = mesh.get_socket_transform("hand_r", unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        st["rows"].append("%.4f\t%s\t%.2f,%.2f,%.2f"
                          % (el, "\t".join("%.4f" % p for p in pos), v.x, v.y, v.z))
        if el >= 1.30:
            finish()
    except Exception as exc:
        unreal.log_error("H2 chart: %s" % exc)
        finish()


st["h"] = unreal.register_slate_post_tick_callback(on_tick)
print("H2 chart armed: tap at 0, hold 0.52-0.74")
print("DONE")
