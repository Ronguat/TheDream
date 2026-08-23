"""A2' (UE half): import the round-tripped FBX as animations onto our skeleton and diff each
endpoint pose against the reference recorded by ue_export_endpoints.py.

Run through run-in-editor.py. Prints per-clip length, missing bones, and the worst deviations.
"""
import json, math, os, unreal

RT = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/roundtrip"
EXP = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/export"
SK = "/Game/GDHBundle/SwordShield/DEMO/Characters_SwordShield/Mannequins/Meshes/SK_Mannequin"
DST = "/Game/TheDream/Combat/Animations/Scratch"
CLIPS = {"Death_Bw_RM": 0.9, "Idle1_IP": 0.0}
AL = unreal.AnimationLibrary


def import_anim(tag):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(RT, tag + "_rt.fbx")
    task.destination_path = DST
    task.destination_name = "AS_RT_" + tag
    task.automated = True
    task.replace_existing = True
    task.save = False
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_animations = True
    ui.import_as_skeletal = False
    ui.automated_import_should_detect_type = False
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ui.skeleton = unreal.load_asset(SK)
    # Cascadeur exports its UE5 Manny root with a -90 deg roll baked in; +90 on import cancels it
    # (measured 2026-08-22: root exact, every bone within 0.13 deg / 0.04 cm).
    ui.anim_sequence_import_data.set_editor_property("import_rotation", unreal.Rotator(roll=90.0, pitch=0.0, yaw=0.0))
    task.options = ui
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    paths = list(task.imported_object_paths)
    print("IMPORT", tag, "->", paths)
    return unreal.load_asset(paths[0]) if paths else None


def quat_deg(a, b):
    d = abs(a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3])
    return math.degrees(2 * math.acos(min(1.0, d)))


for tag, t_ref in CLIPS.items():
    seq = import_anim(tag)
    if not seq:
        continue
    ref = json.load(open(os.path.join(EXP, f"ref_{tag}.json")))["bones"]
    length = AL.get_sequence_length(seq)
    frames = AL.get_num_frames(seq)
    t = min(t_ref, length)
    names = [str(n) for n in AL.get_animation_track_names(seq)]
    missing = [b for b in ref if b not in names]
    extra = [n for n in names if n not in ref]
    poses = AL.get_bone_poses_for_time(seq, names, t, False)
    rows = []
    for n, p in zip(names, poses):
        if n not in ref:
            continue
        r = ref[n]
        dt = math.dist((p.translation.x, p.translation.y, p.translation.z), r[0:3])
        dr = quat_deg((p.rotation.x, p.rotation.y, p.rotation.z, p.rotation.w), r[3:7])
        rows.append((n, dt, dr))
    rows.sort(key=lambda x: -x[2])
    print(f"CLIP {tag}: rt length={length:.4f} frames={frames} ref_t={t_ref} compared_at={t:.4f} "
          f"tracks={len(names)} compared={len(rows)} missing={len(missing)} extra={len(extra)}")
    if missing: print("  missing:", missing[:20])
    if extra: print("  extra:", extra[:20])
    if rows:
        print("  max rot deg=%.3f  max trans cm=%.3f  mean rot=%.3f  mean trans=%.3f" % (
            max(r[2] for r in rows), max(r[1] for r in rows),
            sum(r[2] for r in rows)/len(rows), sum(r[1] for r in rows)/len(rows)))
        for n, dt, dr in rows[:8]:
            print("  %-22s rot %7.3f deg  trans %7.3f cm" % (n, dr, dt))
print("DONE")
