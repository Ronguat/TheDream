"""Hand-off smoothness from ue_chart_ab.py charts. Plain Python, no editor.

  python ue_ab_metrics.py <column> <chart.tsv> [<chart.tsv> ...]

column is the montage whose first reported position marks the hand-off (AM_Heavy2, AM_Charged1...).
Per chart: the speed of the rendered hand across the swap tick, the largest one-tick step, and the
roughness of the first 200 ms -- the mean absolute change in hand speed per 10 ms bin, with the
number of acceleration sign reversals and the path length -- then the binned speed series from
100 ms before the swap to 400 ms after, one column per chart. Shots stall the tick, so measure
roughness on runs made without them.
"""
import math, os, sys

BIN = 0.010


def load(path):
    rows = [l.rstrip("\n").split("\t") for l in open(path)]
    head = rows[0]
    body = [[float(x) for x in r] for r in rows[1:]]
    return head, body


def analyse(path, col_name):
    head, body = load(path)
    if col_name not in head:
        return None, "%s: no column %s" % (os.path.basename(path), col_name)
    col, hx = head.index(col_name), head.index("hx")
    t = [r[0] for r in body]
    p = [(r[hx], r[hx + 1], r[hx + 2]) for r in body]
    k = next((i for i, r in enumerate(body) if r[col] >= 0.0), None)
    if k is None:
        return None, "%s: %s never played" % (os.path.basename(path), col_name)
    v = [0.0] + [math.dist(p[i], p[i - 1]) / (t[i] - t[i - 1]) if t[i] > t[i - 1] else 0.0 for i in range(1, len(body))]
    t0 = t[k]
    jump = max((math.dist(p[i], p[i - 1]) for i in range(max(1, k), len(body)) if t[i] - t0 <= 0.100), default=0.0)
    bins = {}
    for i in range(1, len(body)):
        b = int(math.floor((t[i] - t0) / BIN))
        if -10 <= b < 40 and t[i] > t[i - 1]:
            bins.setdefault(b, [0.0, 0.0])
            bins[b][0] += math.dist(p[i], p[i - 1]); bins[b][1] += t[i] - t[i - 1]
    series = {b: (s / d if d > 0 else 0.0) for b, (s, d) in bins.items()}
    seq = [series.get(b, 0.0) for b in range(0, 20)]
    d = [seq[i] - seq[i - 1] for i in range(1, len(seq))]
    rev = sum(1 for i in range(1, len(d)) if (d[i] > 0) != (d[i - 1] > 0))
    summary = ("%-30s swap @%.3f  speed %5.0f -> %5.0f cm/s  max one-tick step %5.1f cm | first 200 ms: roughness %4.0f cm/s per bin, "
               "%d reversals, path %4.0f cm" % (os.path.basename(path), t0, v[k - 1] if k > 0 else 0.0, v[k], jump,
                                                sum(abs(x) for x in d) / len(d), rev, sum(seq) * BIN))
    return series, summary


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(2)
    col_name, paths = sys.argv[1], sys.argv[2:]
    got = []
    for path in paths:
        series, summary = analyse(path, col_name)
        print(summary)
        if series:
            got.append((os.path.basename(path), series))
    if got:
        print("\n  ms  " + "".join("%22s" % n[:22] for n, _ in got))
        for b in range(-10, 40):
            print("%5d " % (b * 10) + "".join("%22.0f" % s.get(b, float("nan")) for _, s in got))
