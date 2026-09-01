"""L1 heavy candidates scored on BOTH legs: light->heavy and heavy->shared charged.

Picking a heavy on its light leg alone risks one with no charged partner. Since the charged
looks shareable -- its 0.450s window buys 117cm of travel against the heavy's 65cm -- L1's
heavy can be chosen to keep that sharing intact.

Budgets are window x the shipping closing rate (65.2cm / 0.25s), which is a single observed
point extrapolated linearly, so treat them as ranking devices rather than walls.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r", "pelvis"]
RATE = 65.2 / 0.25
T_HEAVY, T_CHARGED = 0.250, 0.450
BUD_H, BUD_C = RATE * T_HEAVY, RATE * T_CHARGED
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
L1 = B + "Attack4_Stage1_Complete_IP"
CHARGED, CH_AT = B + "Attack9_IP", 0.483
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


def hand(a, b):
    return math.dist(a["hand_r"], b["hand_r"])


def foot(a, b):
    return max(math.dist(a["foot_l"], b["foot_l"]), math.dist(a["foot_r"], b["foot_r"]))


lp = pose(unreal.load_asset(L1), 0.225)
cp = pose(unreal.load_asset(CHARGED), CH_AT)

rows = []
for line in list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]:
    f = line.rstrip("\n").split("\t")
    if f[5] != "Y":
        continue
    name, strike, pkg = f[0], float(f[2]), f[7]
    if any(k in name for k in ("Attack4_Stage1", "Attack8_Stage2", "Attack2_Stage2",
                               "Attack6", "Attack3_", "Attack10_IP", "Attack9_IP")):
        continue
    entry = strike - T_HEAVY
    seq = unreal.load_asset(pkg)
    hp = pose(seq, entry)
    lh, lf = hand(lp, hp), foot(lp, hp)
    at350 = pose(seq, entry + 0.200)          # a fitted heavy runs at rate 1
    ch, cf = hand(at350, cp), foot(at350, cp)
    ok = (lh <= BUD_H and ch <= BUD_C)
    rows.append((0 if ok else 1, lh + ch * (T_HEAVY / T_CHARGED), lh, lf, ch, cf, name, entry))

rows.sort()
print("budgets: light->heavy %.0fcm, heavy->charged %.0fcm  (charged = Attack9 @%.3f)"
      % (BUD_H, BUD_C, CH_AT))
print("\n  %-42s %-18s %-18s" % ("L1 heavy candidate", "light leg", "charged leg"))
for ok, _, lh, lf, ch, cf, name, entry in rows[:10]:
    mark = "  BOTH LEGS INSIDE" if ok == 0 else ""
    print("  %-42s hand %3d foot %3d  hand %3d foot %3d  @%.3f%s"
          % (name[3:45], lh, lf, ch, cf, entry, mark))
print("\nDONE")
