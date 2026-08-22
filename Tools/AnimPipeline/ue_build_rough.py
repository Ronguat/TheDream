"""Build the get-up attack rough as a baked AnimSequence: ground pose -> spin segment -> idle.

Frames 0-3 hold AM_Knockdown's ground pose, 4-9 blend into Attack2_Stage2_IP frames 8-28
(placed at 10-30), 31-44 blend into the idle, 45-46 hold it. Root stays at the idle's root.
Verifies the three anchors against the source poses, exports FBX. Run through run-in-editor.py.
"""
import math, os, unreal

AL = unreal.AnimationLibrary
V3 = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation"
DST = "/Game/TheDream/Combat/Animations/Scratch"
OUT = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/export"
GROUND = unreal.load_asset(f"{V3}/RM/AS_SwordSwordAnimV3_Death_Bw_RM")
IDLE = unreal.load_asset(f"{V3}/IP/AS_SwordSwordAnimV3_Idle1_IP")
SPIN = unreal.load_asset(f"{V3}/IP/AS_SwordSwordAnimV3_Attack2_Stage2_Complete_IP")
SPIN_FIRST, SPIN_LAST, SPIN_AT = 8, 28, 10
HOLD0, BLEND_OUT_START, IDLE_AT, LAST = 3, 31, 45, 46

print("DOC set_bone_track_keys:", (unreal.AnimationDataController.set_bone_track_keys.__doc__ or "")[:220].replace("\n", " | "))
print("DOC set_number_of_frames:", (unreal.AnimationDataController.set_number_of_frames.__doc__ or "")[:200].replace("\n", " | "))

names = [str(n) for n in AL.get_animation_track_names(IDLE)]
def poses_at(seq, t): return dict(zip(names, AL.get_bone_poses_for_time(seq, names, t, False)))
ground, idle = poses_at(GROUND, AL.get_sequence_length(GROUND)), poses_at(IDLE, 0.0)
spin = [poses_at(SPIN, f / 30.0) for f in range(SPIN_FIRST, SPIN_LAST + 1)]

def slerp(a, b, t):
    d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w
    if d < 0: b, d = unreal.Quat(-b.x, -b.y, -b.z, -b.w), -d
    if d > 0.9995:
        q = unreal.Quat(a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t)
    else:
        th = math.acos(d); s = math.sin(th); wa, wb = math.sin((1-t)*th)/s, math.sin(t*th)/s
        q = unreal.Quat(a.x*wa+b.x*wb, a.y*wa+b.y*wb, a.z*wa+b.z*wb, a.w*wa+b.w*wb)
    n = math.sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w); return unreal.Quat(q.x/n, q.y/n, q.z/n, q.w/n)
def lerp(a, b, t): return unreal.Vector(a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t)
def xform(loc, quat, scale):
    x = unreal.Transform(); x.translation = loc; x.rotation = quat; x.scale3d = scale; return x

def pose_for_frame(bone, f):
    if f <= HOLD0: return ground[bone]
    if f < SPIN_AT:
        t = (f - HOLD0) / float(SPIN_AT - HOLD0); a, b = ground[bone], spin[0][bone]
        return xform(lerp(a.translation, b.translation, t), slerp(a.rotation, b.rotation, t), a.scale3d)
    if f <= SPIN_AT + (SPIN_LAST - SPIN_FIRST): return spin[f - SPIN_AT][bone]
    if f < IDLE_AT:
        t = (f - BLEND_OUT_START + 1) / float(IDLE_AT - BLEND_OUT_START + 1); a, b = spin[-1][bone], idle[bone]
        return xform(lerp(a.translation, b.translation, t), slerp(a.rotation, b.rotation, t), a.scale3d)
    return idle[bone]

tools = unreal.AssetToolsHelpers.get_asset_tools()
rough = unreal.load_asset(f"{DST}/AS_GetUpAttack_Rough") or tools.duplicate_asset("AS_GetUpAttack_Rough", DST, IDLE)
ctrl = rough.get_editor_property("controller"); model = rough.get_editor_property("data_model_interface")
ctrl.open_bracket("build rough", True)
ctrl.set_number_of_frames(unreal.FrameNumber(LAST), True)
for bone in names:
    pos, rot, scl = [], [], []
    for f in range(LAST + 1):
        p = idle["root"] if bone == "root" else pose_for_frame(bone, f)
        pos.append(p.translation); rot.append(p.rotation); scl.append(p.scale3d)
    ctrl.set_bone_track_keys(bone, pos, rot, scl, True)
ctrl.close_bracket(True)
print("ROUGH frames", model.get_number_of_frames(), "length %.4f" % AL.get_sequence_length(rough))

def check(label, f, ref):
    got = poses_at(rough, f / 30.0); mr = mt = 0.0; wb = ""
    for b in names:
        if b == "root": continue
        g, r = got[b], ref[b]
        dr = math.degrees(2*math.acos(min(1.0, abs(g.rotation.x*r.rotation.x+g.rotation.y*r.rotation.y+g.rotation.z*r.rotation.z+g.rotation.w*r.rotation.w))))
        dt = math.dist((g.translation.x, g.translation.y, g.translation.z), (r.translation.x, r.translation.y, r.translation.z))
        if dr > mr: mr, wb = dr, b
        mt = max(mt, dt)
    print(f"  CHECK {label} f{f}: max rot {mr:.3f} deg ({wb}), max trans {mt:.3f} cm")
check("ground", 0, ground); check("ground-hold", HOLD0, ground); check("spin first", SPIN_AT, spin[0]); check("spin peak", SPIN_AT + 19 - SPIN_FIRST, poses_at(SPIN, 19/30.0)); check("spin last", SPIN_AT + SPIN_LAST - SPIN_FIRST, spin[-1]); check("idle", IDLE_AT, idle); check("idle end", LAST, idle)
r = poses_at(rough, 0.0)["root"]; print("  root f0 = (%.2f, %.2f, %.2f)" % (r.translation.x, r.translation.y, r.translation.z))
unreal.EditorLoadingAndSavingUtils.save_packages([rough.get_outermost()], False)
task = unreal.AssetExportTask(); task.object = rough; task.filename = os.path.join(OUT, "GetUpAttack_Rough.fbx")
task.automated = True; task.replace_identical = True; task.prompt = False
opts = unreal.FbxExportOption(); opts.ascii = False; opts.export_preview_mesh = False; opts.map_skeletal_motion_to_root = False; opts.export_local_time = True
task.options = opts
print("EXPORT ok=%s bytes=%d" % (unreal.Exporter.run_asset_export_task(task), os.path.getsize(task.filename) if os.path.exists(task.filename) else -1))
print("DONE")
