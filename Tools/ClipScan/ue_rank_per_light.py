"""Per-light heavy ranking with both filters folded in.

Position gap alone scored a clip that was light 3's own motion as the best candidate, because
re-entering the same animation has nothing to blend. So two dimensions are added:

  direction  angle between the source's hand velocity and the target's at its entry
  overlap    whether the candidate's motion around its entry appears anywhere in a light

Neither carries an invented threshold. Direction is read against the shipping handovers, and
overlap against two controls: two lights that are genuinely different, and the known-shared
Attack2_IP case.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
BONES = ["hand_r", "foot_l", "foot_r", "pelvis"]
DT, T_HEAVY = 1.0 / 30.0, 0.250
B = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
SRC = [("L1", B + "Attack4_Stage1_Complete_IP", 0.225, 1.500),
       ("L2", B + "Attack8_Stage2_Complete_IP", 0.502, 3.344),
       ("L3", B + "Attack2_Stage2_Complete_IP", 0.360, 2.403)]
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


def angle(sseq, sat, srate, dseq, dat):
    def v(s, at, rate):
        a, b = pose(s, at)["hand_r"], pose(s, at + DT * rate)["hand_r"]
        return tuple((b[i] - a[i]) / DT for i in range(3))
    u, w = v(sseq, sat, srate), v(dseq, dat, 1.0)
    mu = math.sqrt(sum(x * x for x in u)); mw = math.sqrt(sum(x * x for x in w))
    if mu < 1e-6 or mw < 1e-6:
        return -1.0
    d = sum(u[i] * w[i] for i in range(3)) / (mu * mw)
    return math.degrees(math.acos(max(-1.0, min(1.0, d))))


def overlap(cseq, entry, lseq):
    """Min mean bone disagreement between the candidate around its entry and any part of a light."""
    lL = AL.get_sequence_length(lseq)
    probes = [pose(cseq, entry + d) for d in (0.0, 0.10, 0.20)]
    best = 1e9
    off = 0.0
    while off <= max(0.0, lL - 0.20):
        e = sum(agree(probes[i], pose(lseq, off + d))
                for i, d in enumerate((0.0, 0.10, 0.20))) / 3.0
        best = min(best, e)
        off += lL / 30.0
    return best


lights = [(t, unreal.load_asset(p), a, r) for t, p, a, r in SRC]
print("CONTROLS for the overlap number")
print("  two genuinely different lights (L1 vs L2): %.2f cm"
      % overlap(lights[0][1], 0.225, lights[1][1]))
print("  known shared motion (Attack2_IP vs L3):    %.2f cm"
      % overlap(unreal.load_asset(B + "Attack2_IP"), 1.250, lights[2][1]))
print("\nDIRECTION reference, shipping handovers")
print("  L1 -> L2  %.1f deg" % angle(lights[0][1], 0.5411, 0.601, lights[1][1], 0.0))
print("  L2 -> L3  %.1f deg" % angle(lights[1][1], 0.9770, 1.000, lights[2][1], 0.0))

rows = []
for line in list(open(os.path.join(HERE, "p2_screen.tsv")))[1:]:
    f = line.rstrip("\n").split("\t")
    if f[5] != "Y":
        continue
    name, strike, pkg = f[0], float(f[2]), f[7]
    if any(x in name for x in ("AnimV3_Attack4_Stage1", "AnimV3_Attack8_Stage2", "AnimV3_Attack2_Stage2")):
        continue
    entry = strike - T_HEAVY
    seq = unreal.load_asset(pkg)
    tp = pose(seq, entry)
    per = []
    for tag, ls, la, lr in lights:
        sp = pose(ls, la)
        per.append((math.dist(sp["hand_r"], tp["hand_r"]),
                    max(math.dist(sp["foot_l"], tp["foot_l"]), math.dist(sp["foot_r"], tp["foot_r"])),
                    angle(ls, la, lr, seq, entry)))
    rows.append((name, entry, seq, per))

print("\n%d candidates. Best 5 per light after overlap screening." % len(rows))
for i, (tag, ls, la, lr) in enumerate(lights):
    ranked = sorted(rows, key=lambda r: math.hypot(r[3][i][0], r[3][i][1]))
    print("\n  %s:" % tag)
    shown = 0
    for name, entry, seq, per in ranked:
        if shown >= 5:
            break
        ov = min(overlap(seq, entry, l[1]) for l in lights)
        h, ft, ang = per[i]
        tagline = "DISQUALIFIED shares a light's motion" if ov < 12.0 else ""
        print("    hand %3d  foot %3d  dir %5.1f  overlap %5.1f   %-46s @%.3f %s"
              % (h, ft, ang, ov, name[3:49], entry, tagline))
        shown += 1
print("\nDONE")
