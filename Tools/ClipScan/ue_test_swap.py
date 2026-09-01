"""Test the designer's swap: current L2 becomes H1, the H1 candidate becomes the new L2.

Three things have to hold. H1 must read off L1; the new L2 must still work as a light on both
string handovers; and H2 must still have a partner, since changing L2 moves what H2 blends from.

The string figures for the new L2 are ESTIMATES: the outgoing clip's position at chain-out is
taken at the same proportion of clip length the current L2 reaches, not measured in play.
"""
import math, unreal

AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r", "pelvis"]
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
V2 = "/Game/GDHBundle/SwordShield/SwordShieldAnimV2/Animation/IP/AS_SwordShieldAnimV2_"
L1, L1_AT, L1_STRIKE, L1_END = B + "Attack4_Stage1_Complete_IP", 0.225, 0.367, 0.5411
L3, L3_STRIKE = B + "Attack2_Stage2_Complete_IP", 0.633
OLD_L2, OLD_L2_STRIKE = B + "Attack8_Stage2_Complete_IP", 0.833
NEW_L2, NEW_L2_STRIKE = V2 + "Attack5_Stage2_Complete_IP", 0.367
_c = {}


def pose(seq, secs):
    n, L = AL.get_num_frames(seq), AL.get_sequence_length(seq)
    fr = max(0, min(n, int(round(secs * n / L))))
    k = (seq.get_name(), fr)
    if k not in _c:
        o = {}
        for b in BONES:
            path = [str(x) for x in AL.find_bone_path_to_root(seq, b)]
            xf = unreal.Transform()
            for p in AL.get_bone_poses_for_frame(seq, path, fr, False):
                xf = xf * p
            t = xf.translation
            o[b] = (t.x, t.y, t.z)
        _c[k] = o
    return _c[k]


def plane(seq, strike):
    a = pose(seq, max(0.0, strike - 0.10))["hand_r"]
    b = pose(seq, strike + 0.10)["hand_r"]
    d = tuple(b[i] - a[i] for i in range(3))
    m = math.sqrt(sum(x * x for x in d))
    return tuple(x / m for x in d) if m > 1e-6 else (0.0, 0.0, 0.0)


def ang(u, v):
    return math.degrees(math.acos(max(-1.0, min(1.0, sum(u[i] * v[i] for i in range(3))))))


def gap(a, b):
    return (math.dist(a["hand_r"], b["hand_r"]),
            max(math.dist(a["foot_l"], b["foot_l"]), math.dist(a["foot_r"], b["foot_r"])))


s = {p: unreal.load_asset(p) for p in (L1, L3, OLD_L2, NEW_L2)}
pl = {p: plane(s[p], k) for p, k in ((L1, L1_STRIKE), (L3, L3_STRIKE),
                                     (OLD_L2, OLD_L2_STRIKE), (NEW_L2, NEW_L2_STRIKE))}

print("PROPOSAL   H1 = Attack8_Stage2 (the current L2)   new L2 = Attack5_Stage2\n")
h1_entry = OLD_L2_STRIKE - 0.250
h, f = gap(pose(s[L1], L1_AT), pose(s[OLD_L2], h1_entry))
print("  L1 -> H1   hand %3d  foot %3d  plane %3.0f deg   entry %.3f   (budget 65/27)"
      % (h, f, ang(pl[L1], pl[OLD_L2]), h1_entry))
print("             for comparison, L1 -> the rejected Attack10 was 45/4, and every")
print("             pooled candidate sat at 95-136 deg of plane from L1")

print("\nNEW STRING, both handovers (estimates -- chain-out position taken proportionally)")
newL2_len = AL.get_sequence_length(s[NEW_L2])
frac = 0.9770 / AL.get_sequence_length(s[OLD_L2])
h, f = gap(pose(s[L1], L1_END), pose(s[NEW_L2], 0.0))
print("  L1 -> newL2   hand %3d  foot %3d      (current L1 -> L2 ships at 65 / 27)" % (h, f))
h, f = gap(pose(s[NEW_L2], newL2_len * frac), pose(s[L3], 0.0))
print("  newL2 -> L3   hand %3d  foot %3d      (current L2 -> L3 ships at 30 / 13)" % (h, f))

print("\nWHAT THE NEW L2 WOULD COST AS A LIGHT")
print("  strike %.3f -> windupRate %.3f  (current L2 runs 3.344; L1 1.500, L3 2.403)"
      % (NEW_L2_STRIKE, NEW_L2_STRIKE / 0.200))
print("  length %.3f vs current L2 1.500; tail after strike %.3f" % (newL2_len, newL2_len - NEW_L2_STRIKE))
print("\nDOES H2 STILL HAVE A PARTNER? new L2 at escalation 0.150 x rate")
new_at = min(NEW_L2_STRIKE, 0.150 * (NEW_L2_STRIKE / 0.200))
h2 = unreal.load_asset(V2 + "Attack1_Stage5_Complete_IP")
h, f = gap(pose(s[NEW_L2], new_at), pose(h2, 0.750))
print("  newL2 @%.3f -> H2 Attack1_Stage5  hand %3d  foot %3d   (was 24 / 10 from old L2)"
      % (new_at, h, f))
print("\nDONE")
