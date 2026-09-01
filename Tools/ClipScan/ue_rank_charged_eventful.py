"""Charged candidates ranked on windup eventfulness, with pose gap priced as blend time.

The charged has 0.450s from swap to strike against the heavy's 0.250, and blend duration is a
per-montage authored value -- so a gap need not be closed in the 0.25s the vendor clips happen
to ship with. Priced against the shipping handover's closing RATE (65.2cm over 0.25s = 261 cm/s),
a candidate's required blend is gap/261, and anything under the 0.450 window is affordable.

Ranked by how much the hand travels through the windup, because "unorthodox" means eventful and
every previous ranking selected against exactly that.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r", "pelvis"]
T_CHARGED, WINDOW = 0.450, 0.450
CLOSE_RATE = 65.2 / 0.25
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
HEAVIES = [("from L2 heavy", "/Game/GDHBundle/SwordShield/SwordShieldAnimV2/Animation/IP/"
            "AS_SwordShieldAnimV2_Attack1_Stage5_Complete_IP", 0.950),
           ("from L3 heavy", B + "Attack4_Stage2_Complete_IP", 1.250)]
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


def eventfulness(seq, entry):
    """Hand path across the windup -- how much actually happens before the strike."""
    steps = 10
    pts = [pose(seq, entry + WINDOW * i / steps)["hand_r"] for i in range(steps + 1)]
    return sum(math.dist(pts[i], pts[i - 1]) for i in range(1, len(pts)))


rows = []
for line in list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]:
    f = line.rstrip("\n").split("\t")
    if f[6] != "Y":
        continue
    name, strike, pkg = f[0], float(f[2]), f[7]
    if any(k in name for k in ("Attack4_Stage2", "Attack1_Stage5", "Attack8_Stage2",
                               "Attack4_Stage1", "Attack2_Stage2", "Attack6", "Attack3_")):
        continue
    entry = strike - T_CHARGED
    seq = unreal.load_asset(pkg)
    ev = eventfulness(seq, entry)
    gaps = []
    for _, hp, ha in HEAVIES:
        s = pose(unreal.load_asset(hp), ha)
        t = pose(seq, entry)
        gaps.append(math.dist(s["hand_r"], t["hand_r"]))
    rows.append((-ev, ev, gaps, name, entry))

rows.sort()
print("Charged, ranked by windup eventfulness. Family 6 and 3 excluded, lights and chosen")
print("heavies excluded. Required blend = gap / %.0f cm/s; the window affords 0.450s.\n" % CLOSE_RATE)
print("  %-42s %6s  %-22s %-22s" % ("clip", "windup", "from L2 heavy", "from L3 heavy"))
for _, ev, gaps, name, entry in rows[:12]:
    cells = []
    for g in gaps:
        need = g / CLOSE_RATE
        cells.append("gap %3d need %.2fs%s" % (g, need, "" if need <= WINDOW else " OVER"))
    print("  %-42s %5.0fcm  %-22s %-22s  @%.3f" % (name[3:45], ev, cells[0], cells[1], entry))
print("\nDONE")
