"""The missing control: how far do feet naturally travel in 250ms of these clips' own motion?

The 27cm foot reference is taken at the string handover, where both clips have settled feet, so
it may measure a stride phase rather than a tolerance. If clips routinely move a foot 100cm in
a step, a 117cm gap is a stride and not a defect.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
FEET = ["foot_l", "foot_r"]
WIN = 0.250
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
V2 = "/Game/GDHBundle/SwordShield/SwordShieldAnimV2/Animation/IP/AS_SwordShieldAnimV2_"
_c = {}


def feet(seq, secs):
    n, L = AL.get_num_frames(seq), AL.get_sequence_length(seq)
    fr = max(0, min(n, int(round(secs * n / L))))
    k = (seq.get_name(), fr)
    if k not in _c:
        o = {}
        for b in FEET:
            path = [str(x) for x in AL.find_bone_path_to_root(seq, b)]
            xf = unreal.Transform()
            for p in AL.get_bone_poses_for_frame(seq, path, fr, False):
                xf = xf * p
            t = xf.translation
            o[b] = (t.x, t.y, t.z)
        _c[k] = o
    return _c[k]


def travel(seq, at, rate):
    """Worst foot's own path over 250ms of game time at the given play rate."""
    steps = 8
    best = 0.0
    for b in FEET:
        pts = [feet(seq, at + WIN * rate * i / steps)[b] for i in range(steps + 1)]
        best = max(best, sum(math.dist(pts[i], pts[i - 1]) for i in range(1, len(pts))))
    return best


def speed(seq, at, rate):
    a, b = feet(seq, at), feet(seq, at + 0.033 * rate)
    return max(math.dist(a[k], b[k]) / 0.033 for k in FEET)


print("AT THE SHIPPING HANDOVERS -- are the feet actually planted?")
for lbl, p, at, r in (("L1 in recovery @0.541", B + "Attack4_Stage1_Complete_IP", 0.541, 0.601),
                      ("L2 at frame 0", B + "Attack8_Stage2_Complete_IP", 0.0, 3.344),
                      ("L2 in recovery @0.977", B + "Attack8_Stage2_Complete_IP", 0.977, 1.000),
                      ("L3 at frame 0", B + "Attack2_Stage2_Complete_IP", 0.0, 2.403)):
    s = unreal.load_asset(p)
    print("  %-26s foot speed %5.0f cm/s" % (lbl, speed(s, at, r)))

print("\nNATURAL FOOT TRAVEL over 250ms, each clip at its own escalation point and rate")
for lbl, p, at, r in (("L1 @0.225 rate 1.50", B + "Attack4_Stage1_Complete_IP", 0.225, 1.500),
                      ("L2 @0.502 rate 3.34", B + "Attack8_Stage2_Complete_IP", 0.502, 3.344),
                      ("L3 @0.360 rate 2.40", B + "Attack2_Stage2_Complete_IP", 0.360, 2.403)):
    print("  %-24s worst foot travels %5.0f cm" % (lbl, travel(unreal.load_asset(p), at, r)))

print("\nSAME, across the heavy candidates at their fitted entries (rate 1.0)")
rows = []
for line in list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]:
    f = line.rstrip("\n").split("\t")
    if f[5] != "Y":
        continue
    rows.append((f[0], float(f[2]) - 0.250, f[7]))
vals = []
for name, entry, pkg in rows:
    try:
        vals.append(travel(unreal.load_asset(pkg), entry, 1.0))
    except Exception:
        pass
vals.sort()
n = len(vals)
print("  %d candidates:  min %.0f   25%% %.0f   median %.0f   75%% %.0f   max %.0f cm"
      % (n, vals[0], vals[n // 4], vals[n // 2], vals[3 * n // 4], vals[-1]))
print("\n  the L1 -> H1 foot GAP under discussion is 117 cm")
print("DONE")
