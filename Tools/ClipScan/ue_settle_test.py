"""Charged follow-ups for the L3 heavy, plus the settle test on the Attack4 variants.

Settle: a clip meant to end a swing decays to near-stationary; a mid-combo fragment is cut off
still moving. Measured as mean hand speed over the last 0.15s, read against the three lights,
which are all _Complete and do settle.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r", "pelvis"]
DT, T_CHARGED = 1.0 / 30.0, 0.450
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
HEAVY_L3, HEAVY_AT = B + "Attack4_Stage2_Complete_IP", 1.250
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


def terminal(path):
    s = unreal.load_asset(path)
    L = AL.get_sequence_length(s)
    ds = [math.dist(pose(s, t)["hand_r"], pose(s, t - DT)["hand_r"]) / DT
          for t in [L - 0.12, L - 0.08, L - 0.04, L - 0.005]]
    return sum(ds) / len(ds), L


print("SETTLE TEST -- mean hand speed over the last 0.15s")
for lbl, p in (("light 1 (settles)", LIGHTS[0]), ("light 2 (settles)", LIGHTS[1]),
               ("light 3 (settles)", LIGHTS[2]),
               ("Attack4_Stage2_IP", B + "Attack4_Stage2_IP"),
               ("Attack4_Stage2_Complete_IP", B + "Attack4_Stage2_Complete_IP")):
    v, L = terminal(p)
    print("  %-28s len %.3f   terminal %6.0f cm/s" % (lbl, L, v))


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


def vel(s, at):
    a, b = pose(s, at)["hand_r"], pose(s, at + DT)["hand_r"]
    return tuple((b[i] - a[i]) / DT for i in range(3))


hseq = unreal.load_asset(HEAVY_L3)
hp, hv = pose(hseq, HEAVY_AT), vel(hseq, HEAVY_AT)
mh = math.sqrt(sum(x * x for x in hv))
lights = [unreal.load_asset(p) for p in LIGHTS]

rows = []
for line in list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]:
    f = line.rstrip("\n").split("\t")
    if f[6] != "Y":
        continue
    name, strike, pkg = f[0], float(f[2]), f[7]
    if "Attack4_Stage2" in name:
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
print("\nCHARGED for the L3 heavy (%s @ %.3f)" % ("Attack4_Stage2_Complete", HEAVY_AT))
print("reference 65 hand / 27 foot / direction seen up to 103.6 deg")
shown = 0
for _, hand, foot, ang, name, entry, seq in rows:
    if shown >= 5:
        break
    ov = min([overlap(seq, entry, l) for l in lights] + [overlap(seq, entry, hseq)])
    marks = []
    if ov < 12.0:
        marks.append("DISQUALIFIED shares a clip")
    if "Attack6" in name or "Attack3" in name:
        marks.append("family the V3 review called unusable")
    print("  hand %3d  foot %3d  dir %5.1f  overlap %5.1f  %-42s @%.3f  %s"
          % (hand, foot, ang, ov, name[3:45], entry, "; ".join(marks)))
    shown += 1
print("\nDONE")
