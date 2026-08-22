"""Component-space speed profile of a bone across a clip — locates a strike by the hand's peak.

Run through run-in-editor.py. Edit CLIP/BONE below.
"""
import math, unreal

AL = unreal.AnimationLibrary
CLIP = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_Attack2_Stage2_Complete_IP"
BONES = ["hand_r", "hand_l", "head"]
seq = unreal.load_asset(CLIP)
frames = AL.get_num_frames(seq)
tracks = [str(n) for n in AL.get_animation_track_names(seq)]
print("tracks with weapon/hand/ik:", [t for t in tracks if any(k in t for k in ("weapon", "hand", "ik_"))][:12])


def component_pos(bone, frame):
    path = [str(b) for b in AL.find_bone_path_to_root(seq, bone)]  # bone .. root
    poses = AL.get_bone_poses_for_frame(seq, path, frame, False)
    xf = unreal.Transform()
    for p in poses:  # bone-first: compose child * parent ... * root
        xf = xf * p
    return xf.translation


for bone in BONES:
    pts = [component_pos(bone, f) for f in range(frames + 1)]
    speeds = [math.dist((pts[f].x, pts[f].y, pts[f].z), (pts[f-1].x, pts[f-1].y, pts[f-1].z)) * 30.0 for f in range(1, frames + 1)]
    peak = max(range(len(speeds)), key=lambda i: speeds[i]) + 1
    prof = " ".join(f"{int(s):4d}" for s in speeds)
    print(f"{bone}: peak frame {peak} ({speeds[peak-1]:.0f} cm/s); z@peak={pts[peak].z:.1f}")
    print("  cm/s per frame:", prof)
pelvis_z = [component_pos("pelvis", f).z for f in range(frames + 1)]
print("pelvis z per frame:", " ".join(f"{z:.0f}" for z in pelvis_z))
print("DONE")
