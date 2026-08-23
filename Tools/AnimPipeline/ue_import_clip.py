"""Import a Cascadeur FBX as an AnimSequence onto our skeleton, with the root-roll fix, and
diff chosen frames against another sequence. Edit the constants; run through run-in-editor.py.
"""
import math, os, unreal

AL = unreal.AnimationLibrary
FBX = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/roundtrip/GetUpAttack_casc.fbx"
DST, NAME = "/Game/TheDream/Combat/Animations/Scratch", "AS_GetUpAttack_Casc"
SK = "/Game/GDHBundle/SwordShield/DEMO/Characters_SwordShield/Mannequins/Meshes/SK_Mannequin"
COMPARE_TO = "/Game/TheDream/Combat/Animations/Scratch/AS_GetUpAttack_Rough"
FRAMES = [0, 3, 6, 10, 21, 30, 38, 45, 46]

task = unreal.AssetImportTask()
task.filename = FBX; task.destination_path = DST; task.destination_name = NAME
task.automated = True; task.replace_existing = True; task.save = True
ui = unreal.FbxImportUI()
ui.import_mesh = False; ui.import_animations = True; ui.import_as_skeletal = False
ui.automated_import_should_detect_type = False; ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
ui.skeleton = unreal.load_asset(SK)
task.options = ui
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
paths = list(task.imported_object_paths); print("IMPORT ->", paths)
seq = unreal.load_asset(paths[0]); ref = unreal.load_asset(COMPARE_TO)
# Cascadeur bakes a -90 deg roll into root on some exports and not others (measured 2026-08-22:
# A2' files yes, the gap-edited scene no), so the cancelling +90 is applied only when root reads -90.
roll = AL.get_bone_pose_for_time(seq, "root", 0.0, False).rotation.rotator().roll
if abs(roll + 90.0) < 1.0:
    ui.anim_sequence_import_data.set_editor_property("import_rotation", unreal.Rotator(roll=90.0, pitch=0.0, yaw=0.0))
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    seq = unreal.load_asset(list(task.imported_object_paths)[0])
    print("ROOT ROLL was %.1f -> re-imported with +90; now %.1f" % (roll, AL.get_bone_pose_for_time(seq, "root", 0.0, False).rotation.rotator().roll))
else:
    print("ROOT ROLL %.1f -> no correction" % roll)
print("frames=%d length=%.4f (ref frames=%d)" % (AL.get_num_frames(seq), AL.get_sequence_length(seq), AL.get_num_frames(ref)))
names = [str(n) for n in AL.get_animation_track_names(ref)]
for f in FRAMES:
    a = AL.get_bone_poses_for_time(seq, names, f / 30.0, False); b = AL.get_bone_poses_for_time(ref, names, f / 30.0, False)
    mr = mt = 0.0; wb = ""
    for n, p, q in zip(names, a, b):
        dr = math.degrees(2 * math.acos(min(1.0, abs(p.rotation.x*q.rotation.x + p.rotation.y*q.rotation.y + p.rotation.z*q.rotation.z + p.rotation.w*q.rotation.w))))
        dt = math.dist((p.translation.x, p.translation.y, p.translation.z), (q.translation.x, q.translation.y, q.translation.z))
        if dr > mr: mr, wb = dr, n
        mt = max(mt, dt)
    print("  f%-2d vs rough: max rot %7.3f deg (%s)  max trans %6.3f cm" % (f, mr, wb, mt))
print("DONE")
