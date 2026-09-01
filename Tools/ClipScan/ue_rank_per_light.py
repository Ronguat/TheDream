"""Best heavy per individual light -- one clip serving all three is a stricter ask than three
serving one each, and the pool failed the strict version. Flags family siblings of the lights,
since a vendor combo authors consecutive stages to flow and that may be why the shipping
handovers sit so far below everything else on feet.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r"]
T_HEAVY = 0.250
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
SOURCES = [("L1", B + "Attack4_Stage1_Complete_IP", 0.225),
           ("L2", B + "Attack8_Stage2_Complete_IP", 0.502),
           ("L3", B + "Attack2_Stage2_Complete_IP", 0.360)]
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


def gap(a, b):
    return (math.dist(a["hand_r"], b["hand_r"]),
            max(math.dist(a["foot_l"], b["foot_l"]), math.dist(a["foot_r"], b["foot_r"])))


src = [(t, pose(unreal.load_asset(p), a)) for t, p, a in SOURCES]
rows = []
for line in list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]:
    f = line.rstrip("\n").split("\t")
    if f[5] != "Y":
        continue
    name, strike, pkg = f[0], float(f[2]), f[7]
    if any(x in name for x in ("AnimV3_Attack4_Stage1", "AnimV3_Attack8_Stage2", "AnimV3_Attack2_Stage2")):
        continue
    tp = pose(unreal.load_asset(pkg), strike - T_HEAVY)
    rows.append((name, strike - T_HEAVY, [gap(s, tp) for _, s in src]))

print("REFERENCE  L1->L2 hand 65.2 foot 27.3   |   L2->L3 hand 29.7 foot 12.9")
print("%d heavy-eligible, lights excluded" % len(rows))
FAM = ("AnimV3_Attack4_", "AnimV3_Attack8_", "AnimV3_Attack2_")
for i, (tag, _, _) in enumerate(SOURCES):
    best = sorted(rows, key=lambda r: math.hypot(r[2][i][0], r[2][i][1]))[:6]
    print("\n  best heavy for %s:" % tag)
    for name, entry, g in best:
        mark = "   <- family sibling of a light" if any(k in name for k in FAM) else ""
        flag = "  BOTH UNDER REF" if (g[i][0] <= 65.2 and g[i][1] <= 27.3) else ""
        print("    hand %3d  foot %3d   %-50s entry %.3f%s%s"
              % (g[i][0], g[i][1], name[3:53], entry, flag, mark))
print("\nDONE")
