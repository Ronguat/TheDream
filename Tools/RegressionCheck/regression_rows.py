"""Per-row evaluators for the scripted rows, and the vocabulary they are written in.

    regression_rows.py <id> <slice> [--tape <tape.tsv>]

Prints one PASS or FAIL line per assertion and a "N passed, M failed" summary; exit 1 on any FAIL.
A row is a function in ROWS taking a Context and a Result. The legacy rows stay in
regression-check.sh; a row is written here when it is new or is being changed for another reason.

Every count-shaped assertion fails on n=0: a row that examined nothing has proven nothing.
"""
import argparse
import os
import re
import sys

FRAME = 1.0 / 60.0
TRACE = re.compile(r"LogTDCombatTiming: \[(\d+\.\d+)\] (.*)$")
MARKER = re.compile(r"REGRESSION (\w+) (.*)$")


# --- reading ------------------------------------------------------------------

class Context(object):
    def __init__(self, slice_path, tape_path=None):
        self.trace, self.markers = [], []
        for line in open(slice_path, errors="replace"):
            m = TRACE.search(line)
            if m:
                self.trace.append((float(m.group(1)), m.group(2).rstrip()))
                continue
            m = MARKER.search(line)
            if m:
                self.markers.append((m.group(1), m.group(2).rstrip()))
        self.roles = {}
        for kind, rest in self.markers:
            if kind == "ROLES":
                for tok in rest.split()[1:]:
                    if "=" in tok:
                        r, n = tok.split("=", 1)
                        self.roles[r] = n
        self.reps = self._segments()
        self.tape = self._tape(tape_path) if tape_path and os.path.exists(tape_path) else {}

    def _segments(self):
        """Trace lines per rep: rep k runs from the previous REP readout's game time to its own."""
        bounds = []
        for kind, rest in self.markers:
            if kind == "REP":
                f = fields(rest)
                if "game" in f:
                    n = int(f.get("n", len(bounds)))
                    if not bounds or bounds[-1][0] != n:
                        bounds.append((n, f["game"]))
        if not bounds:
            return [list(self.trace)]
        segs, start = [], -1.0
        for _n, end in bounds:
            segs.append([(t, x) for t, x in self.trace if start < t <= end])
            start = end
        return segs

    def _tape(self, path):
        out = {}
        with open(path) as fh:
            head = fh.readline().rstrip("\n").split("\t")
            for line in fh:
                parts = line.rstrip("\n").split("\t")
                if len(parts) < len(head):
                    continue
                row = dict(zip(head, parts))
                for k in ("frame",):
                    row[k] = int(row[k])
                for k in ("t", "x", "y", "z", "yaw", "health", "stamina"):
                    row[k] = float(row[k])
                out.setdefault(row["pawn"], []).append(row)
        return out

    def who(self, role):
        return self.roles.get(role, "")

    def variant(self, k, scenario):
        plans = scenario.get("plans") or [scenario.get("plan") or []]
        return k % len(plans)


def fields(text):
    out = {}
    for k, v in re.findall(r"([A-Za-z]+)=(-?\d+\.?\d*)", text):
        out[k] = float(v)
    return out


def sfield(text, name):
    m = re.search(r"\b%s=([^\s,]+)" % re.escape(name), text)
    return m.group(1) if m else None


def tag_of(text):
    out = []
    for tok in text.split():
        if tok.isalpha() and tok.isupper():
            out.append(tok)
        else:
            break
    return " ".join(out)


def lines(seg, tag, who=None, contains=None):
    """Lines whose tag matches exactly, optionally naming a pawn and carrying a substring."""
    out = []
    for t, x in seg:
        if tag_of(x) != tag:
            continue
        if who and who not in x:
            continue
        if contains and contains not in x:
            continue
        out.append((t, x))
    return out


def first(seg, tag, who=None, contains=None, after=None):
    for t, x in lines(seg, tag, who, contains):
        if after is None or t > after:
            return t, x
    return None, None


def frames_between(a, b):
    return int(round((b - a) / FRAME))


