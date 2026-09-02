"""Chart the rendered right hand through a scripted escalation on the live PIE player pawn, one run
per call, optionally shooting stills. Reads ue_chart_ab.json beside this script:

  {"mode": "std"|"inertial", "scen": "H2", "tag": "", "shots": 0.0, "shots_from": 0.0,
   "shots_to": 9e9, "tail": 0.6, "label": "", "arm": 0, "montages": [], "scratch_abp": ""}

mode sets every listed montage's blend modes to Inertialization or Standard for the run and
restores them after; "scratch_abp" swaps the pawn onto that AnimBlueprint's class for the run. scen picks the
input plan below. shots > 0 takes a screenshot every that many game seconds inside
[shots_from, shots_to], named <label>_<ms>.png under Saved/Screenshots/WindowsEditor/, and arm > 0
shortens the camera boom for them. Writes Saved/ClipScan/ab_<mode>_<scen><tag>.tsv: one row per
slate tick under time dilation with every montage's position and the hand in component space.
The pawn is teleported to its spawn before each run; every attack lunges it forward.

Plans, in game seconds from the first tick:
  H1  hold 0.00-0.22            light 1 escalates to heavy at 0.150, commits at 0.350
  C1  hold 0.00-0.85            escalates twice, commits at 0.750
  H2  tap 0.00, hold 0.52-0.74  light 2 escalates to heavy
  C2  tap 0.00, hold 0.52-1.37  light 2 escalates twice
  H3  tap 0.00, tap 0.52, hold 1.04-1.26   light 3 escalates to heavy
  C3  tap 0.00, tap 0.52, hold 1.04-1.89   light 3 escalates twice
Read the TSV with ue_ab_metrics.py.
"""
import json, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
A = json.load(open(os.path.join(HERE, "ue_chart_ab.json")))
MODE, SCEN, TAG = A.get("mode", "std"), A.get("scen", "H2"), A.get("tag", "")
DIL = 0.10
OUT_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "ClipScan")
os.makedirs(OUT_DIR, exist_ok=True)
OUT = os.path.join(OUT_DIR, "ab_%s_%s%s.tsv" % (MODE, SCEN, TAG))
M = "/Game/TheDream/Combat/Animations/"
NAMES = ["AM_Attack", "AM_Attack_S2", "AM_Attack_S3", "AM_Heavy1", "AM_Heavy2", "AM_Heavy3",
         "AM_Charged1", "AM_Charged2", "AM_Charged3"]
STD, INERT = unreal.MontageBlendMode.STANDARD, unreal.MontageBlendMode.INERTIALIZATION
PLAN = {
    "H1": [(0.00, "hold"), (0.22, "release")],
    "C1": [(0.00, "hold"), (0.85, "release")],
    "H2": [(0.00, "tap"), (0.52, "hold"), (0.74, "release")],
    "C2": [(0.00, "tap"), (0.52, "hold"), (1.37, "release")],
    "H3": [(0.00, "tap"), (0.52, "tap"), (1.04, "hold"), (1.26, "release")],
    "C3": [(0.00, "tap"), (0.52, "tap"), (1.04, "hold"), (1.89, "release")],
}[SCEN]
END = PLAN[-1][0] + float(A.get("tail", 0.60))
SHOTS, SHOT_FROM, SHOT_TO = float(A.get("shots", 0.0)), float(A.get("shots_from", 0.0)), float(A.get("shots_to", 9e9))
LABEL = A.get("label") or "%s_%s%s" % (MODE, SCEN, TAG)
ARM = float(A.get("arm", 0.0))

sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
gw = sub.get_game_world()
pc = unreal.GameplayStatics.get_player_controller(gw, 0)
pawn = pc.get_controlled_pawn()
mesh = pawn.get_component_by_class(unreal.SkeletalMeshComponent)
HOME = unreal.Vector(-3000.0, -3000.0, 100.0)
pawn.set_actor_location_and_rotation(HOME, unreal.Rotator(0.0, 0.0, 0.0), False, True)
IA = unreal.load_asset("/Game/TheDream/Combat/Input/IA_Attack.IA_Attack")
MONT = [(n, unreal.load_asset(M + n + "." + n)) for n in NAMES]
for extra in A.get("montages", []):
    m = unreal.load_asset(extra)
    if m:
        MONT.append((m.get_name(), m))
