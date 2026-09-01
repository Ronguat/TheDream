"""Velocity profile of the rendered hand through each handover -- a pop is a spike at the seam,
smooth acceleration is the incoming attack doing its job. Reported in 25ms game-time bins."""
import math, os

HERE = os.path.dirname(os.path.abspath(__file__))
rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(HERE, "p4_chart.tsv"))][1:]
T = [float(r[0]) for r in rows]
S = [[float(r[1]), float(r[2]), float(r[3])] for r in rows]
W = [float(r[4]) for r in rows]
H = [tuple(float(x) for x in r[5].split(",")) for r in rows]


def first_on(i):
    return min([k for k in range(len(rows)) if S[k][i] >= 0], default=None)


for a, b, label in ((0, 1, "L1 -> L2"), (1, 2, "L2 -> L3")):
    k0 = first_on(b)
    if k0 is None:
        continue
    print(f"\n{label}   handover at row {k0}, t={T[k0]:.3f}")
    print("   ms   speed cm/s   slotW   incoming pos")
    lo = max(0, k0 - 2)
    bins = {}
    for k in range(lo + 1, len(rows)):
        ms = int((T[k] - T[k0]) * 1000)
        if ms > 260:
            break
        v = math.dist(H[k], H[k - 1]) / max(T[k] - T[k - 1], 1e-9)
        bins.setdefault(ms // 25 * 25, []).append((v, W[k], S[k][b]))
    for ms in sorted(bins):
        vs = bins[ms]
        peak = max(x[0] for x in vs)
        print(f"  {ms:4d}   {peak:9.0f}   {vs[-1][1]:5.2f}   {vs[-1][2]:6.3f}")
print("\nDONE")
