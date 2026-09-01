"""Can one heavy serve all three lights, judged against each light's OWN motion?

Third budget in this search, and the reason is that the first two were pose-gap samples adopted
without checking what they represented. This one is per-light and physical: a blend may move a
joint no further than that light's own animation moves it over the same window. The shipping
handovers are kept as a cross-check, not as the budget.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
JOINTS = ["hand_r", "foot_l", "foot_r"]
WIN = 0.250
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
LIGHTS = [("L1", B + "Attack4_Stage1_Complete_IP", 0.225, 1.500),
          ("L2", B + "Attack8_Stage2_Complete_IP", 0.502, 3.344),
          ("L3", B + "Attack2_Stage2_Complete_IP", 0.360, 2.403)]
_c = {}


def pose(seq, secs):
    n, L = AL.get_num_frames(seq), AL.get_sequence_length(seq)
    fr = max(0, min(n, int(round(secs * n / L))))
    k = (seq.get_name(), fr)
    if k not in _c:
        o = {}
        for b in JOINTS:
            path = [str(x) for x in AL.find_bone_path_to_root(seq, b)]
            xf = unreal.Transform()
            for p in AL.get_bone_poses_for_frame(seq, path, fr, False):
                xf = xf * p
            t = xf.translation
            o[b] = (t.x, t.y, t.z)
        _c[k] = o
    return _c[k]


def own_travel(seq, at, rate):
    """What this clip moves each joint over the window, at its own rate."""
    steps, out = 8, {}
    for j in JOINTS:
        pts = [pose(seq, at + WIN * rate * i / steps)[j] for i in range(steps + 1)]
        out[j] = sum(math.dist(pts[i], pts[i - 1]) for i in range(1, len(pts)))
    return out


budget, lp = {}, []
print("PER-LIGHT BUDGETS -- what each light's own motion covers in 250ms")
for tag, p, at, r in LIGHTS:
    s = unreal.load_asset(p)
    t = own_travel(s, at, r)
    fb = max(t["foot_l"], t["foot_r"])
    budget[tag] = (t["hand_r"], fb)
    lp.append((tag, pose(s, at)))
    print("  %s   hand %5.0f cm   foot %5.0f cm" % (tag, t["hand_r"], fb))
print("  (the shipping L1->L2 handover, for cross-check, is 65 hand / 27 foot)")

rows = []
for line in list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]:
    f = line.rstrip("\n").split("\t")
    if f[5] != "Y":
        continue
    name, strike, pkg = f[0], float(f[2]), f[7]
    if any(k in name for k in ("Attack4_Stage1", "Attack8_Stage2", "Attack2_Stage2",
                               "Attack6", "Attack3_")):
        continue
    entry = strike - 0.250
    hp = pose(unreal.load_asset(pkg), entry)
    worst, cells, ok = 0.0, [], True
    for tag, sp in lp:
        h = math.dist(sp["hand_r"], hp["hand_r"])
        ft = max(math.dist(sp["foot_l"], hp["foot_l"]), math.dist(sp["foot_r"], hp["foot_r"]))
        bh, bf = budget[tag]
        r = max(h / bh, ft / bf)
        worst = max(worst, r)
        ok = ok and r <= 1.0
        cells.append("%3d/%3d %.2f" % (h, ft, r))
    rows.append((worst, ok, name, entry, cells))

rows.sort()
inside = [r for r in rows if r[1]]
print("\n%d candidates, %d inside every light's own budget.\n" % (len(rows), len(inside)))
print("  %-6s %-40s %-14s %-14s %-14s" % ("worst", "candidate", "vs L1", "vs L2", "vs L3"))
for worst, ok, name, entry, cells in rows[:10]:
    print("  %5.2f  %-40s %-14s %-14s %-14s @%.3f%s"
          % (worst, name[3:43], cells[0], cells[1], cells[2], entry, "  INSIDE ALL" if ok else ""))
print("\n  (hand/foot, then the worst ratio against that light's own travel)")
print("DONE")
