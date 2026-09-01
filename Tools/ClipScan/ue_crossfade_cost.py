"""Crossfade cost: velocity of the *rendered* pose while two moving clips blend.

Both clips advance during the blend, so comparing a source pose against the target's entry pose
samples the target where it carries almost no weight. What a viewer sees is
    blended(t) = lerp(source(pos_s(t)), target(pos_t(t)), w(t))
and a pop is that mix moving faster than either clip ever does on its own.

w(t) approximates UE's HermiteCubic as smoothstep -- good enough to rank transitions against
each other, not a claim about the exact curve.
"""
import math, unreal

AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r"]
STEPS = 30
_pc, _sc = {}, {}


def pose(seq, secs):
    n, L = AL.get_num_frames(seq), AL.get_sequence_length(seq)
    fr = max(0, min(n, int(round(secs * n / L))))
    key = (seq.get_name(), fr)
    if key not in _pc:
        out = {}
        for b in BONES:
            path = [str(x) for x in AL.find_bone_path_to_root(seq, b)]
            xf = unreal.Transform()
            for p in AL.get_bone_poses_for_frame(seq, path, fr, False):
                xf = xf * p
            t = xf.translation
            out[b] = (t.x, t.y, t.z)
        _pc[key] = out
    return _pc[key]


def peak(seq):
    k = seq.get_name()
    if k not in _sc:
        n = AL.get_num_frames(seq)
        fps = n / AL.get_sequence_length(seq)
        pts = [pose(seq, f / fps)["hand_r"] for f in range(n + 1)]
        _sc[k] = max(math.dist(pts[f], pts[f - 1]) * fps for f in range(1, n + 1))
    return _sc[k]


def xfade(label, sp, s_at, s_rate, dp, d_at, d_rate, blend):
    s, d = unreal.load_asset(sp), unreal.load_asset(dp)
    fr = []
    for i in range(STEPS + 1):
        u = i / STEPS
        w = 3 * u * u - 2 * u * u * u
        a, b = pose(s, s_at + s_rate * blend * u), pose(d, d_at + d_rate * blend * u)
        fr.append({k: tuple((1 - w) * a[k][j] + w * b[k][j] for j in range(3)) for k in BONES})
    dt = blend / STEPS
    v = {k: max(math.dist(fr[i][k], fr[i - 1][k]) / dt for i in range(1, len(fr))) for k in BONES}
    ref = max(peak(s), peak(d))
    print(f"  {label:26s} hand {v['hand_r']:6.0f} cm/s   foot {max(v['foot_l'], v['foot_r']):6.0f}"
          f"   clip peak {ref:6.0f}   ratio {v['hand_r']/ref:5.2f}")


B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
L1, L2, L3 = B + "Attack4_Stage1_Complete_IP", B + "Attack8_Stage2_Complete_IP", B + "Attack2_Stage2_Complete_IP"
A8S3, A7S1 = B + "Attack8_Stage3_Complete_IP", B + "Attack7_Stage1_Complete_IP"

print("REFERENCE -- shipping string blends, judged good in play (blend 0.25)")
xfade("L1 -> L2", L1, 0.5410, 0.601, L2, 0.0, 3.344, 0.25)
xfade("L2 -> L3", L2, 0.9770, 1.000, L3, 0.0, 2.403, 0.25)

print("\nTIER -- into A8S3, entry 0.617 (strike 0.867 - 0.250), hold rate 1.0, blend 0.25")
for tag, src, at, r in (("L1", L1, 0.225, 1.500), ("L2", L2, 0.502, 3.344), ("L3", L3, 0.360, 2.403)):
    xfade(f"{tag} -> heavy A8S3", src, at, r, A8S3, 0.617, 1.0, 0.25)

print("\nTIER -- into A7S1, entry 0.950 (strike 1.200 - 0.250), hold rate 1.0, blend 0.25")
for tag, src, at, r in (("L1", L1, 0.225, 1.500), ("L2", L2, 0.502, 3.344), ("L3", L3, 0.360, 2.403)):
    xfade(f"{tag} -> heavy A7S1", src, at, r, A7S1, 0.950, 1.0, 0.25)
print("DONE")