class Result(object):
    def __init__(self):
        self.rows = []

    def add(self, ok, label, detail):
        self.rows.append(("PASS" if ok else "FAIL", label, detail))

    def counted(self, n, label, detail_ok, detail_zero="no samples"):
        """A count that must be positive to mean anything."""
        self.add(n > 0, label, detail_ok if n > 0 else detail_zero)

    @property
    def failed(self):
        return sum(1 for s, _, _ in self.rows if s != "PASS")

    def show(self):
        for status, label, detail in self.rows:
            print("  %-6s %-40s %s" % (status, label, detail))
        print("  %d passed, %d failed" % (len(self.rows) - self.failed, self.failed))


# --- the rows -------------------------------------------------------------------

def _accept(ctx, r, s, stun, stun_end, refusal, needs=None):
    """A press early in a stun is discarded; one inside the acceptance window fires at the stun's
    end. stun/stun_end are the bracketing tags, refusal the REFUSED reason, needs an extra line each
    rep must carry for the rep to count."""
    player = ctx.who("player")
    early_ok, late_ok, refused, n_early, n_late, lat = 0, 0, 0, 0, 0, []
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        hit_t, _ = first(seg, stun, player)
        end_t, _ = first(seg, stun_end, player)
        presses = [p for p in lines(seg, "INPUT", player, "InputTag.Attack pressed")
                   if hit_t is not None and p[0] >= hit_t]
        if hit_t is None or end_t is None or not presses:
            continue
        if needs and not lines(seg, needs[0], player, needs[1]):
            continue
        refused += len(lines(seg, "REFUSED", player, refusal))
        acts = [t for t, _ in lines(seg, "ACTIVATE", player) if t > hit_t]
        if v == 0:
            n_early += 1
            expired = lines(seg, "BUFFER", player, "expired")
            if expired and not acts:
                early_ok += 1
        else:
            n_late += 1
            stored = lines(seg, "BUFFER", player, "stored")
            if stored and acts:
                lat.append(frames_between(end_t, acts[0]))
                if abs(acts[0] - end_t) <= FRAME + 1e-3:
                    late_ok += 1
    r.counted(n_early + n_late, "reps with a press inside the stun", "%d reps" % (n_early + n_late))
    r.add(refused >= n_early + n_late and refused > 0, "every press refused naming the stun",
          "%d refusals across %d presses" % (refused, n_early + n_late))
    r.add(n_early > 0 and early_ok == n_early, "early press discarded, nothing fires",
          "%d of %d early reps expired with no activation" % (early_ok, n_early))
    r.add(n_late > 0 and late_ok == n_late, "late press fires at %s" % stun_end,
          "%d of %d late reps within 1 f; latencies %s f" % (late_ok, n_late, lat))


def input_accept_hitstun(ctx, r, s):
    _accept(ctx, r, s, "HITSTUN", "HITSTUN END", ": hitstun")


def input_accept_blockstun(ctx, r, s):
    _accept(ctx, r, s, "BLOCKSTUN", "BLOCKSTUN END", "Blockstun", needs=("BLOCKED", None))


def input_accept_lockout(ctx, r, s):
    _accept(ctx, r, s, "PARRY LOCKOUT", "PARRY LOCKOUT END", "parry lockout")


def _getup_held(ctx, r, s):
    """Held inputs reach the input window; the priority order decides between several."""
    player = ctx.who("player")
    expect = s.get("expect", {})
    by = expect["by"]
    held = expect["held"]
    lockout = float(expect["lockout"])
    auto_at = float(expect["auto_at"])
    good, n, held_ok, held_n, refused_stand, want_refusals, rise_frames = 0, 0, 0, 0, 0, 0, []
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        kd_t, kd = first(seg, "KNOCKDOWN", player, "type=")
        rise_t, rise = first(seg, "KNOCKDOWN RISE", player)
        if kd_t is None or rise_t is None:
            continue
        n += 1
        token = sfield(rise, "by")
        want = by[v]
        target = lockout if want != "auto" else auto_at
        span = rise_t - kd_t
        rise_frames.append(frames_between(kd_t, rise_t))
        if token == want and abs(span - target) <= FRAME + 1e-3:
            good += 1
        if held[v]:
            held_n += 1
            if lines(seg, "KNOCKDOWN", player, "rose on held InputTag.%s" % held[v]):
                held_ok += 1
        if want == "auto":
            want_refusals += 1
            if lines(seg, "REFUSED", player, "no stand from a hard knockdown") or \
                    lines(seg, "REFUSED", player, "knocked down"):
                refused_stand += 1
    r.counted(n, "knockdowns with a rise", "%d reps" % n)
    r.add(n > 0 and good == n, "rise by the expected option on the expected frame",
          "%d of %d; rise frames %s" % (good, n, rise_frames))
    r.add(held_n > 0 and held_ok == held_n, "rose on held names the winning input",
          "%d of %d" % (held_ok, held_n))
    if want_refusals:
        r.add(refused_stand == want_refusals, "held stand refused where the type forbids it",
              "%d of %d" % (refused_stand, want_refusals))