MONT = [(n, m) for n, m in MONT if m]
orig_class = unreal.load_asset(M + "ABP_Combat").generated_class()
orig_modes = {}
if A.get("scratch_abp"):
    mesh.set_anim_instance_class(unreal.load_asset(A["scratch_abp"]).generated_class())
WANT = INERT if MODE == "inertial" else STD
for n, m in MONT:
    orig_modes[n] = (m.get_editor_property("blend_mode_in"), m.get_editor_property("blend_mode_out"))
    m.set_editor_property("blend_mode_in", WANT)
    m.set_editor_property("blend_mode_out", WANT)
ai = mesh.get_anim_instance()
boom = pawn.get_component_by_class(unreal.SpringArmComponent)
orig_arm = boom.get_editor_property("target_arm_length") if boom else None
if boom and ARM > 0:
    boom.set_editor_property("target_arm_length", ARM)
unreal.log("AB %s/%s anim class %s" % (MODE, SCEN, ai.get_class().get_name()))

unreal.GameplayStatics.set_global_time_dilation(gw, DIL)
st = {"rows": [], "h": None, "t0": None, "step": 0, "holding": False, "next_shot": SHOT_FROM, "shots": 0}


def restore():
    unreal.GameplayStatics.set_global_time_dilation(gw, 1.0)
    try:
        if st["holding"]:
            unreal.TDInputTools.stop_hold(pc, IA)
    except Exception:
        pass
    for n, m in MONT:
        if n in orig_modes:
            m.set_editor_property("blend_mode_in", orig_modes[n][0])
            m.set_editor_property("blend_mode_out", orig_modes[n][1])
    if A.get("scratch_abp"):
        mesh.set_anim_instance_class(orig_class)
    if boom and orig_arm is not None:
        boom.set_editor_property("target_arm_length", orig_arm)


def finish():
    if st["h"] is not None:
        unreal.unregister_slate_post_tick_callback(st["h"])
        st["h"] = None
    restore()
    with open(OUT, "w") as fh:
        fh.write("t\t" + "\t".join(n for n, _ in MONT) + "\thx\thy\thz\n")
        for r in st["rows"]:
            fh.write(r + "\n")
    unreal.log("AB %s/%s wrote %d rows to %s, %d shots" % (MODE, SCEN, len(st["rows"]), OUT, st["shots"]))


def on_tick(delta):
    try:
        now = unreal.GameplayStatics.get_time_seconds(gw)
        if st["t0"] is None:
            st["t0"] = now
        el = now - st["t0"]
        while st["step"] < len(PLAN) and el >= PLAN[st["step"]][0]:
            kind = PLAN[st["step"]][1]
            if kind == "tap":
                unreal.TDInputTools.inject_action(pc, IA, 1.0)
            elif kind == "hold":
                unreal.TDInputTools.start_hold(pc, IA, 1.0); st["holding"] = True
            elif kind == "release":
                unreal.TDInputTools.stop_hold(pc, IA); st["holding"] = False
            st["step"] += 1
        pos = [ai.montage_get_position(m) if ai.montage_is_playing(m) else -1.0 for _, m in MONT]
        v = mesh.get_socket_transform("hand_r", unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        st["rows"].append("%.4f\t%s\t%.2f\t%.2f\t%.2f" % (el, "\t".join("%.4f" % p for p in pos), v.x, v.y, v.z))
        if SHOTS > 0 and el >= st["next_shot"] and el <= SHOT_TO:
            unreal.AutomationLibrary.take_high_res_screenshot(960, 540, "%s_%04d.png" % (LABEL, int(round(el * 1000))))
            st["shots"] += 1
            st["next_shot"] += SHOTS
        if el >= END:
            finish()
    except Exception as exc:
        unreal.log_error("AB chart: %s" % exc)
        finish()


st["h"] = unreal.register_slate_post_tick_callback(on_tick)
print("AB armed %s/%s -> %s" % (MODE, SCEN, OUT))
print("DONE")
