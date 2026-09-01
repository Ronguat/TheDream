"""Stage 2: blend cost for every clip that survived the geometry screen.

Each candidate is posed at the entry its role implies (strike - T), and compared against the
three lights at their escalation positions. Cost is reported against each light's OWN natural
travel over the same 150 ms, so 1.00 means the blend asks exactly what the clip already does.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
BONES = ["hand_r", "lowerarm_r", "foot_l", "foot_r"]
T = {"Heavy": 0.250, "Charged": 0.450}

# Source, montage position at escalation, and its own hand travel over the next 150 ms.
SOURCES = [
    ("L1", "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_Attack4_Stage1_Complete_IP", 0.225, 109.6),
    ("L2", "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_Attack8_Stage2_Complete_IP", 0.502, 89.1),
    ("L3", "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_Attack2_Stage2_Complete_IP", 0.360, 62.7),
]

cache = {}


def pose(seq, secs):
    n = AL.get_num_frames(seq)
    frame = max(0, min(n, int(round(secs * n / AL.get_sequence_length(seq)))))
    key = (seq.get_name(), frame)
    if key in cache:
        return cache[key]
    out = {}
    for b in BONES:
        path = [str(x) for x in AL.find_bone_path_to_root(seq, b)]
        xf = unreal.Transform()
        for p in AL.get_bone_poses_for_frame(seq, path, frame, False):
            xf = xf * p
        t = xf.translation
        out[b] = (t.x, t.y, t.z)
    cache[key] = out
    return out


src = [(tag, pose(unreal.load_asset(p), at), nat) for tag, p, at, nat in SOURCES]

rows = list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]
out = []
for line in rows:
    f = line.rstrip("\n").split("\t")
    name, strike, ok_h, ok_c, pkg = f[0], float(f[2]), f[5], f[6], f[7]
    seq = unreal.load_asset(pkg)
    for role, flag in (("Heavy", ok_h), ("Charged", ok_c)):
        if flag != "Y":
            continue
        entry = strike - T[role]
        tp = pose(seq, entry)
        costs, feet = [], []
        for tag, sp, nat in src:
            costs.append(math.dist(sp["hand_r"], tp["hand_r"]) / nat)
            feet.append(max(math.dist(sp["foot_l"], tp["foot_l"]),
                            math.dist(sp["foot_r"], tp["foot_r"])))
        out.append((max(costs), name, role, f"{entry:.3f}",
                    " ".join(f"{c:.2f}" for c in costs), f"{max(feet):.0f}"))

out.sort()
with open(os.path.join(HERE, "p2_blend.tsv"), "w") as fh:
    fh.write("WorstCost\tClip\tRole\tEntry\tL1_L2_L3_cost\tWorstFootCm\n")
    for r in out:
        fh.write("%.3f\t%s\t%s\t%s\t%s\t%s\n" % r)
print("scored", len(out))
print("DONE")