def knockdown_getup_held(ctx, r, s):
    _getup_held(ctx, r, s)


def knockdown_getup_held_normal(ctx, r, s):
    _getup_held(ctx, r, s)


def knockdown_getup_exhausted_held(ctx, r, s):
    """Exhaustion refuses the defensive get-ups and leaves the attack; the wait still rises."""
    _getup_held(ctx, r, s)
    player = ctx.who("player")
    n, ordered = 0, 0
    for seg in ctx.reps:
        kd_t, _ = first(seg, "KNOCKDOWN", player, "type=")
        ex_t, _ = first(seg, "EXHAUSTED", player)
        if kd_t is None:
            continue
        n += 1
        if ex_t is not None and ex_t < kd_t:
            ordered += 1
    r.add(n > 0 and ordered == n, "exhausted before every knockdown",
          "%d of %d" % (ordered, n))


def edge_light_checkpoint(ctx, r, s):
    """Holds either side of the light checkpoint commit different tiers; the hold on it is reported."""
    player = ctx.who("player")
    expect = s.get("expect", {})
    holds = expect["holds"]              # frames per variant
    want = expect["want"]                # branch per variant, None = report only
    seen = {}
    lengths_ok, n = 0, 0
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        press_t, _ = first(seg, "INPUT", player, "InputTag.Attack pressed")
        rel_t, _ = first(seg, "INPUT", player, "InputTag.Attack released")
        _, commit = first(seg, "COMMIT", player)
        if press_t is None or rel_t is None or commit is None:
            continue
        n += 1
        if frames_between(press_t, rel_t) == holds[v]:
            lengths_ok += 1
        m = re.search(r"branch (\d+)", commit)
        seen.setdefault(v, []).append(int(m.group(1)) if m else -1)
    r.counted(n, "reps with a press, a release and a commit", "%d reps" % n)
    r.add(lengths_ok == n and n > 0, "held for exactly the planned frames",
          "%d of %d" % (lengths_ok, n))
    for v, branches in sorted(seen.items()):
        label = "hold %d f" % holds[v]
        if want[v] is None:
            # Reported, never asserted: which side a hold on the checkpoint falls is the ruling the
            # report exists to inform, and the frame after it is a race the report has to show.
            r.add(True, label + " on the checkpoint: reported",
                  "REPORT branch %s across %d reps" % (branches, len(branches)))
        else:
            r.add(all(b == want[v] for b in branches), label + " commits branch %d" % want[v],
                  "saw %s" % branches)


