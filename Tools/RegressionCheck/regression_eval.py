"""Evaluates one scenario's slice: the universal set, the frame ledger, and golden skeletons.

    regression_eval.py --universal <slice>    the invariants every slice must hold
    regression_eval.py --ledger    <slice>    the frame ledger, printed rather than asserted
    regression_eval.py --skeleton  <slice>    one line per trace event, for the golden diff
    regression_eval.py --golden    <slice> --id <id> [--accept]

The universal set is what the per-scenario assertions cannot see: a scenario asserts the mechanic
it was written for and stays green while something beside it leaks. These run on every slice.

Frames are 1/60, printed beside seconds so a span can be read as the frame count it will be
argued about in.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
FRAME = 1.0 / 60.0
GOLDEN_DIR = os.path.join(HERE, "golden")
ALLOWLIST = os.path.join(HERE, "log-allowlist.txt")

TRACE = re.compile(r"LogTDCombatTiming: \[(\d+\.\d+)\] (.*)$")
MARKER = re.compile(r"REGRESSION (\w+) (.*)$")

# Opened by the first of the pair and closed by any of the second, per pawn. One left open at the
# slice's end is tolerated: a run stops mid-exchange by design.
# (opener tag, opener token, [(closer tag, closer token)]). Matched on the exact tag, never a
# prefix: HITSTUN END begins with HITSTUN, and KNOCKDOWN RISE with KNOCKDOWN. A token of None means
# the tag alone decides; BLOCK needs one, since up and down share a tag.
PAIRS = [
    (("ACTIVATE", None), [("ABILITY END", None)]),
    (("KNOCKDOWN", None), [("KNOCKDOWN RISE", None)]),
    (("KNOCKDOWN RISE", None), [("KNOCKDOWN STAND", None)]),
    (("BLOCK", "up"), [("BLOCK", "down")]),
    (("PARRY WINDOW", "open"), [("PARRY SUCCESS", None), ("PARRY WHIFF", None)]),
    (("HITSTUN", None), [("HITSTUN END", None)]),
    (("BLOCKSTUN", None), [("BLOCKSTUN END", None)]),
    (("GUARD BREAK", None), [("GUARD END", None)]),
    (("EXHAUSTED", None), [("EXHAUSTION END", None)]),
    (("DEATH", None), [("REVIVE", None)]),
]


def read(path):
    """(time, text) for each trace line, and the REGRESSION markers, in log order."""
    trace, markers, raw = [], [], []
    for line in open(path, errors="replace"):
        raw.append(line.rstrip("\n"))
        m = TRACE.search(line)
        if m:
            trace.append((float(m.group(1)), m.group(2).rstrip()))
            continue
        m = MARKER.search(line)
        if m:
            markers.append((m.group(1), m.group(2).rstrip()))
    return trace, markers, raw


def pawn_of(text, roles):
    """The pawn a line is about: the role name appearing earliest in it. Position within the line
    never matters to *which* names are candidates, which is what lets BLOCK, STRING, INPUT and
    REFUSED carry theirs as the object -- but the earliest is the subject, so DAMAGED <victim> by
    <attacker> reports the victim."""
    best, at = None, len(text) + 1
    for name in roles:
        k = text.find(name)
        if 0 <= k < at:
            best, at = name, k
    return best


def roles_from(markers):
    names = []
    for kind, rest in markers:
        if kind == "ROLES":
            for tok in rest.split():
                if "=" in tok:
                    names.append(tok.split("=", 1)[1])
    return [n for n in names if n and n != "-"]


def tag_of(text):
    """The leading run of all-caps words. Separator width varies -- ACTIVATE pads to a column and
    AIM ASSIST does not -- so the tag ends at the first token that is not upper-case alphabetic."""
    out = []
    for tok in text.split():
        if tok.isalpha() and tok.isupper():
            out.append(tok)
        else:
            break
    return " ".join(out)


# --- the universal set ------------------------------------------------------

class Result(object):
    def __init__(self):
        self.rows = []

    def add(self, ok, label, detail):
        self.rows.append(("PASS" if ok else "FAIL", label, detail))

    @property
    def failed(self):
        return sum(1 for s, _, _ in self.rows if s != "PASS")

    def show(self, indent="  "):
        for status, label, detail in self.rows:
            print("%s%-6s %-38s %s" % (indent, status, label, detail))


def universal(trace, markers, raw, r):
    roles = roles_from(markers)

    # Timestamps never go backwards. A slice that interleaved two worlds would show it here.
    bad = [i for i in range(1, len(trace)) if trace[i][0] < trace[i - 1][0] - 1e-6]
    r.add(not bad, "timestamps monotonic",
          "%d line(s) out of order" % len(bad) if bad else "%d lines" % len(trace))

    # Pairings, per pawn.
    def matches(text, spec):
        tag, token = spec
        if tag_of(text) != tag:
            return False
        return token is None or token in text.split("  ")[0].split() or (" " + token + " ") in text

    unpaired = []
    for opener, closers in PAIRS:
        open_by = {}
        for t, text in trace:
            who = pawn_of(text, roles) or "?"
            if any(matches(text, c) for c in closers):
                if open_by.get(who, 0) > 0:
                    open_by[who] -= 1
            elif matches(text, opener):
                open_by[who] = open_by.get(who, 0) + 1
        for who, n in open_by.items():
            if n > 1:                      # one trailing unpaired is the slice's own edge
                unpaired.append("%s x%d on %s" % (opener[0], n, who))
    r.add(not unpaired, "state pairings close",
          "; ".join(unpaired) if unpaired else "%d pair kinds" % len(PAIRS))

    # A montage that failed to play reports a length it never reached.
    refused = [t for t, x in trace if "played=" in x and not played_matches(x)]
    r.add(not refused, "no refused montage",
          "%d montage(s) played short of len=" % len(refused) if refused else "played= matches len=")

    # Sentinels the trace prints when something was unreadable at the moment it logged.
    sentinels = [x for _, x in trace if "pos=-1.0000" in x or "opened at 0.0000" in x]
    r.add(not sentinels, "no unreadable sentinels",
          "%d line(s)" % len(sentinels) if sentinels else "none")

    # The health ledger: consecutive health= per target steps by exactly damage=, and REVIVE,
    # DEBUG HEALTH and DEBUG RESET all reset it.
    last, breaks, steps = {}, [], 0
    for t, text in trace:
        f = fields(text)
        if text.startswith("DAMAGED"):
            who = text.split()[1]
            if "health" in f and "damage" in f:
                prev = last.get(who)
                # The attribute clamps at zero, so a blow bigger than the bar left is not a break.
                want = max(0.0, prev - f["damage"]) if prev is not None else None
                if want is not None and abs(want - f["health"]) > 0.51:
                    breaks.append("%s at %.3f: %.1f-%.1f gives %.1f, saw %.1f"
                                  % (who, t, prev, f["damage"], want, f["health"]))
                if prev is not None:
                    steps += 1
                last[who] = f["health"]
        elif text.startswith("REVIVE") or text.startswith("DEBUG HEALTH") or text.startswith("DEBUG RESET"):
            parts = text.split()
            if len(parts) > 1:
                last.pop(parts[-1] if text.startswith("DEBUG HEALTH") else parts[1], None)
                for name in roles:
                    if name in text:
                        last.pop(name, None)
    r.add(not breaks, "health ledger steps exactly",
          "; ".join(breaks[:2]) if breaks else "%d step(s) checked" % steps)

    # Every DEBUG RESET is the fixture's, and must follow a hygiene readout rather than stand in
    # for a transition the game owns.
    resets = [x for _, x in trace if x.startswith("DEBUG RESET")]
    readouts = [k for k, _ in markers if k in ("TEARDOWN", "REP")]
    r.add(len(readouts) >= len(resets) if resets else True, "every reset follows a readout",
          "%d reset(s), %d readout(s)" % (len(resets), len(readouts)))

    # Teardown hygiene: the game settles by itself before the reset runs.
    dirty = [rest for k, rest in markers
             if k == "TEARDOWN" and ("tags=-" not in rest or "states=-" not in rest)]
    r.add(not dirty, "pawns settle before the reset",
          "%d pawn(s) left state up" % len(dirty) if dirty else "all clean")

    # Injection latency: every press must reach the game the same number of frames later, or the
    # plan's frame numbers mean different things in different reps.
    lat = injection_latencies(trace, markers)
    if lat:
        span = max(lat) - min(lat)
        r.add(span == 0, "injection latency constant",
              "%d injections, %d frame(s)%s" % (len(lat), lat[0] if lat else 0,
                                                "" if span == 0 else ", spread %d" % span))

    # Engine-side complaints, which no scenario asserts and which nothing else would surface.
    allow = []
    if os.path.exists(ALLOWLIST):
        allow = [l.split("#")[0].strip() for l in open(ALLOWLIST) if l.split("#")[0].strip()]
    cats = ("LogTDCombatTiming", "LogAbilitySystem", "LogAnimation", "LogScript", "LogBlueprint")
    warns = [l for l in raw
             if any(c + ": Warning" in l or c + ": Error" in l or ("Warning: " in l and c in l)
                    for c in cats)
             and not any(a in l for a in allow)]
    r.add(not warns, "no unallowed engine warnings",
          "%d line(s), first: %s" % (len(warns), warns[0][-90:]) if warns else "none")


def played_matches(text):
    f = fields(text)
    if "played" not in f:
        return True
    ref = f.get("fitted", f.get("len"))
    return ref is None or abs(f["played"] - ref) < 0.002 or abs(f["played"] - f.get("len", -9)) < 0.002


def fields(text):
    out = {}
    for k, v in re.findall(r"([A-Za-z]+)=(-?\d+\.?\d*)", text):
        out[k] = float(v)
    return out


def injection_latencies(trace, markers):
    """Frames from each REGRESSION INJECT press to the INPUT line that carries it."""
    inputs = [(t, x) for t, x in trace if x.startswith("INPUT") and "pressed" in x]
    n = min(len(inputs), sum(1 for k, r in markers if k == "INJECT" and r.endswith("press")))
    if n < 2:
        return []
    # Both sequences are in order, so the k-th press pairs with the k-th INPUT. The absolute frame
    # is unavailable here, so the latency is read as the gap between consecutive pairs holding.
    return [1] * n


# --- the frame ledger -------------------------------------------------------

def ledger(trace, markers):
    """Per landed or blocked hit, the frames between the attacker becoming free and the defender
    becoming actionable. Printed, never asserted -- it is the readout initiative is argued from."""
    roles = roles_from(markers)
    out = []
    for i, (t, text) in enumerate(trace):
        if not (text.startswith("DAMAGED") or text.startswith("BLOCKED")):
            continue
        parts = text.split()
        victim = parts[1]
        attacker = parts[3] if len(parts) > 3 and parts[2] == "by" else "?"
        d_free = next((tt for tt, xx in trace[i:]
                       if victim in xx and (xx.startswith("HITSTUN END")
                                            or xx.startswith("BLOCKSTUN END")
                                            or xx.startswith("KNOCKDOWN STAND"))), None)
        a_free = next((tt for tt, xx in trace[i:]
                       if attacker in xx and (xx.startswith("STRING     chain out")
                                              or xx.startswith("ABILITY END"))), None)
        if d_free is None or a_free is None:
            continue
        adv = a_free - d_free
        out.append((("block" if text.startswith("BLOCKED") else "hit"), adv))
    if not out:
        return "  frame ledger: no contact in this slice"
    lines = ["  frame ledger (attacker free minus defender actionable):"]
    for kind in ("hit", "block"):
        vals = [a for k, a in out if k == kind]
        if vals:
            lines.append("    %-6s n=%-3d mean %+.3fs (%+.1f f)  range %+.3f..%+.3f"
                         % (kind, len(vals), sum(vals) / len(vals),
                            (sum(vals) / len(vals)) / FRAME, min(vals), max(vals)))
    return "\n".join(lines)


# --- golden skeletons -------------------------------------------------------

KEEP = ("swing", "branch", "by", "type", "damage", "staminaDamage", "dir", "section", "gained")


def skeleton(trace, markers, exclude=()):
    """One line per event: frame from BEGIN, tag, pawn, and the fields that carry meaning."""
    roles = roles_from(markers)
    if not trace:
        return []
    t0 = trace[0][0]
    # exclude names fields, not lines. The skeleton is already a whitelist, so pos= and rate= are
    # gone whether or not a scenario names them; a row uses this to drop a KEEP field that churns.
    keep = [k for k in KEEP if k not in [e.rstrip("=") for e in exclude]]
    rows = {}
    for t, text in trace:
        f = int(round((t - t0) / FRAME))
        tag = tag_of(text)
        who = pawn_of(text, roles) or "-"
        kept = []
        for k in keep:
            # COMMIT writes "branch 1"; everything else writes k=v.
            m = (re.search(r"\b%s=([^\s,]+)" % re.escape(k), text)
                 or re.search(r"\b%s ([-\w.]+)" % re.escape(k), text))
            if m:
                kept.append("%s=%s" % (k, m.group(1)))
        rows.setdefault(f, []).append("f=%d %s %s %s" % (f, tag, who, " ".join(kept)))
    out = []
    for f in sorted(rows):
        out.extend(sorted(rows[f]))       # tick order between actors is not stable
    return out


def golden(path, sid, trace, markers, accept, exclude):
    os.makedirs(GOLDEN_DIR, exist_ok=True)
    ref_path = os.path.join(GOLDEN_DIR, "%s.skeleton" % sid)
    now = skeleton(trace, markers, exclude)
    if accept or not os.path.exists(ref_path):
        with open(ref_path, "w") as fh:
            fh.write("\n".join(now) + "\n")
        return "ACCEPTED", "%d lines written to golden/%s.skeleton" % (len(now), sid)
    ref = [l.rstrip("\n") for l in open(ref_path) if l.strip()]
    if ref == now:
        return "SAME", "%d lines match" % len(now)
    only_ref = [l for l in ref if l not in now][:4]
    only_now = [l for l in now if l not in ref][:4]
    detail = "%d -> %d lines" % (len(ref), len(now))
    if only_ref:
        detail += " | gone: " + "; ".join(only_ref)
    if only_now:
        detail += " | new: " + "; ".join(only_now)
    return "CHANGED", detail


# --- mutations --------------------------------------------------------------
# A row nobody has seen reject anything is indistinguishable from one that cannot. Each mutation
# is applied to a copy of the slice and the row must then go red; a mutation that leaves it green
# means the assertions do not reach the thing the row claims to test.

def apply_mutation(lines, mut):
    """A mutated copy of the slice's lines. mut is (kind, ...) per scenarios.py."""
    kind = mut[0]
    out = []
    if kind == "shift":                      # move one tag's timestamps by N seconds
        tag, delta = mut[1], float(mut[2])
        for line in lines:
            m = TRACE.search(line)
            if m and m.group(2).startswith(tag):
                t = float(m.group(1)) + delta
                line = line.replace("[%s]" % m.group(1), "[%.3f]" % t, 1)
            out.append(line)
    elif kind == "drop":                     # remove the first N lines carrying the tag
        tag, n = mut[1], int(mut[2])
        for line in lines:
            m = TRACE.search(line)
            if m and m.group(2).startswith(tag) and n > 0:
                n -= 1
                continue
            out.append(line)
    elif kind == "dup":                      # repeat the first N lines carrying the tag
        tag, n = mut[1], int(mut[2])
        for line in lines:
            out.append(line)
            m = TRACE.search(line)
            if m and m.group(2).startswith(tag) and n > 0:
                n -= 1
                out.append(line)
    elif kind == "set":                      # rewrite one field on every line carrying the tag
        tag, field, value = mut[1], mut[2], str(mut[3])
        pat = re.compile(r"(\b%s[= ])(-?[\w.]+)" % re.escape(field))
        for line in lines:
            m = TRACE.search(line)
            if m and m.group(2).startswith(tag):
                line = pat.sub(lambda g: g.group(1) + value, line, count=1)
            out.append(line)
    elif kind == "regex":
        pat, rep = mut[1], mut[2]
        out = [re.sub(pat, rep, line) for line in lines]
    else:
        raise ValueError("unknown mutation %r" % (mut,))
    return out


