"""Light x heavy-candidate matrix, with swing plane -- so the assignment can be shuffled.

Per-light greedy picking can be worse than a considered matching: the best clip for one light
may be far better for another. And pose gap is measured at a single instant, so it is blind to
swing plane; a clip can sit near a light's hand while swinging the opposite diagonal.

Swing plane is the hand's displacement through the strike, normalised, in component space.
The angle between two planes says whether they cut the same way.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r", "pelvis"]
T_HEAVY = 0.250
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
LIGHTS = [("L1", B + "Attack4_Stage1_Complete_IP", 0.225, 0.367),
          ("L2", B + "Attack8_Stage2_Complete_IP", 0.502, 0.833),
          ("L3", B + "Attack2_Stage2_Complete_IP", 0.360, 0.633)]
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
    """Hand displacement through the strike, normalised -- which way the cut travels."""
    a = pose(seq, max(0.0, strike - 0.10))["hand_r"]
    b = pose(seq, strike + 0.10)["hand_r"]
    d = tuple(b[i] - a[i] for i in range(3))
    m = math.sqrt(sum(x * x for x in d))
    return tuple(x / m for x in d) if m > 1e-6 else (0.0, 0.0, 0.0)


def ang(u, v):
    d = sum(u[i] * v[i] for i in range(3))
    return math.degrees(math.acos(max(-1.0, min(1.0, d))))


lp = [(t, pose(unreal.load_asset(p), a), plane(unreal.load_asset(p), s)) for t, p, a, s in LIGHTS]

cands = []
for line in list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]:
    f = line.rstrip("\n").split("\t")
    if f[5] != "Y":
        continue
    name, strike, pkg = f[0], float(f[2]), f[7]
    if any(k in name for k in ("Attack4_Stage1", "Attack8_Stage2", "Attack2_Stage2",
                               "Attack6", "Attack3_", "Attack10_IP")):
        continue
    seq = unreal.load_asset(pkg)
    entry = strike - T_HEAVY
    hp, pl = pose(seq, entry), plane(seq, strike)
    cells = []
    for _, sp, spl in lp:
        cells.append((math.dist(sp["hand_r"], hp["hand_r"]),
                      max(math.dist(sp["foot_l"], hp["foot_l"]), math.dist(sp["foot_r"], hp["foot_r"])),
                      ang(spl, pl)))
    cands.append((min(math.hypot(c[0], c[1]) for c in cells), name, entry, cells))

cands.sort()
print("plane angle: 0 = same cut direction, 180 = opposite diagonal")
print("budget 65cm hand.  Best 12 candidates against every light:\n")
print("  %-40s %-20s %-20s %-20s" % ("candidate", "vs L1", "vs L2", "vs L3"))
for _, name, entry, cells in cands[:12]:
    cols = ["%3d/%3d p%3.0f" % (c[0], c[1], c[2]) for c in cells]
    print("  %-40s %-20s %-20s %-20s @%.3f" % (name[3:43], cols[0], cols[1], cols[2], entry))
print("\n  (hand/foot p=plane angle)")
print("\nDONE")
