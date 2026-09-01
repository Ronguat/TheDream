"""Pose distance from each light's escalation pose to a heavy candidate's entry pose.

Component space, so actor movement is out. The implied speed is the distance the blend must
cover divided by its duration -- comparable against the clips' own hand speeds.
"""
import json, math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
J = json.load(open(os.path.join(HERE, "p0_posedist.json")))
AL = unreal.AnimationLibrary
BONES = ["hand_r", "hand_l", "lowerarm_r", "foot_l", "foot_r", "head", "pelvis"]


def pose(seq, frame):
    out = {}
    for b in BONES:
        path = [str(x) for x in AL.find_bone_path_to_root(seq, b)]
        xf = unreal.Transform()
        for p in AL.get_bone_poses_for_frame(seq, path, frame, False):
            xf = xf * p
        t = xf.translation
        out[b] = (t.x, t.y, t.z)
    return out


def frame_at(seq, secs):
    n = AL.get_num_frames(seq)
    fps = n / AL.get_sequence_length(seq)
    return max(0, min(n, int(round(secs * fps))))


heavy = unreal.load_asset(J["heavy"]["path"])
blend = J["blend"]
print(f"heavy = {J['heavy']['name']}   blend = {blend*1000:.0f} ms\n")

for entry in J["entries"]:
    hp = pose(heavy, frame_at(heavy, entry))
    print(f"--- heavy entry {entry:.3f}s ---")
    print(f"{'source':22s} {'hand_r':>8s} {'lowarm_r':>9s} {'foot_l':>8s} {'foot_r':>8s} "
          f"{'head':>7s} | {'hand_r cm/s':>11s}")
    for src in J["sources"]:
        seq = unreal.load_asset(src["path"])
        sp = pose(seq, frame_at(seq, src["at"]))
        d = {b: math.dist(sp[b], hp[b]) for b in BONES}
        print(f"{src['name']:22s} {d['hand_r']:8.1f} {d['lowerarm_r']:9.1f} {d['foot_l']:8.1f} "
              f"{d['foot_r']:8.1f} {d['head']:7.1f} | {d['hand_r']/blend:11.0f}")
    print()
print("DONE")
