"""Charged candidates for the accepted L2 heavy.

The charged blends from the heavy at elapsed 0.350, which is a known position: the heavy swaps
in at 0.150 at its entry and runs at its hold rate, so a well-fitted heavy sits at entry+0.200.
Charged window is 0.450s (swap 0.350 -> release 0.800), so its entry is strike - 0.450.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r", "pelvis"]
DT, T_CHARGED = 1.0 / 30.0, 0.450
V2 = "/Game/GDHBundle/SwordShield/SwordShieldAnimV2/Animation/IP/AS_SwordShieldAnimV2_"
HEAVY_L2, HEAVY_AT = V2 + "Attack1_Stage5_Complete_IP", 0.950
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
LIGHTS = [B + "Attack4_Stage1_Complete_IP", B + "Attack8_Stage2_Complete_IP", B + "Attack2_Stage2_Complete_IP"]
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


def agree(a, b):
    return sum(math.dist(a[k], b[k]) for k in BONES) / len(BONES)


def overlap(cseq, entry, other):
    L = AL.get_sequence_length(other)
    probes = [pose(cseq, entry + d) for d in (0.0, 0.10, 0.20)]
    best, off = 1e9, 0.0
    while off <= max(0.0, L - 0.20):
        best = min(best, sum(agree(probes[i], pose(other, off + d))
                             for i, d in enumerate((0.0, 0.10, 0.20))) / 3.0)
        off += L / 30.0
    return best


def vel(s, at, rate=1.0):
    a, b = pose(s, at)["hand_r"], pose(s, at + DT * rate)["hand_r"]
    return tuple((b[i] - a[i]) / DT for i in range(3))


hseq = unreal.load_asset(HEAVY_L2)
hp, hv = pose(hseq, HEAVY_AT), vel(hseq, HEAVY_AT)
mh = math.sqrt(sum(x * x for x in hv))
lights = [unreal.load_asset(p) for p in LIGHTS]

rows = []
for line in list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]:
    f = line.rstrip("\n").split("\t")
    if f[6] != "Y":
        continue
    name, strike, pkg = f[0], float(f[2]), f[7]
    if "Attack1_Stage5" in name:
        continue
    entry = strike - T_CHARGED
    seq = unreal.load_asset(pkg)
    tp = pose(seq, entry)
    hand = math.dist(hp["hand_r"], tp["hand_r"])
    foot = max(math.dist(hp["foot_l"], tp["foot_l"]), math.dist(hp["foot_r"], tp["foot_r"]))
    tv = vel(seq, entry)
    mt = math.sqrt(sum(x * x for x in tv))
    ang = -1.0
    if mh > 1e-6 and mt > 1e-6:
        d = sum(hv[i] * tv[i] for i in range(3)) / (mh * mt)
        ang = math.degrees(math.acos(max(-1.0, min(1.0, d))))
    rows.append((math.hypot(hand, foot), hand, foot, ang, name, entry, seq))

rows.sort()
print("REFERENCE  shipping handovers: 65.2 hand / 27.3 foot, direction up to 103.6 deg")
print("SOURCE     %s @ %.3f\n" % (HEAVY_L2.split("_IP")[0][-24:], HEAVY_AT))
print("%d charged-eligible.  Best 8, overlap-screened against all three lights and the heavy:"
      % len(rows))
for _, hand, foot, ang, name, entry, seq in rows[:8]:
    ov = min([overlap(seq, entry, l) for l in lights] + [overlap(seq, entry, hseq)])
    flag = "  DISQUALIFIED shares a clip's motion" if ov < 12.0 else ""
    print("  hand %3d  foot %3d  dir %5.1f  overlap %5.1f   %-44s @%.3f%s"
          % (hand, foot, ang, ov, name[3:47], entry, flag))
print("\nDONE")
