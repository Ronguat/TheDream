"""Is Attack2_IP at 1.250 the same motion as light 3, under another name?

A near-zero direction angle between a clip and a light is what a shared motion looks like, and a
clip serving two attack types is a coil by definition. This slides one clip against the other and
reports the best per-frame pose agreement; a deep minimum means shared content.
"""
import math, unreal

AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r", "pelvis"]
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"


def pose(seq, secs):
    n, L = AL.get_num_frames(seq), AL.get_sequence_length(seq)
    fr = max(0, min(n, int(round(secs * n / L))))
    o = {}
    for b in BONES:
        path = [str(x) for x in AL.find_bone_path_to_root(seq, b)]
        xf = unreal.Transform()
        for p in AL.get_bone_poses_for_frame(seq, path, fr, False):
            xf = xf * p
        t = xf.translation
        o[b] = (t.x, t.y, t.z)
    return o


def agree(a, b):
    return sum(math.dist(a[k], b[k]) for k in BONES) / len(BONES)


def slide(child_path, parent_path, label):
    c, p = unreal.load_asset(child_path), unreal.load_asset(parent_path)
    lc, lp = AL.get_sequence_length(c), AL.get_sequence_length(p)
    print("\n%s   child %.3fs   parent %.3fs" % (label, lc, lp))
    best = []
    off = 0.0
    while off <= lp - lc * 0.5:
        errs = [agree(pose(c, t), pose(p, off + t)) for t in (0.0, lc * 0.25, lc * 0.5, lc * 0.75)]
        best.append((sum(errs) / len(errs), off))
        off += lp / 40.0
    best.sort()
    for e, o in best[:4]:
        print("    offset %6.3f s   mean bone disagreement %6.2f cm" % (o, e))
    print("    verdict: %s" % ("SHARED CONTENT -- same motion, would be a coil"
                               if best[0][0] < 5.0 else "distinct motion"))


print("Does Attack2_IP contain light 3's motion?")
slide(B + "Attack2_Stage2_Complete_IP", B + "Attack2_IP", "Attack2_Stage2 (light 3) vs Attack2_IP")
slide(B + "Attack4_Stage1_Complete_IP", B + "Attack4_IP", "Attack4_Stage1 (light 1) vs Attack4_IP")
print("\nDONE")