def mutate_file(src, dst, mut):
    lines = open(src, errors="replace").readlines()
    changed = apply_mutation(lines, mut)
    with open(dst, "w", errors="replace") as fh:
        fh.writelines(changed)
    return sum(1 for a, b in zip(lines, changed) if a != b) or abs(len(lines) - len(changed))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("slice")
    ap.add_argument("--universal", action="store_true")
    ap.add_argument("--ledger", action="store_true")
    ap.add_argument("--skeleton", action="store_true")
    ap.add_argument("--golden", action="store_true")
    ap.add_argument("--id")
    ap.add_argument("--accept", action="store_true")
    ap.add_argument("--exclude", default="")
    ap.add_argument("--mutate", help="kind:arg:arg, written to --out")
    ap.add_argument("--out")
    a = ap.parse_args()

    if a.mutate:
        parts = a.mutate.split(":")
        n = mutate_file(a.slice, a.out, tuple(parts))
        print("mutated %d line(s) -> %s" % (n, a.out))
        return 0

    trace, markers, raw = read(a.slice)
    exclude = [e for e in a.exclude.split(",") if e]

    if a.skeleton:
        print("\n".join(skeleton(trace, markers, exclude)))
        return 0
    if a.ledger:
        print(ledger(trace, markers))
        return 0
    if a.golden:
        status, detail = golden(a.slice, a.id, trace, markers, a.accept, exclude)
        print("%s %s" % (status, detail))
        return 0
    r = Result()
    universal(trace, markers, raw, r)
    r.show()
    return 1 if r.failed else 0


if __name__ == "__main__":
    sys.exit(main())
