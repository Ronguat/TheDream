"""A1': export the round-trip inputs and record the endpoint poses as the reference.

Run through run-in-editor.py. Writes FBX files and ref_*.json under Saved/AnimPipeline/export.
"""
import json, os, unreal

OUT = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/export"
MESH = "/Game/GDHBundle/SwordShield/DEMO/Characters_SwordShield/Mannequins/Meshes/SKM_Manny"
CLIPS = {
    "Death_Bw_RM": "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/RM/AS_SwordSwordAnimV3_Death_Bw_RM",
    "Idle1_IP": "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_Idle1_IP",
}
os.makedirs(OUT, exist_ok=True)


def export_fbx(asset, file_name, preview_mesh):
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = os.path.join(OUT, file_name)
    task.automated = True
    task.replace_identical = True
    task.prompt = False
    opts = unreal.FbxExportOption()
    opts.ascii = False
    opts.export_preview_mesh = preview_mesh
    opts.map_skeletal_motion_to_root = False
    opts.export_local_time = True
    task.options = opts
    ok = unreal.Exporter.run_asset_export_task(task)
    size = os.path.getsize(task.filename) if os.path.exists(task.filename) else -1
    print(f"EXPORT {file_name}: ok={ok} bytes={size} errors={list(task.errors)}")
    return ok


def reference(seq, time, tag):
    names = list(unreal.AnimationLibrary.get_animation_track_names(seq))
    poses = unreal.AnimationLibrary.get_bone_poses_for_time(seq, names, time, False)
    ref = {}
    for n, p in zip(names, poses):
        t, r, s = p.translation, p.rotation, p.scale3d
        ref[str(n)] = [round(t.x, 4), round(t.y, 4), round(t.z, 4),
                       round(r.x, 6), round(r.y, 6), round(r.z, 6), round(r.w, 6),
                       round(s.x, 4), round(s.y, 4), round(s.z, 4)]
    path = os.path.join(OUT, f"ref_{tag}.json")
    with open(path, "w") as f:
        json.dump({"time": time, "bones": ref}, f, indent=0)
    print(f"REF {tag}: {len(ref)} bones at t={time:.3f} -> {path}")
    return ref


mesh = unreal.load_asset(MESH)
export_fbx(mesh, "SKM_Manny.fbx", False)
for tag, path in CLIPS.items():
    seq = unreal.load_asset(path)
    length = unreal.AnimationLibrary.get_sequence_length(seq)
    frames = unreal.AnimationLibrary.get_num_frames(seq)
    print(f"CLIP {tag}: length={length:.4f} frames={frames} rootmotion={seq.get_editor_property('bEnableRootMotion')}")
    export_fbx(seq, f"{tag}.fbx", False)
    reference(seq, length if tag == "Death_Bw_RM" else 0.0, tag)
print("DONE")
