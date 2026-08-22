"""Export one or more AnimSequences to FBX for Cascadeur, plus a reference pose per clip.

    run-in-editor.py ue_export_clip.py        # edits CLIPS below; keeps the pipeline's conventions
"""
import json, os, unreal

OUT = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/export"
CLIPS = {
    "Attack2_Stage2_RM": "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/RM/AS_SwordSwordAnimV3_Attack2_Stage2_Complete_RM",
    "Attack2_Stage2_IP": "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_Attack2_Stage2_Complete_IP",
}
AL = unreal.AnimationLibrary
os.makedirs(OUT, exist_ok=True)
for tag, path in CLIPS.items():
    seq = unreal.load_asset(path)
    if not seq:
        print("MISSING", tag, path); continue
    task = unreal.AssetExportTask()
    task.object = seq; task.filename = os.path.join(OUT, tag + ".fbx")
    task.automated = True; task.replace_identical = True; task.prompt = False
    opts = unreal.FbxExportOption(); opts.ascii = False; opts.export_preview_mesh = False
    opts.map_skeletal_motion_to_root = False; opts.export_local_time = True
    task.options = opts
    ok = unreal.Exporter.run_asset_export_task(task)
    length = AL.get_sequence_length(seq); frames = AL.get_num_frames(seq)
    print(f"EXPORT {tag}: ok={ok} length={length:.4f} frames={frames} bytes={os.path.getsize(task.filename) if os.path.exists(task.filename) else -1}")
    names = [str(n) for n in AL.get_animation_track_names(seq)]
    root = {}
    for f in range(frames + 1):
        p = AL.get_bone_pose_for_time(seq, "root", f / 30.0, False)
        root[f] = [round(p.translation.x, 2), round(p.translation.y, 2), round(p.translation.z, 2)]
    print(f"  root travel: f0={root[0]} fmid={root[frames // 2]} fend={root[frames]}")
print("DONE")