def lock_guard_break(ctx, r, s):
    """A broken guard cannot walk or jump; the same held move walks the instant the stun ends."""
    player = ctx.who("player")
    tape = ctx.tape.get(player, [])
    breaks, equal, moved_after, refusals = 0, 0, 0, 0
    pairs = 0
    worst = 0.0
    segs = ctx.reps
    for k in range(0, len(segs) - 1, 2):
        a, b = segs[k], segs[k + 1]          # a holds a move, b is the control
        ba_t, _ = first(a, "GUARD BREAK", player)
        ea_t, _ = first(a, "GUARD END", player)
        bb_t, _ = first(b, "GUARD BREAK", player)
        eb_t, _ = first(b, "GUARD END", player)
        if None in (ba_t, ea_t, bb_t, eb_t):
            continue
        breaks += 2
        pairs += 1
        # Any reason: a break empties the bar, so exhaustion is up at every break and its refusal
        # shadows the break's own. The movement lock above is what separates the two states.
        refusals += len(lines(a, "REFUSED", player, "GA_Jump"))
        pa = [row for row in tape if ba_t <= row["t"] <= ea_t + 0.2]
        pb = [row for row in tape if bb_t <= row["t"] <= eb_t + 0.2]
        if not pa or not pb:
            continue
        ax, ay = pa[0]["x"], pa[0]["y"]
        bx, by_ = pb[0]["x"], pb[0]["y"]
        # Displacement from the break, compared at matching offsets through the stun.
        diff = 0.0
        for ra in pa:
            if ra["t"] > ea_t:
                break
            off = ra["t"] - ba_t
            rb = min(pb, key=lambda q: abs((q["t"] - bb_t) - off))
            da = ((ra["x"] - ax) ** 2 + (ra["y"] - ay) ** 2) ** 0.5
            db = ((rb["x"] - bx) ** 2 + (rb["y"] - by_) ** 2) ** 0.5
            diff = max(diff, abs(da - db))
        worst = max(worst, diff)
        if diff <= 2.0:
            equal += 1
        # After GUARD END the held move must show within six frames.
        at_end = min(pa, key=lambda q: abs(q["t"] - ea_t))
        later = [q for q in pa if ea_t < q["t"] <= ea_t + 6 * FRAME + 1e-3]
        if later:
            d = max(((q["x"] - at_end["x"]) ** 2 + (q["y"] - at_end["y"]) ** 2) ** 0.5 for q in later)
            if d > 1.0:
                moved_after += 1
    r.counted(breaks, "guard breaks observed", "%d across %d pairs" % (breaks, pairs))
    r.add(pairs > 0 and equal == pairs, "held move displaces nothing through the stun",
          "%d of %d pairs within 2 cm of the control; worst %.1f cm" % (equal, pairs, worst))
    r.add(pairs > 0 and moved_after == pairs, "the held move walks within 6 f of GUARD END",
          "%d of %d" % (moved_after, pairs))
    r.add(refusals >= pairs and pairs > 0, "jump refused inside the stun",
          "%d refusals across %d stuns" % (refusals, pairs))


def reach_light(ctx, r, s):
    """A body just inside MaxReachCm plus its radius is struck; one just outside is not."""
    player, target = ctx.who("player"), ctx.who("defender")
    expect = s.get("expect", {})
    hits = expect["hits"]                # True/False per variant
    n, ok, dists = 0, 0, []
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        if not lines(seg, "ACTIVATE", player):
            continue
        n += 1
        landed = bool(lines(seg, "DAMAGED", target, "by " + player))
        if landed == hits[v]:
            ok += 1
        _, rel = first(seg, "TARGET", player, "release")
        if rel:
            d = fields(rel).get("dist")
            if d is not None:
                dists.append(round(d, 1))
    r.counted(n, "swings thrown", "%d reps" % n)
    r.add(n > 0 and ok == n, "hit inside reach, miss outside",
          "%d of %d as placed; release distances %s" % (ok, n, dists))


ROWS = {
    "input-accept-hitstun": input_accept_hitstun,
    "input-accept-blockstun": input_accept_blockstun,
    "input-accept-lockout": input_accept_lockout,
    "knockdown-getup-exhausted-held": knockdown_getup_exhausted_held,
    "knockdown-getup-held": knockdown_getup_held,
    "knockdown-getup-held-normal": knockdown_getup_held_normal,
    "edge-light-checkpoint": edge_light_checkpoint,
    "lock-guard-break": lock_guard_break,
    "reach-light": reach_light,
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("id")
    ap.add_argument("slice")
    ap.add_argument("--tape")
    a = ap.parse_args()
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import scenarios as SC
    if a.id not in ROWS:
        print("  no evaluator for %s" % a.id)
        return 2
    ctx = Context(a.slice, a.tape)
    r = Result()
    ROWS[a.id](ctx, r, SC.SCENARIOS[a.id])
    r.show()
    return 1 if r.failed else 0


if __name__ == "__main__":
    sys.exit(main())
