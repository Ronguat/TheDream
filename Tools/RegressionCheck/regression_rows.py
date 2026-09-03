"""Every row's assertions, and the vocabulary they are written in.

    regression_rows.py <id> <slice> [--tape <tape.tsv>]
    regression_rows.py --self-test

Prints one PASS or FAIL line per assertion and a "N passed, M failed" summary; exit 1 on any FAIL.
A row is a function in ROWS taking a Context, a Result and its scenario. Scripted rows read the
slice per rep; the whole-slice rows read the trace end to end. Authored values come from the mirror,
Docs/Combat-Values.tsv; tolerances are the constants beside the rows that use them.

Every count-shaped assertion fails on n=0: a row that examined nothing has proven nothing. The
self-test runs a known-good band and a wrong one over a fixed slice and fails unless both answer.
"""
import argparse
import math
import os
import re
import sys

FRAME = 1.0 / 60.0
TRACE = re.compile(r"LogTDCombatTiming: \[(\d+\.\d+)\] (.*)$")
MARKER = re.compile(r"REGRESSION (\w+) (.*)$")


# --- reading ------------------------------------------------------------------

class Context(object):
    def __init__(self, slice_path, tape_path=None):
        self.trace, self.trace_raw, self.markers, self.raw = [], [], [], []
        for line in open(slice_path, errors="replace"):
            self.raw.append(line)
            m = TRACE.search(line)
            if m:
                self.trace.append((float(m.group(1)), m.group(2).rstrip()))
                self.trace_raw.append("[%s] %s" % (m.group(1), m.group(2).rstrip()))
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
    for k, v in re.findall(r"([A-Za-z]+)=([-+]?\d+\.?\d*)", text):
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
    """Held inputs reach the input window; the priority order decides between several. A roll or
    kip-up is priced, spans DodgeSeconds and travels its distance or none, i-framed; a guard is up
    within its gap of the rise; hard refuses the stand by name."""
    player = ctx.who("player")
    expect = s.get("expect", {})
    by = expect["by"]
    held = expect["held"]
    lockout = float(expect["lockout"])
    auto_at = float(expect["auto_at"])
    good, n, held_ok, held_n, refused_stand, want_refusals, rise_frames = 0, 0, 0, 0, 0, 0, []
    rolls, roll_ok, roll_detail, guards, guard_ok, stands, stand_refused = 0, 0, [], 0, 0, 0, 0
    dodge_s = float(mirror("GA_Dodge", "DodgeSeconds"))
    dist = float(mirror("GA_Dodge", "DodgeTargetDistanceCm"))
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
            # The held input was refused rather than ignored. Any reason: exhaustion shadows the
            # knockdown's own reason whenever it is up, and the absent rise is the fact asserted.
            want_refusals += 1
            if [t for t, _ in lines(seg, "REFUSED", player) if kd_t <= t <= rise_t]:
                refused_stand += 1
        after = [(t, x) for t, x in seg if t >= rise_t - 1e-6]
        if token in ("dodge", "kipup"):
            rolls += 1
            d = first(after, "DODGE", player, "dir=")
            e = first(after, "DODGE END", player)
            stands_at = fields(rise).get("stands")
            problems = []
            if d[1] is None or e[1] is None:
                problems.append("no dodge")
            else:
                if abs(fields(d[1]).get("remaining", -1) - (float(mirror(
                        "BP_PlayerCharacter", "StartingMaxStamina")) - DODGE_COST)) > 0.01:
                    problems.append("remaining %s" % fields(d[1]).get("remaining"))
                travel = fields(e[1]).get("dist", -1)
                if token == "dodge" and not (dist + DODGE_BAND_CM[0] <= travel <= dist + DODGE_BAND_CM[1]):
                    problems.append("travel %.0f" % travel)
                if token == "kipup" and travel > KIPUP_TRAVEL_MAX:
                    problems.append("travel %.0f" % travel)
                if [t for t, _ in lines(seg, "DAMAGED", player) if rise_t <= t <= e[0]]:
                    problems.append("hit during it")
            if stands_at is None or abs((stands_at - rise_t) - dodge_s) > KD_SPAN_TOL:
                problems.append("rise span %s" % (None if stands_at is None else "%.3f" % (stands_at - rise_t)))
            if problems:
                roll_detail.append("rep %d %s: %s" % (k, token, ", ".join(problems)))
            else:
                roll_ok += 1
        if token == "block":
            guards += 1
            up = first(after, "BLOCK", player, "up on")
            if up[0] is not None and up[0] - rise_t <= BLOCK_GUARD_GAP + 1e-6:
                guard_ok += 1
        holds_jump = any(len(st) > 3 and st[2] == "hold" and st[3] == "jump" for st in (s.get("plans") or [[]])[v])
        if expect.get("hard") and holds_jump and want == "auto":
            stands += 1
            if [t for t, x in lines(seg, "REFUSED", player, "no stand from a hard knockdown") if kd_t <= t <= rise_t]:
                stand_refused += 1
    r.counted(n, "knockdowns with a rise", "%d reps" % n)
    r.add(n > 0 and good == n, "rise by the expected option on the expected frame",
          "%d of %d; rise frames %s" % (good, n, rise_frames))
    r.add(held_n > 0 and held_ok == held_n, "rose on held names the winning input",
          "%d of %d" % (held_ok, held_n))
    if want_refusals:
        r.add(refused_stand == want_refusals, "held input refused where nothing may rise",
              "%d of %d" % (refused_stand, want_refusals))
    if rolls:
        r.add(roll_ok == rolls, "the roll or kip-up costs %.0f, spans DodgeSeconds, travels as authored, i-framed"
              % DODGE_COST, "%d of %d%s" % (roll_ok, rolls, "; " + "; ".join(roll_detail[:3]) if roll_detail else ""))
    if guards:
        r.add(guard_ok == guards, "the guard is up within %.3f s of the rise" % BLOCK_GUARD_GAP,
              "%d of %d" % (guard_ok, guards))
    if stands:
        r.add(stand_refused == stands, "hard refuses the stand by name", "%d of %d" % (stand_refused, stands))


def knockdown_getup_held(ctx, r, s):
    _getup_held(ctx, r, s)


def knockdown_getup_held_normal(ctx, r, s):
    _getup_held(ctx, r, s)


def knockdown_getup_tap_priority(ctx, r, s):
    """The rise comes at the lockout's end and the held option takes it over the buffered tap."""
    player = ctx.who("player")
    lockout = float(s["expect"]["lockout"])
    want = s["expect"]["by"]
    n, at_open, by = 0, 0, []
    for seg in ctx.reps:
        kd_t, _ = first(seg, "KNOCKDOWN", player, "type=")
        rise_t, rise = first(seg, "KNOCKDOWN RISE", player)
        if kd_t is None or rise_t is None:
            continue
        n += 1
        if abs((rise_t - kd_t) - lockout) <= FRAME + 1e-3:
            at_open += 1
        by.append(sfield(rise, "by"))
    r.counted(n, "knockdowns that rose", "%d" % n)
    r.add(n > 0 and at_open == n, "the rise comes at the lockout's end", "%d of %d within 1 f" % (at_open, n))
    r.add(n > 0 and all(b == want for b in by), "the held %s outranks the buffered tap at the open" % want,
          "saw %s" % by)


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


# --- the authored values, read from the mirror rather than typed -------------------------------

_MIRROR = {}


def mirror(obj, prop):
    """A value off Docs/Combat-Values.tsv, the dated mirror of the live CDOs."""
    if not _MIRROR:
        path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
            os.path.abspath(__file__)))), "Docs", "Combat-Values.tsv")
        for line in open(path, encoding="utf-8"):
            if line.startswith("#") or line.startswith("Object\t"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 3:
                _MIRROR[(parts[0], parts[1])] = parts[2]
    return _MIRROR[(obj, prop)]


def parry_reward(ctx, r, s):
    """Every catch with room on the bar pays the authored reward in full."""
    player = ctx.who("player")
    want = float(mirror("BP_PlayerCharacter", "ParryStaminaReward"))
    n, full, spent = 0, 0, 0
    for seg in ctx.reps:
        catches = lines(seg, "PARRY SUCCESS", player)
        if not catches:
            continue
        n += len(catches)
        if lines(seg, "DEBUG STAMINA", player):
            spent += 1
        full += sum(1 for _, x in catches if abs(fields(x).get("gained", -1) - want) < 0.01)
    r.counted(n, "catches", "%d" % n)
    r.add(spent > 0 and spent == len([1 for seg in ctx.reps if lines(seg, "PARRY SUCCESS", player)]),
          "the bar was spent before every catch", "%d rep(s)" % spent)
    r.add(n > 0 and full == n, "every catch credits the full reward",
          "%d of %d credited %.0f" % (full, n, want))


def tier_cells(ctx, r, s):
    """Each cell's release timing, total, escalation count, committed branch and montage, from the
    player's own presses, read against the mirror."""
    player = ctx.who("player")
    cells = s["expect"]["cells"]
    n, rel_ok, tot_ok, esc_ok, com_ok, mont_ok = 0, 0, 0, 0, 0, 0
    detail = []
    tot_first, tot_first_ok, tot_chain, tot_chain_ok, chain_detail = 0, 0, 0, 0, []
    span_n, span_ok, span_detail = 0, 0, []
    for k, seg in enumerate(ctx.reps):
        p, b = cells[ctx.variant(k, s)]
        acts = lines(seg, "ACTIVATE", player, "swing=%d" % p)
        if not acts:
            continue
        a_t, a_x = acts[-1]
        start = next(i for i, (t, x) in enumerate(seg) if t == a_t and x == a_x)
        after = seg[start + 1:]
        rel = first(after, "RELEASE BEGIN", player)
        end = next(((t, x) for t, x in after if tag_of(x) == "ABILITY END" and player in x
                    and "(cancelled)" not in x), (None, None))
        com = first(after, "COMMIT", player)
        if rel[0] is None or end[0] is None or com[1] is None:
            continue
        n += 1
        release_at = float(mirror("GA_Attack", "Branches[%d].ReleaseAtSeconds" % b))
        total = release_at + float(mirror("GA_Attack", "Positions[%d].Cells[%d].ReleaseSeconds" % (p, b))) \
            + float(mirror("GA_Attack", "Positions[%d].Cells[%d].RecoverySeconds" % (p, b)))
        montage = mirror("GA_Attack", "Positions[%d].Cells[%d].Montage" % (p, b)).rsplit(".", 1)[-1]
        if abs((rel[0] - a_t) - release_at) <= 0.030:
            rel_ok += 1
        rel_end = first(after, "RELEASE END", player)
        release_s = float(mirror("GA_Attack", "Positions[%d].Cells[%d].ReleaseSeconds" % (p, b)))
        if rel_end[0] is not None:
            span_n += 1
            over = (rel_end[0] - rel[0]) - release_s
            if -0.009 <= over <= 0.020:
                span_ok += 1
            else:
                span_detail.append("cell %d/%d release %.3f vs %.3f" % (p, b, rel_end[0] - rel[0], release_s))
        elapsed = fields(end[1]).get("elapsed", -1)
        over = elapsed - total
        if p == 0:
            tot_first += 1
            if -0.009 <= over <= 0.050 + 1e-6:
                tot_first_ok += 1
            else:
                detail.append("cell %d/%d total %.3f vs %.3f" % (p, b, elapsed, total))
        else:
            tot_chain += 1
            if -0.009 <= over <= 0.050 + 1e-6:
                tot_chain_ok += 1
            else:
                chain_detail.append("cell %d/%d %+.3f" % (p, b, over))
        escalations = len([1 for t, x in after if t <= end[0] and tag_of(x) == "ESCALATE" and player in x])
        if escalations == b:
            esc_ok += 1
        m = re.search(r"branch (\d+)", com[1])
        if m and int(m.group(1)) == b:
            com_ok += 1
        swaps = [x for t, x in after if t <= end[0] and tag_of(x) == "TIER SWAP" and player in x]
        if (b == 0 and not swaps) or (b > 0 and swaps and ("'%s'" % montage) in swaps[-1]):
            mont_ok += 1
    r.counted(n, "cells thrown", "%d reps across 9 cells" % n)
    r.add(n > 0 and rel_ok == n, "release opens at the branch's ReleaseAt", "%d of %d within 30 ms" % (rel_ok, n))
    r.add(span_n > 0 and span_ok == span_n, "release lasts the cell's ReleaseSeconds, -0.5 to +1 f",
          "%d of %d%s" % (span_ok, span_n, "; " + "; ".join(sorted(set(span_detail))[:4]) if span_detail else ""))
    r.add(tot_first > 0 and tot_first_ok == tot_first, "position 1 totals: authored sum, -0.5 to +3 f",
          "%d of %d%s" % (tot_first_ok, tot_first, "; " + "; ".join(detail[:3]) if detail else ""))
    r.add(tot_chain > 0 and tot_chain_ok == tot_chain, "chained totals: authored sum, -0.5 to +3 f",
          "%d of %d%s" % (tot_chain_ok, tot_chain, "; " + "; ".join(sorted(set(chain_detail))[:4]) if chain_detail else ""))
    r.add(n > 0 and esc_ok == n, "escalations equal the branch", "%d of %d" % (esc_ok, n))
    r.add(n > 0 and com_ok == n, "commit names the branch", "%d of %d" % (com_ok, n))
    r.add(n > 0 and mont_ok == n, "the cell's own montage is swapped in", "%d of %d" % (mont_ok, n))


def _string_player(ctx, r, s):
    """The player's string: three swings at the cadence, each landing or blocked as the target
    stands, every value read against the mirror."""
    player, target = ctx.who("player"), ctx.who("defender")
    blocked = bool(s["expect"].get("blocked"))
    gap_lo, gap_hi = 0.500 - 0.045, 0.500 + 0.045
    lat_lo, lat_hi = 0.125, 0.175
    dmg = float(mirror("GA_Attack", "Positions[0].Cells[0].Damage"))
    sdmg = float(mirror("GA_Attack", "Positions[0].Cells[0].StaminaDamage"))
    hit_stun = float(mirror("GA_Attack", "Positions[0].Cells[0].HitstunSeconds"))
    blk_stun = float(mirror("GA_Attack", "Positions[0].Cells[0].BlockstunSeconds"))
    strings, gaps, lats, contacts, stuns, kb_ok, kb_n, kd, bad_contact = 0, [], [], 0, [], 0, 0, 0, 0
    for seg in ctx.reps:
        acts = lines(seg, "ACTIVATE", player)
        idx = [int(sfield(x, "swing") or -1) for _, x in acts]
        if idx != [0, 1, 2]:
            continue
        strings += 1
        for (t0, _), (t1, _) in zip(acts, acts[1:]):
            gaps.append(round(t1 - t0, 3))
        offs = lines(seg, "RELEASE OFF", player)
        for (to, _), (ta, _) in zip(offs, acts[1:]):
            lats.append(round(ta - to, 3))
        if blocked:
            hits = lines(seg, "BLOCKED", target)
            contacts += len(hits)
            bad_contact += sum(1 for _, x in hits if abs(fields(x).get("staminaDamage", -1) - sdmg) > 0.01)
            bad_contact += len(lines(seg, "DAMAGED", target))
            for t, x in lines(seg, "BLOCKSTUN", target):
                stuns.append(round(fields(x)["until"] - t, 3))
            kbs = lines(seg, "KNOCKBACK", target, "(blocked)")
        else:
            hits = lines(seg, "DAMAGED", target)
            contacts += len(hits)
            bad_contact += sum(1 for _, x in hits if abs(fields(x).get("damage", -1) - dmg) > 0.01)
            for t, x in lines(seg, "HITSTUN", target):
                stuns.append(round(fields(x)["until"] - t, 3))
            kbs = lines(seg, "KNOCKBACK", target)
            kd += len(lines(seg, "KNOCKDOWN", target, "type="))
        for _, x in kbs:
            kb_n += 1
            m = re.search(r"spacing=(-?\d+\.?\d*) \(authored (-?\d+\.?\d*)\)", x)
            if m and float(m.group(1)) >= float(m.group(2)) - 0.5:
                kb_ok += 1
    r.counted(strings, "strings of three swings", "%d" % strings)
    r.add(gaps and all(gap_lo <= g <= gap_hi for g in gaps), "chain gap at the tapped cadence",
          "n=%d in [%.3f, %.3f]: %s" % (len(gaps), gap_lo, gap_hi, sorted(set(gaps))))
    r.add(lats and all(lat_lo <= l <= lat_hi for l in lats), "chain latency inside its band",
          "n=%d in [%.3f, %.3f]: %s" % (len(lats), lat_lo, lat_hi, sorted(set(lats))))
    kind = "BLOCKED" if blocked else "DAMAGED"
    r.add(strings > 0 and contacts == 3 * strings and bad_contact == 0,
          "every swing %s for the cell's value" % ("is blocked" if blocked else "lands"),
          "%d %s across %d strings, %d off value" % (contacts, kind, strings, bad_contact))
    want = blk_stun if blocked else hit_stun
    r.add(stuns and all(abs(v - want) <= 0.020 for v in stuns),
          "%s spans the authored %.3f" % ("blockstun" if blocked else "hitstun", want),
          "n=%d: %s" % (len(stuns), sorted(set(stuns))))
    if blocked:
        r.add(kb_n == contacts and kb_n > 0, "one blocked knockback per blocked hit",
              "%d knockbacks, %d blocked hits" % (kb_n, contacts))
    else:
        r.add(strings > 0 and kd == strings, "the ender floors the target once per string",
              "%d knockdowns across %d strings" % (kd, strings))
    r.add(kb_n > 0 and kb_ok == kb_n, "knockback never pulls inward", "%d of %d" % (kb_ok, kb_n))


def string_player_cadence(ctx, r, s):
    _string_player(ctx, r, s)


def string_player_blocked(ctx, r, s):
    _string_player(ctx, r, s)


def dodge_directions(ctx, r, s):
    """Every section resolves from a held direction, fitted to the dash and travelling its distance
    along that direction."""
    player = ctx.who("player")
    dirs = s["expect"]["dirs"]
    dist = float(mirror("GA_Dodge", "DodgeTargetDistanceCm"))
    fit = float(mirror("GA_Dodge", "DodgeClipSeconds"))
    n, sec_ok, fit_ok, dist_ok, split_ok, seen = 0, 0, 0, 0, 0, {}
    comp = dict(Fw=(1, 0), FR=(1, 1), R=(0, 1), BR=(-1, 1), Bw=(-1, 0), BL=(-1, -1), L=(0, -1), FL=(1, -1))
    for k, seg in enumerate(ctx.reps):
        want = dirs[ctx.variant(k, s)]
        _, d = first(seg, "DODGE", player)
        _, e = first(seg, "DODGE END", player)
        if not d or not e:
            continue
        n += 1
        sec = sfield(d, "section")
        seen.setdefault(want, []).append(sec)
        if sec == want:
            sec_ok += 1
        if abs(fields(d).get("fitLen", -1) - fit) < 0.002:
            fit_ok += 1
        f = fields(e)
        travelled = f.get("dist", -1)
        if abs(travelled - dist) <= 15.0:
            dist_ok += 1
        fw, rt = comp[want]
        norm = (fw * fw + rt * rt) ** 0.5
        want_fwd, want_right = dist * fw / norm, dist * rt / norm
        if abs(f.get("fwd", 0) - want_fwd) <= 20.0 and abs(f.get("right", 0) - want_right) <= 20.0:
            split_ok += 1
    r.counted(n, "dodges with a start and an end", "%d across %d directions" % (n, len(seen)))
    r.add(n > 0 and sec_ok == n, "section follows the held direction",
          "%d of %d; %s" % (sec_ok, n, {k: v for k, v in seen.items() if any(x != k for x in v)} or "all as held"))
    r.add(n > 0 and fit_ok == n, "fitted to the dash, %.3f" % fit, "%d of %d" % (fit_ok, n))
    r.add(n > 0 and dist_ok == n, "travels %.0f within 15 cm" % dist, "%d of %d" % (dist_ok, n))
    r.add(n > 0 and split_ok == n, "travel lies along the held direction", "%d of %d within 20 cm per axis" % (split_ok, n))


def dodge_iframes(ctx, r, s):
    """A dodge opened into the swing takes no damage, and the swing runs on; the control is hit."""
    player, attacker = ctx.who("player"), ctx.who("attacker")
    dodged, clean, ran_on, controls, hit = 0, 0, 0, 0, 0
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        if not lines(seg, "ACTIVATE", attacker):
            continue
        if v == 0:
            d_t, _ = first(seg, "DODGE", player)
            e_t, _ = first(seg, "DODGE END", player)
            if d_t is None or e_t is None:
                continue
            dodged += 1
            contact = [t for t, _ in lines(seg, "DAMAGED", player) + lines(seg, "BLOCKED", player)
                       if d_t <= t <= e_t]
            if not contact:
                clean += 1
            if not lines(seg, "LUNGE STOP", attacker):
                ran_on += 1
        else:
            controls += 1
            if lines(seg, "DAMAGED", player):
                hit += 1
    r.counted(dodged, "dodges into the swing", "%d" % dodged)
    r.add(dodged > 0 and clean == dodged, "nothing lands during the dodge", "%d of %d" % (clean, dodged))
    r.add(dodged > 0 and ran_on == dodged, "the evaded swing runs on, no LUNGE STOP", "%d of %d" % (ran_on, dodged))
    r.add(controls > 0 and hit == controls, "the control rep is hit", "%d of %d" % (hit, controls))


def _refused_row(ctx, r, s, ability, absent_tag=None, absent_contains=None, label="refused"):
    """Every rep refuses the ability and nothing of absent_tag follows."""
    player = ctx.who("player")
    n, refused, leaked = 0, 0, 0
    for seg in ctx.reps:
        presses = lines(seg, "INPUT", player, "pressed")
        if not presses:
            continue
        n += 1
        if lines(seg, "REFUSED", player, ability):
            refused += 1
        if absent_tag and lines(seg, absent_tag, player, absent_contains):
            leaked += 1
    r.counted(n, "reps with a press", "%d" % n)
    r.add(n > 0 and refused == n, "%s %s every rep" % (ability, label), "%d of %d" % (refused, n))
    if absent_tag:
        r.add(leaked == 0, "no %s afterwards" % absent_tag, "%d rep(s) leaked" % leaked)


def attack_airborne(ctx, r, s):
    _refused_row(ctx, r, s, "GA_Attack", "ACTIVATE")
    player = ctx.who("player")
    stored = sum(1 for seg in ctx.reps for _ in lines(seg, "BUFFER", player, "InputTag.Attack: stored"))
    r.add(stored == 0, "the airborne press is not buffered", "%d stored" % stored)


def dodge_airborne(ctx, r, s):
    _refused_row(ctx, r, s, "GA_Dodge", "DODGE")
    player = ctx.who("player")
    stored = sum(1 for seg in ctx.reps for _ in lines(seg, "BUFFER", player, "InputTag.Dodge: stored"))
    r.add(stored == 0, "the airborne press is not buffered", "%d stored" % stored)


def attack_whiff_commitment(ctx, r, s):
    _refused_row(ctx, r, s, "GA_Dodge", "DODGE")


def parry_refused(ctx, r, s):
    _refused_row(ctx, r, s, "GA_Parry", "PARRY WINDOW", "open")


def block_facing(ctx, r, s):
    """Faced away the light lands; faced toward it is blocked."""
    player = ctx.who("player")
    away = s["expect"]["away"]
    n, ok = 0, 0
    for k, seg in enumerate(ctx.reps):
        if not lines(seg, "ACTIVATE", ctx.who("attacker")):
            continue
        n += 1
        landed, blocked = bool(lines(seg, "DAMAGED", player)), bool(lines(seg, "BLOCKED", player))
        if away[ctx.variant(k, s)] and landed and not blocked:
            ok += 1
        if not away[ctx.variant(k, s)] and blocked and not landed:
            ok += 1
    r.counted(n, "swings at the player", "%d" % n)
    r.add(n > 0 and ok == n, "the guard covers the front and not the back", "%d of %d" % (ok, n))


def parry_facing(ctx, r, s):
    player = ctx.who("player")
    n = sum(1 for seg in ctx.reps if lines(seg, "PARRY WINDOW", player, "open"))
    caught = sum(1 for seg in ctx.reps if lines(seg, "PARRY SUCCESS", player))
    r.counted(n, "windows opened facing away", "%d" % n)
    r.add(n > 0 and caught == n, "every window catches", "%d of %d" % (caught, n))


def block_stun_offense_only(ctx, r, s):
    """Inside blockstun a dodge fires, a parry opens, and an attack is refused."""
    player = ctx.who("player")
    want = ["DODGE", "PARRY WINDOW", "REFUSED"]
    n, ok, detail = 0, 0, []
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        b_t, _ = first(seg, "BLOCKSTUN", player)
        e_t, _ = first(seg, "BLOCKSTUN END", player)
        if b_t is None or e_t is None:
            continue
        n += 1
        inside = [(t, x) for t, x in seg if b_t <= t <= e_t]
        if v == 0:
            hit = bool(lines(inside, "DODGE", player))
        elif v == 1:
            hit = bool(lines(inside, "PARRY WINDOW", player, "open"))
        else:
            hit = bool(lines(inside, "REFUSED", player, "GA_Attack")) and not lines(inside, "ACTIVATE", player)
        ok += 1 if hit else 0
        if not hit:
            detail.append("rep %d wanted %s" % (k, want[v]))
    r.counted(n, "blockstuns with a press inside", "%d" % n)
    r.add(n > 0 and ok == n, "dodge and parry fire in blockstun, the attack is refused",
          "%d of %d%s" % (ok, n, "; " + "; ".join(detail[:3]) if detail else ""))


def block_commitment(ctx, r, s):
    """A release inside the floor drops the guard when the floor ends; one after it, at once."""
    player = ctx.who("player")
    want = s["expect"]["down_at"]
    n, ok, seen = 0, 0, []
    for k, seg in enumerate(ctx.reps):
        up_t, _ = first(seg, "BLOCK", player, "up on")
        dn_t, _ = first(seg, "BLOCK", player, "(released)")
        if up_t is None or dn_t is None:
            continue
        n += 1
        f = frames_between(up_t, dn_t)
        seen.append(f)
        if abs(f - want[ctx.variant(k, s)]) <= 1:
            ok += 1
    r.counted(n, "guards raised and released", "%d" % n)
    r.add(n > 0 and ok == n, "guard drops at the floor or at the release", "frames %s" % seen)


def input_last_wins(ctx, r, s):
    player = ctx.who("player")
    n, superseded, dodged, no_attack = 0, 0, 0, 0
    for seg in ctx.reps:
        end_t, _ = first(seg, "HITSTUN END", player)
        if end_t is None:
            continue
        n += 1
        if lines(seg, "BUFFER", player, "dropped, superseded by InputTag.Dodge"):
            superseded += 1
        d = [t for t, _ in lines(seg, "DODGE", player) if abs(t - end_t) <= FRAME + 1e-3]
        if d:
            dodged += 1
        if not [t for t, _ in lines(seg, "ACTIVATE", player) if t > end_t - 1.0]:
            no_attack += 1
    r.counted(n, "hitstuns with two presses", "%d" % n)
    r.add(n > 0 and superseded == n, "the attack is superseded by the dodge", "%d of %d" % (superseded, n))
    r.add(n > 0 and dodged == n, "the dodge fires at HITSTUN END", "%d of %d" % (dodged, n))
    r.add(n > 0 and no_attack == n, "no attack fires", "%d of %d" % (no_attack, n))


def input_parry_never_buffers(ctx, r, s):
    player = ctx.who("player")
    n, refused, stored, opened = 0, 0, 0, 0
    for seg in ctx.reps:
        end_t, _ = first(seg, "HITSTUN END", player)
        if end_t is None or not lines(seg, "INPUT", player, "InputTag.Parry pressed"):
            continue
        n += 1
        if lines(seg, "REFUSED", player, "GA_Parry"):
            refused += 1
        if lines(seg, "BUFFER", player, "InputTag.Parry: stored"):
            stored += 1
        if [t for t, _ in lines(seg, "PARRY WINDOW", player, "open") if t >= end_t - FRAME]:
            opened += 1
    r.counted(n, "parries pressed in hitstun", "%d" % n)
    r.add(n > 0 and refused == n, "refused", "%d of %d" % (refused, n))
    r.add(stored == 0 and opened == 0, "never buffered, no window after the stun",
          "%d stored, %d opened" % (stored, opened))


def input_block_never_replays(ctx, r, s):
    player = ctx.who("player")
    taps, holds, tap_up, hold_up = 0, 0, 0, 0
    for k, seg in enumerate(ctx.reps):
        end_t, _ = first(seg, "HITSTUN END", player)
        if end_t is None or not lines(seg, "INPUT", player, "InputTag.Block pressed"):
            continue
        ups = [t for t, _ in lines(seg, "BLOCK", player, "up on") if t >= end_t - FRAME]
        if ctx.variant(k, s) == 0:
            taps += 1
            tap_up += 1 if ups else 0
        else:
            holds += 1
            if ups and abs(ups[0] - end_t) <= FRAME + 1e-3:
                hold_up += 1
    r.counted(taps + holds, "block presses inside hitstun", "%d" % (taps + holds))
    r.add(taps > 0 and tap_up == 0, "a tap raises nothing after the stun", "%d of %d raised" % (tap_up, taps))
    r.add(holds > 0 and hold_up == holds, "a hold raises the guard at HITSTUN END", "%d of %d" % (hold_up, holds))


def death_midair(ctx, r, s):
    """Killed in the air, revived, and walking afterwards."""
    player = ctx.who("player")
    tape = ctx.tape.get(player, [])
    n, airborne, revived, walked = 0, 0, 0, 0
    for seg in ctx.reps:
        d_t, _ = first(seg, "DEATH", player)
        if d_t is None:
            continue
        n += 1
        rv_t, _ = first(seg, "REVIVE", player)
        if rv_t is not None:
            revived += 1
        rows = [q for q in tape if seg[0][0] <= q["t"] <= seg[-1][0]]
        if rows:
            floor = min(q["z"] for q in rows[:10])
            at_death = min(rows, key=lambda q: abs(q["t"] - d_t))
            if at_death["z"] - floor >= 20.0:
                airborne += 1
            if rv_t is not None:
                after = [q for q in rows if rv_t <= q["t"] <= rv_t + 3.0]
                if len(after) > 5:
                    d = ((after[-1]["x"] - after[0]["x"]) ** 2 + (after[-1]["y"] - after[0]["y"]) ** 2) ** 0.5
                    if d > 20.0:
                        walked += 1
    r.counted(n, "deaths", "%d" % n)
    r.add(n > 0 and airborne == n, "died at height", "%d of %d at 20 cm or more" % (airborne, n))
    r.add(n > 0 and revived == n, "revived", "%d of %d" % (revived, n))
    r.add(n > 0 and walked == n, "walks after the revive", "%d of %d" % (walked, n))


def edge(ctx, r, s):
    """Outcomes per probe: the sides asserted, the rest reported."""
    player = ctx.who("player")
    ex = s["expect"]
    probe, want, labels = ex["probe"], ex["want"], ex["labels"]
    seen = {}
    n = 0
    lock_probes, lock_refused = 0, 0
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        out = None
        if probe == "commit":
            _, c = first(seg, "COMMIT", player)
            m = re.search(r"branch (\d+)", c) if c else None
            out = m.group(1) if m else None
        elif probe == "chain":
            acts = lines(seg, "ACTIVATE", player)
            if acts:
                if lines(seg, "STRING", player, "chain out"):
                    out = "chain"
                elif len([1 for _, x in acts if "swing=0" in x]) >= 2:
                    out = "fresh"
                else:
                    out = "none"
        elif probe == "guard":
            up_t, _ = first(seg, "BLOCK", player, "up on")
            dn_t, _ = first(seg, "BLOCK", player, "(released)")
            if up_t is not None and dn_t is not None:
                out = str(frames_between(up_t, dn_t))
        elif probe == "fire":
            presses = lines(seg, "INPUT", player, "InputTag.Attack pressed")
            if len(presses) >= 2:
                t_p = presses[-1][0]
                acts = [t for t, x in lines(seg, "ACTIVATE", player) if t >= t_p - 1e-6]
                if acts:
                    d = frames_between(t_p, acts[0])
                    out = "now" if d == 0 else "held+%d" % d
        elif probe == "buffer":
            presses = lines(seg, "INPUT", player, "pressed")
            if presses:
                t_p = presses[-1][0]
                after = [(t, x) for t, x in seg if t >= t_p - 1e-6]
                buf = [x for _, x in lines(after, "BUFFER", player)]
                if any("expired" in x for x in buf):
                    out = "expired"
                elif any("fired" in x for x in buf):
                    out = "fired"
                elif any(frames_between(t_p, t) <= 1 for t, _ in lines(after, "ACTIVATE", player)):
                    out = "now"
        elif probe == "parry":
            if lines(seg, "PARRY SUCCESS", player):
                out = "caught"
            elif lines(seg, "DAMAGED", player):
                out = "hit"
        elif probe == "lockout":
            presses = lines(seg, "INPUT", player, "pressed")
            kd = first(seg, "KNOCKDOWN", player, "type=")
            rise = first(seg, "KNOCKDOWN RISE", player)
            if presses and kd[0] is not None and rise[0] is not None:
                t_p = presses[-1][0]
                m = re.search(r"by=(\w+)", rise[1])
                wait = frames_between(t_p, rise[0])
                cls = "stale" if wait < 0 else "now" if wait == 0 else "later"
                out = "%s,rise@%d,by=%s" % (cls, frames_between(kd[0], rise[0]), m.group(1) if m else "-")
        if out is None:
            continue
        n += 1
        seen.setdefault(v, []).append(out)
        if probe == "lockout" and out.startswith("later"):
            lock_probes += 1
            if lines(seg, "REFUSED", player, "knocked down (lockout)"):
                lock_refused += 1
    r.counted(n, "probes with an outcome", "%d reps" % n)
    for v in range(len(labels)):
        outs = seen.get(v, [])
        label = "%s %s" % (probe, labels[v])
        if want[v] is None:
            r.add(True, label + ": reported", "REPORT %s" % outs)
        else:
            r.add(bool(outs) and all(_edge_match(o, want[v]) for o in outs),
                  label + " -> %s" % want[v], "saw %s" % outs)
    plans = s.get("plans") or []
    presses = [st for p in plans for st in p if st[2] in ("tap", "press", "hold")]
    if probe in ("chain", "fire", "buffer") and presses and all(st[2] == "tap" and st[3] == "attack" for st in presses):
        branches = [re.search(r"branch (\d+)", x).group(1) for seg in ctx.reps
                    for _, x in lines(seg, "COMMIT", player) if re.search(r"branch (\d+)", x)]
        r.add(bool(branches) and all(b == "0" for b in branches), "every tap commits a light",
              "%d commits, branches %s" % (len(branches), sorted(set(branches))))
    if probe == "lockout":
        r.add(lock_probes > 0 and lock_refused == lock_probes, "REFUSED names the lockout on every press inside it",
              "%d of %d" % (lock_refused, lock_probes))


def _edge_match(out, want):
    """An outcome matches its side by class: 'held+3' is held, 'refused,by=auto' is refused."""
    return out == want or out.split("+")[0] == want or out.split(",")[0] == want


def reach_probes(ctx, r, s):
    """DAMAGED present or absent per placement, asserted where the row says so and reported else."""
    player = ctx.who("player")
    ex = s["expect"]
    hits, labels = ex["hits"], ex["labels"]
    seen = {}
    n = 0
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        if not lines(seg, "ACTIVATE", player):
            continue
        n += 1
        seen.setdefault(v, []).append(bool(lines(seg, "DAMAGED", None, "by " + player)))
    r.counted(n, "swings thrown", "%d reps" % n)
    for v in range(len(labels)):
        outs = ["hit" if o else "miss" for o in seen.get(v, [])]
        if hits[v] is None:
            r.add(True, labels[v] + ": reported", "REPORT %s" % outs)
        else:
            want = "hit" if hits[v] else "miss"
            r.add(bool(outs) and all(o == want for o in outs), "%s: %s" % (labels[v], want),
                  "saw %s" % outs)


def reach_aim_wedge(ctx, r, s):
    """A target inside the wedge is named and the commit bearing turned onto it; one outside leaves
    no candidate."""
    player, target = ctx.who("player"), ctx.who("defender")
    degs = s["expect"]["degrees"]
    seen = {}
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        if not lines(seg, "ACTIVATE", player):
            continue
        named = lines(seg, "AIM ASSIST", player, "'%s'" % target)
        none = lines(seg, "AIM ASSIST", player, "no candidate")
        tgt = first(seg, "TARGET", player, "commit")
        bearing = fields(tgt[1]).get("bearing") if tgt[1] else None
        hit = bool(lines(seg, "DAMAGED", None, "by " + player))
        seen.setdefault(v, []).append(("named" if named else "none" if none else "silent", bearing, hit))
    inside, outside = seen.get(0, []), seen.get(1, [])
    r.counted(sum(len(x) for x in seen.values()), "swings thrown", "%d" % sum(len(x) for x in seen.values()))
    r.add(bool(inside) and all(o[0] == "named" for o in inside),
          "%d deg: aim assist names the target" % degs[0], "saw %s" % [o[0] for o in inside])
    r.add(bool(inside) and all(o[1] is not None and abs(o[1]) <= 3.0 for o in inside),
          "%d deg: the commit bearing is turned within 3 deg" % degs[0],
          "bearings %s" % [o[1] for o in inside])
    r.add(bool(outside) and all(o[0] == "none" for o in outside),
          "%d deg: no candidate in the wedge" % degs[1], "saw %s" % [o[0] for o in outside])
    for v in range(2, len(degs)):
        r.add(True, "%d deg: reported" % degs[v], "REPORT %s" % [
            "%s bearing %s %s" % (o[0], o[1], "hit" if o[2] else "miss") for o in seen.get(v, [])])
    r.add(True, "hits at the asserted bearings: reported", "REPORT inside %s outside %s"
          % (["hit" if o[2] else "miss" for o in inside], ["hit" if o[2] else "miss" for o in outside]))


def reach_aim_gap(ctx, r, s):
    """Per distance: whether aim assist named the target, how far the player travelled, and whether
    the swing landed. The near probe must be named and hit, the far one neither."""
    player, target = ctx.who("player"), ctx.who("defender")
    dists = s["expect"]["distances"]
    tape = ctx.tape.get(player, [])
    seen = {}
    for k, seg in enumerate(ctx.reps):
        v = ctx.variant(k, s)
        act = first(seg, "ACTIVATE", player)
        end = first(seg, "ABILITY END", player)
        if act[0] is None or end[0] is None:
            continue
        named = bool(lines(seg, "AIM ASSIST", player, "'%s'" % target))
        hit = bool(lines(seg, "DAMAGED", None, "by " + player))
        rows = [q for q in tape if act[0] <= q["t"] <= end[0]]
        travel = _dist(rows[-1], rows[0]) if len(rows) > 1 else 0.0
        tgt = first(seg, "TARGET", player, "release")
        rel = fields(tgt[1]).get("dist") if tgt[1] else None
        seen.setdefault(v, []).append("%s travel=%.0f release-dist=%s %s" % (
            "named" if named else "none", travel, rel, "hit" if hit else "miss"))
    r.counted(sum(len(x) for x in seen.values()), "swings thrown", "%d" % sum(len(x) for x in seen.values()))
    near, far = seen.get(0, []), seen.get(1, [])
    r.add(bool(near) and all(o.startswith("named") and o.endswith("hit") for o in near),
          "%d cm: named and hit" % dists[0], "saw %s" % near)
    r.add(bool(far) and all(o.startswith("none") and o.endswith("miss") for o in far),
          "%d cm: neither" % dists[1], "saw %s" % far)
    for v in range(2, len(dists)):
        r.add(True, "%d cm: reported" % dists[v], "REPORT %s" % seen.get(v, []))


def parry_grace_catch(ctx, r, s):
    """Rep 1's second swing is caught by grace; later reps, 6 f further apart each, land."""
    player, second = ctx.who("player"), ctx.who("defender")
    out = []
    for seg in ctx.reps:
        catches = lines(seg, "PARRY SUCCESS", player)
        if not catches:
            out.append("uncaught")
            continue
        by = [re.search(r"by=(\w+)", x).group(1) for _, x in catches]
        graces = [x for _, x in lines(seg, "PARRY GRACE", player) if "END" not in x]
        hit = bool(lines(seg, "DAMAGED", player, "by " + second))
        out.append((by, len(graces), hit))
    caught = [o for o in out if isinstance(o, tuple)]
    r.counted(len(caught), "reps with the first swing caught", "%d of %d" % (len(caught), len(out)))
    r1 = out[0] if out else None
    ok1 = isinstance(r1, tuple)
    r.add(ok1 and r1[0] == ["window", "grace"], "rep 1: the second swing is caught by grace",
          "saw %s" % (r1[0] if ok1 else r1))
    r.add(ok1 and r1[1] == 1, "rep 1: the grace catch opens no second grace",
          "%s PARRY GRACE line(s)" % (r1[1] if ok1 else "-"))
    later = out[1:]
    r.add(bool(later) and all(isinstance(o, tuple) and o[0] == ["window"] and o[2] for o in later),
          "reps 2 on: the second swing lands past the grace",
          "saw %s" % [(o[0], "hit" if o[2] else "miss") if isinstance(o, tuple) else o for o in later])


def knockdown_floor_per_body(ctx, r, s):
    """The first heavy knocks the player down; the second, landing later each rep, deals nothing."""
    player, second = ctx.who("player"), ctx.who("defender")
    n, down, swung, reached, hit, rose, dists = 0, 0, 0, 0, 0, 0, []
    for seg in ctx.reps:
        n += 1
        if lines(seg, "KNOCKDOWN", player, "type="):
            down += 1
        if lines(seg, "ACTIVATE", second):
            swung += 1
        tgt = first(seg, "TARGET", second, "commit")
        d = fields(tgt[1]).get("dist") if tgt[1] else None
        dists.append(d)
        if d is not None and d <= 192.0:
            reached += 1
        if lines(seg, "DAMAGED", player, "by " + second):
            hit += 1
        rise = first(seg, "KNOCKDOWN RISE", player)
        if rise[1] and "by=auto" in rise[1]:
            rose += 1
    r.counted(n, "reps", "%d" % n)
    r.add(n > 0 and down == n, "the first heavy knocks the player down every rep", "%d of %d" % (down, n))
    r.add(n > 0 and swung == n, "the second heavy swings every rep", "%d of %d" % (swung, n))
    r.add(n > 1 and reached >= n - 1, "the second commits within reach of the downed player, rep 1 aside",
          "%d of %d; dists %s" % (reached, n, dists))
    r.add(hit == 0, "the downed player takes nothing from it", "%d DAMAGED by it" % hit)
    r.add(n > 0 and rose == n, "the player rises on its own each rep", "%d of %d" % (rose, n))


def parry_lockout(ctx, r, s):
    """Each caught swing locks the attacker out for its cell's ParryLockoutSeconds, as announced
    and as ended."""
    player, attacker = ctx.who("player"), ctx.who("attacker")
    n, ann_ok, end_ok, detail, cells = 0, 0, 0, [], []
    for seg in ctx.reps:
        catch = first(seg, "PARRY SUCCESS", player)
        if catch[0] is None:
            continue
        acts = [(t, x) for t, x in lines(seg, "ACTIVATE", attacker) if t <= catch[0]]
        coms = [(t, x) for t, x in lines(seg, "COMMIT", attacker) if t <= catch[0]]
        after = [(t, x) for t, x in seg if t >= catch[0] - 1e-6]
        lock = first(after, "PARRY LOCKOUT", attacker)
        end = first(after, "PARRY LOCKOUT END", attacker)
        if not acts or not coms or lock[0] is None or end[0] is None:
            continue
        p = int(re.search(r"swing=(\d+)", acts[-1][1]).group(1))
        m = re.search(r"branch (\d+)", coms[-1][1])
        b = int(m.group(1)) if m else 0
        cells.append("%d/%d" % (p, b))
        want = float(mirror("GA_Attack", "Positions[%d].Cells[%d].ParryLockoutSeconds" % (p, b)))
        n += 1
        announced = fields(lock[1]).get("until", -1) - lock[0]
        actual = end[0] - lock[0]
        if abs(announced - want) <= 0.001:
            ann_ok += 1
        else:
            detail.append("cell %d/%d announced %.3f vs %.3f" % (p, b, announced, want))
        if 0.0 <= actual - want <= 0.050 + 1e-6:
            end_ok += 1
        else:
            detail.append("cell %d/%d ended %+.3f" % (p, b, actual - want))
    r.counted(n, "caught swings with a lockout", "%d: cells %s" % (n, sorted(set(cells))))
    r.add(n > 0 and ann_ok == n, "the lockout announced is the cell's ParryLockoutSeconds",
          "%d of %d%s" % (ann_ok, n, "; " + "; ".join(detail[:3]) if detail else ""))
    r.add(n > 0 and end_ok == n, "the lockout ends 0 to +3 f after it", "%d of %d" % (end_ok, n))


def _displacement_pairs(ctx, s, start, end):
    """(held rows, control rows, start time, end time) per rep pair, from the player's tape."""
    player = ctx.who("player")
    tape = ctx.tape.get(player, [])
    out = []
    segs = ctx.reps
    for k in range(0, len(segs) - 1, 2):
        a, b = segs[k], segs[k + 1]
        sa, _ = first(a, start, player, "type=" if start == "KNOCKDOWN" else None)
        ea, _ = first(a, end, player)
        sb, _ = first(b, start, player, "type=" if start == "KNOCKDOWN" else None)
        eb, _ = first(b, end, player)
        if None in (sa, ea, sb, eb):
            continue
        pa = [q for q in tape if sa <= q["t"] <= ea + 0.2]
        pb = [q for q in tape if sb <= q["t"] <= eb + 0.2]
        if pa and pb:
            out.append((pa, pb, sa, ea, sb, eb))
    return out


def _dist(p, q):
    return ((p["x"] - q["x"]) ** 2 + (p["y"] - q["y"]) ** 2) ** 0.5


def _forward(p, q):
    """Travel from q to p along the facing q had."""
    yaw = math.radians(q["yaw"])
    return (p["x"] - q["x"]) * math.cos(yaw) + (p["y"] - q["y"]) * math.sin(yaw)


def _locked_pair(ctx, r, s):
    """Held move versus control through the state, then walking after it."""
    ex = s["expect"]
    pairs = _displacement_pairs(ctx, s, ex["start"], ex["end"])
    equal, walked, worst = 0, 0, 0.0
    for pa, pb, sa, ea, sb, eb in pairs:
        diff = 0.0
        for ra in pa:
            if ra["t"] >= ea - 1e-3:
                break
            off = ra["t"] - sa
            rb = min(pb, key=lambda q: abs((q["t"] - sb) - off))
            diff = max(diff, abs(_dist(ra, pa[0]) - _dist(rb, pb[0])))
        worst = max(worst, diff)
        if diff <= 2.0:
            equal += 1
        at_end = min(pa, key=lambda q: abs(q["t"] - ea))
        later = [q for q in pa if ea < q["t"] <= ea + 6 * FRAME + 1e-3]
        if later and max(_dist(q, at_end) for q in later) > 1.0:
            walked += 1
    n = len(pairs)
    r.counted(n, "held-versus-control pairs through %s" % ex["start"], "%d" % n)
    r.add(n > 0 and equal == n, "the held move displaces nothing through the state",
          "%d of %d within 2 cm of the control; worst %.1f cm" % (equal, n, worst))
    r.add(n > 0 and walked == n, "the held move walks within 6 f of %s" % ex["end"], "%d of %d" % (walked, n))


def _free_pair(ctx, r, s):
    """Held move versus control where movement must stay free: the held rep travels further."""
    ex = s["expect"]
    pairs = _displacement_pairs(ctx, s, ex["start"], ex["end"])
    freer, gains = 0, []
    for pa, pb, sa, ea, sb, eb in pairs:
        da = _forward(min(pa, key=lambda q: abs(q["t"] - ea)), pa[0])
        db = _forward(min(pb, key=lambda q: abs(q["t"] - eb)), pb[0])
        gains.append(round(da - db, 1))
        if da - db > 10.0:
            freer += 1
    n = len(pairs)
    r.counted(n, "held-versus-control pairs through %s" % ex["start"], "%d" % n)
    r.add(n > 0 and freer == n, "the held move travels further forward than the control by the stun's end",
          "%d of %d; gains %s cm" % (freer, n, gains))


def _still_then_walk(ctx, r, s):
    """A held move that must not show until the state ends, with no control needed."""
    ex = s["expect"]
    player = ctx.who("player")
    tape = ctx.tape.get(player, [])
    n, still, walked, worst = 0, 0, 0, 0.0
    for seg in ctx.reps:
        sa, _ = first(seg, ex["start"], player)
        ea, _ = first(seg, ex["end"], player)
        if sa is None or ea is None:
            continue
        rows = [q for q in tape if sa <= q["t"] <= ea + 0.2]
        if not rows:
            continue
        n += 1
        moved = max(_dist(q, rows[0]) for q in rows if q["t"] <= ea)
        worst = max(worst, moved)
        if moved <= 2.0:
            still += 1
        at_end = min(rows, key=lambda q: abs(q["t"] - ea))
        later = [q for q in rows if ea < q["t"] <= ea + 6 * FRAME + 1e-3]
        if later and max(_dist(q, at_end) for q in later) > 1.0:
            walked += 1
    r.counted(n, "reps through %s" % ex["start"], "%d" % n)
    r.add(n > 0 and still == n, "no displacement until %s" % ex["end"], "%d of %d; worst %.1f cm" % (still, n, worst))
    r.add(n > 0 and walked == n, "walks within 6 f of %s" % ex["end"], "%d of %d" % (walked, n))


def _speed(ctx, r, s):
    """Steady-state travel over frames 30 to 60 of a held move against a mirrored speed cap."""
    ex = s["expect"]
    player = ctx.who("player")
    tape = ctx.tape.get(player, [])
    want = float(mirror("BP_PlayerCharacter", ex["speed"])) * 0.5
    n, ok, seen = 0, 0, []
    for seg in ctx.reps:
        if not lines(seg, ex["tag"], player):
            continue
        t0 = seg[0][0] if seg else None
        press = [t for t, x in lines(seg, "INPUT", player, "pressed")]
        moves = [q for q in tape if seg[0][0] <= q["t"] <= seg[-1][0]] if seg else []
        if not moves:
            continue
        start = moves[0]["frame"]
        a = [q for q in moves if q["frame"] == start + 30]
        b = [q for q in moves if q["frame"] == start + 60]
        if not a or not b:
            continue
        n += 1
        d = _dist(b[0], a[0])
        seen.append(round(d, 1))
        if abs(d - want) <= 6.0:
            ok += 1
    r.counted(n, "steady-state windows", "%d" % n)
    r.add(n > 0 and ok == n, "travel over 30 f matches %s" % ex["speed"],
          "want %.1f cm, saw %s" % (want, seen))


def lock_hitstun(ctx, r, s):
    _locked_pair(ctx, r, s)


def lock_knockdown(ctx, r, s):
    _locked_pair(ctx, r, s)


def lock_attack_recovery(ctx, r, s):
    _locked_pair(ctx, r, s)


def lock_parry(ctx, r, s):
    _still_then_walk(ctx, r, s)


def lock_blockstun_free(ctx, r, s):
    _free_pair(ctx, r, s)


def lock_block_speed(ctx, r, s):
    _speed(ctx, r, s)


def lock_exhausted_speed(ctx, r, s):
    _speed(ctx, r, s)


# --- whole-slice rows -------------------------------------------------------------------------
# Rows read over the whole trace rather than per rep, with the bash checker's labels, bands and n=0
# rule kept as they were when it retired (2026-09-03). Authored values come from the mirror; the
# tolerances are the constants here.

TOL_RELEASE_MS = 30
ELAPSED_MIN, ELAPSED_MAX = -0.009, 0.020
TOL_BLOCKSTUN = 0.020
TOL_GUARDSTUN = 0.025
TOL_PARRY_SPAN = 0.025
PARRY_GAINED_MIN = 0
WAIVER_DODGE_MAX_MS = 100
DODGE_BAND_CM = (-5, 15)
DODGE_MIN_DURATION = 0.38
DODGE_LATERAL_MAX = 1.0
DODGE_COST = 50.0
STAMINA_TOL = 0.5
STRING_SWINGS = 3
CHAIN_GAP, CHAIN_GAP_TOL = 0.500, 0.045
CHAIN_LATENCY_MS = (125, 175)
HITSTUN_TOL = 0.020
SPACING_SLACK = 0.5
KD_SPAN_TOL = 0.025
EXHAUST_SPAN_TOL = 0.100
AIRBORNE_STAND_TOL = 1.0
AIRBORNE_MIN_HEIGHT = 20.0
KIPUP_TRAVEL_MAX = 25.0
BLOCK_GUARD_GAP = 0.100
REVIVE_TOL = 0.060
DEATH_SETTLE_CM = (300, 560)
ESCALATIONS = {0: (0, 0), 1: (1, 1), 2: (2, 1)}     # (ESCALATE, COIL START) per branch

_NUM = re.compile(r"[0-9]")


def _f(text, name):
    """A numeric field's string, as the line prints it."""
    m = re.search(r"\b%s=([-+]?[0-9.]+)" % re.escape(name), text)
    return m.group(1) if m else None


def _g(v):
    """awk's default number rendering."""
    return "%.6g" % float(v)


def _band(r, label, vals, lo, hi, unit=""):
    n = sum(1 for v in vals if _NUM.search(v))
    if n == 0:
        r.add(False, label, "no samples")
        return
    flo, fhi = float(lo), float(hi)
    frames = ""
    if unit == "s":
        frames = " (%.1f-%.1f f)" % (flo * 60, fhi * 60)
    elif unit == "ms":
        frames = " (%.1f-%.1f f)" % (flo * 0.06, fhi * 0.06)
    bad = [v for v in vals if float(v) < flo or float(v) > fhi]
    if bad:
        r.add(False, label, "n=%d outside [%s,%s]%s%s: %s" % (n, lo, hi, unit, frames, " ".join(bad)))
    else:
        r.add(True, label, "n=%d all within [%s,%s]%s%s" % (n, lo, hi, unit, frames))


def _equal(r, label, vals, want):
    n = sum(1 for v in vals if _NUM.search(v))
    if n == 0:
        r.add(False, label, "no samples")
        return
    bad = [v for v in vals if float(v) != float(want)]
    if bad:
        r.add(False, label, "n=%d expected %s, saw: %s" % (n, want, " ".join(bad)))
    else:
        r.add(True, label, "n=%d all exactly %s" % (n, want))


def _count(r, label, actual, expected):
    if actual == expected:
        r.add(True, label, "%d" % actual)
    else:
        r.add(False, label, "expected %d, got %d" % (expected, actual))


def _n(trace, prefix):
    return sum(1 for _, x in trace if x.startswith(prefix))


def _m(trace, pattern):
    pat = re.compile(pattern)
    return [(t, x) for t, x in trace if pat.search(x)]


def _plus(a, b):
    return "%.3f" % (float(a) + b)


# --- extractors ---------------------------------------------------------------------------------

def press_to_release(trace):
    out, p = [], None
    for t, x in trace:
        if x.startswith("INPUT      InputTag.Attack pressed"):
            p = t
        elif p is not None and x.startswith("RELEASE BEGIN"):
            out.append("%.0f" % ((t - p) * 1000))
            p = None
    return out


def elapsed_values(trace):
    return [_f(x, "elapsed") for _, x in trace if "elapsed=" in x and "(cancelled)" not in x]


def count_per_attack(trace, tag):
    out, started, n = [], False, 0
    for _, x in trace:
        if x.startswith("ACTIVATE"):
            started, n = True, 0
        elif started and "ABILITY END" in x:
            if "(cancelled)" not in x:
                out.append("%d" % n)
            started = False
        elif started and x.startswith(tag):
            n += 1
    return out


def stamina_damage_values(trace):
    return [m.group(1) for _, x in trace for m in re.finditer(r"staminaDamage=([0-9.]*)", x)]


def damaged_values(trace):
    return [_f(x, "damage") for _, x in trace if x.startswith("DAMAGED") and _f(x, "damage")]


def damaged_ledger_violations(trace):
    prev, out = {}, []
    for _, x in trace:
        p = x.split()
        if x.startswith("REVIVE"):
            prev[p[1]] = ""
        elif x.startswith("DAMAGED"):
            who, d, h = p[1], _f(x, "damage"), _f(x, "health")
            if prev.get(who, "") != "" and d and h:
                expected = max(float(prev[who]) - float(d), 0.0)
                if abs(expected - float(h)) > 0.01:
                    out.append("%s: %s->%s(damage %s)" % (who, prev[who], h, d))
            if h:
                prev[who] = h
    return out


def _until_spans(trace, prefix, pattern=None):
    out = []
    for t, x in trace:
        if (pattern and re.search(pattern, x)) or (not pattern and x.startswith(prefix)):
            u = _f(x, "until")
            if u:
                out.append("%.3f" % (float(u) - t))
    return out


def blockstun_spans(trace):
    return _until_spans(trace, "BLOCKSTUN  ")


def hitstun_spans(trace):
    return _until_spans(trace, "HITSTUN    ")


def guardstun_spans(trace):
    out, b = [], None
    for t, x in trace:
        if x.startswith("GUARD BREAK"):
            b = t
        elif b is not None and x.startswith("GUARD END"):
            out.append("%.3f" % (t - b))
            b = None
    return out


def swing_index_counts(trace):
    counts = {}
    for _, x in trace:
        m = re.search(r"ACTIVATE +[A-Za-z_0-9]+ +swing=([0-9]+)", x)
        if m:
            counts[m.group(1)] = counts.get(m.group(1), 0) + 1
    return counts


def chain_gaps(trace):
    out, prev = [], None
    for t, x in trace:
        if x.startswith("ACTIVATE"):
            s = _f(x, "swing") or "0"
            if int(float(s)) > 0 and prev is not None:
                out.append("%.3f" % (t - prev))
            prev = t
    return out


def chain_latency_ms(trace):
    out, r = [], None
    for t, x in trace:
        if x.startswith("RELEASE OFF"):
            r = t
        elif r is not None and x.startswith("ACTIVATE"):
            if int(float(_f(x, "swing") or "0")) > 0:
                out.append("%.0f" % ((t - r) * 1000))
            r = None
    return out


def knockback_inward_violations(trace):
    out = []
    for _, x in trace:
        if not x.startswith("KNOCKBACK"):
            continue
        m = re.search(r"spacing=(-?[0-9.]+) \(authored (-?[0-9.]+)\)", x)
        if m and float(m.group(1)) < float(m.group(2)) - SPACING_SLACK:
            out.append("%s<authored %s" % (m.group(1), m.group(2)))
    return out


def dodges_inside_hitstun(trace):
    n, inside = 0, False
    for _, x in trace:
        if x.startswith("HITSTUN    "):
            inside = True
        elif x.startswith("HITSTUN END"):
            inside = False
        elif inside and re.match(r"DODGE      [A-Za-z_0-9]+ +dir=", x):
            n += 1
    return n


def targets_per_window(trace):
    out, strung, sw, inwin, seen = [], 0, 0, False, set()
    for _, x in trace:
        if x.startswith("ACTIVATE"):
            sw = int(float(_f(x, "swing") or "0"))
            if sw == 0:
                strung += 1
        elif x.startswith("RELEASE BEGIN"):
            inwin, seen = True, set()
        elif x.startswith("RELEASE END"):
            if inwin:
                out.append((strung, sw, len(seen)))
            inwin = False
        elif inwin and x.startswith("DAMAGED"):
            seen.add(x.split()[1])
    return out


def kd_types(trace):
    return [m.group(1) for _, x in trace if x.startswith("KNOCKDOWN  ")
            for m in [re.search(r"type=([a-z]+)", x)] if m]


def kd_entry_to_rise_by(trace, want=""):
    out, down = [], {}
    for t, x in trace:
        p = x.split()
        if re.match(r"KNOCKDOWN  [^ ]+  type=", x):
            down[p[1]] = t
        elif x.startswith("KNOCKDOWN RISE"):
            reason = p[3][3:] if len(p) > 3 and p[3].startswith("by=") else (p[3] if len(p) > 3 else "")
            if p[2] in down:
                if want == "" or reason == want:
                    out.append("%.3f" % (t - down[p[2]]))
                del down[p[2]]
    return out


def kd_rise_to_stand(trace):
    out, rise = [], {}
    for t, x in trace:
        p = x.split()
        if x.startswith("KNOCKDOWN RISE"):
            rise[p[2]] = t
        elif x.startswith("KNOCKDOWN STAND") and p[2] in rise:
            out.append("%.3f" % (t - rise[p[2]]))
            del rise[p[2]]
    return out


def kd_rise_reasons(trace):
    return [m.group(1) for _, x in trace if x.startswith("KNOCKDOWN RISE")
            for m in [re.search(r"by=([a-z]+)", x)] if m]


def kd_damage_while_down(trace):
    n, down = 0, set()
    for _, x in trace:
        p = x.split()
        if re.match(r"KNOCKDOWN  [^ ]+  type=", x):
            down.add(p[1])
        elif x.startswith("KNOCKDOWN RISE"):
            down.discard(p[2])
        elif x.startswith("DAMAGED") and p[1] in down:
            n += 1
    return n


def kd_fall_overruns_lockout(trace):
    out, span = [], None
    for _, x in trace:
        if x.startswith("KNOCKDOWN MONTAGE") and " fall " in x:
            played, rate, frm = _f(x, "played"), _f(x, "rate"), _f(x, "from") or "0"
            span = (float(played) - float(frm)) / float(rate) if rate and float(rate) > 0 else None
        elif re.match(r"KNOCKDOWN  [^ ]+  type=", x):
            lock = _f(x, "lockout")
            if span is not None and lock and span >= float(lock):
                out.append("%.3fs montage >= %ss lockout" % (span, lock))
            span = None
    return out


def parry_window_spans(trace):
    return _until_spans(trace, "PARRY WINDOW open")


def parry_recovery_spans(trace):
    return _until_spans(trace, None, r"^PARRY RECOVERY [^E]")


def parry_grace_spans(trace):
    return _until_spans(trace, None, r"^PARRY GRACE [^E]")


def parry_lockout_spans(trace):
    return _until_spans(trace, "PARRY LOCKOUT  ")


def parry_success_gained(trace):
    return [m.group(1) for _, x in trace if x.startswith("PARRY SUCCESS")
            for m in [re.search(r"gained=([0-9.-]*)", x)] if m]


def parry_recoil_travels(trace):
    out = []
    for _, x in trace:
        if x.startswith("PARRY RECOIL  "):
            f, t = _f(x, "from"), _f(x, "to")
            if f and t:
                out.append("%.0f" % (float(t) - float(f)))
    return out


def _who_after_on(x):
    p = x.split()
    return p[p.index("on") + 1] if "on" in p and p.index("on") + 1 < len(p) else ""


def acts_during_parry_window(ctx):
    out, in_win, who = [], False, ""
    for raw, (t, x) in zip(ctx.trace_raw, ctx.trace):
        if x.startswith("PARRY WINDOW open"):
            who, in_win = _who_after_on(x), True
        elif re.match(r"(PARRY SUCCESS|PARRY WHIFF|HITSTUN|GUARD BREAK)", x):
            in_win = False
        elif in_win and who and re.search(r"ACTIVATE +%s +swing=" % re.escape(who), x):
            out.append(raw)
        elif in_win and who and re.search(r"BLOCK +cost .* on %s" % re.escape(who), x):
            out.append(raw)
        elif in_win and re.match(r"DODGE +[A-Za-z_0-9]+ +dir=", x):
            out.append(raw)
    return out


def acts_during_parry_recovery(ctx):
    out, in_rec, who, until_t = [], False, "", 0.0
    for raw, (t, x) in zip(ctx.trace_raw, ctx.trace):
        if re.match(r"PARRY RECOVERY [^E]", x):
            who, until_t, in_rec = x.split()[2], float(_f(x, "until") or 0), True
        elif x.startswith("PARRY RECOVERY END"):
            in_rec = False
        elif in_rec:
            if t >= until_t:
                continue
            if who and re.search(r"ACTIVATE +%s +swing=" % re.escape(who), x):
                out.append(raw)
            elif who and re.search(r"BLOCK +cost .* on %s" % re.escape(who), x):
                out.append(raw)
            elif re.match(r"DODGE +[A-Za-z_0-9]+ +dir=", x):
                out.append(raw)
            elif "PARRY WINDOW open on %s" % who in x:
                out.append(raw)
    return out


def gesture_outside_window(ctx):
    out, open_seen, open_t, close_t, who = [], False, 0.0, 0.0, ""
    for raw, (t, x) in zip(ctx.trace_raw, ctx.trace):
        if x.startswith("PARRY WINDOW open"):
            open_t, open_seen, who = t, True, _who_after_on(x)
            close_t = float(_f(x, "until") or 0)
        elif x.startswith("PARRY GESTURE"):
            p = x.split()
            if who == "" or len(p) < 3 or p[2] != who:
                continue
            if not open_seen or t < open_t or t > close_t + TOL_PARRY_SPAN:
                out.append(raw)
    return out


def grace_violations(ctx):
    out, last_by, seen_success, is_open = [], "", False, False
    for raw, (_, x) in zip(ctx.trace_raw, ctx.trace):
        if x.startswith("PARRY SUCCESS"):
            last_by, seen_success = ("window" if "by=window" in x else "grace"), True
        elif re.match(r"PARRY GRACE [^E]", x):
            if not seen_success:
                out.append("grace with no preceding success: " + raw)
            elif last_by != "window":
                out.append("grace started by a grace catch: " + raw)
            else:
                last_by = "consumed"
    for raw, (_, x) in zip(ctx.trace_raw, ctx.trace):
        if re.match(r"PARRY GRACE [^E]", x):
            if is_open:
                out.append("grace opened while one was running: " + raw)
            is_open = True
        elif x.startswith("PARRY GRACE END"):
            is_open = False
    return out


def parried_string_violations(trace):
    n, armed = 0, False
    for _, x in trace:
        if x.startswith("PARRY SUCCESS"):
            armed = True
        elif armed and x.startswith("STRING"):
            if "advance marked" in x:
                n += 1
            armed = False
    return n


def chains_after_first_parry(trace):
    n, seen = 0, False
    for _, x in trace:
        if x.startswith("PARRY SUCCESS"):
            seen = True
        elif seen and x.startswith("STRING     chain out of swing"):
            n += 1
    return n


def damage_during_parry_lockout(trace):
    n, locked = 0, set()
    for _, x in trace:
        p = x.split()
        if x.startswith("PARRY LOCKOUT  "):
            locked.add(p[2])
        elif x.startswith("PARRY LOCKOUT END"):
            locked.discard(p[3])
        elif x.startswith("DAMAGED"):
            n += sum(1 for a in locked if " by " + a in x)
    return n


def waiver_dodge_latency_ms(trace):
    out, hit = [], None
    for t, x in trace:
        if x.startswith("DAMAGED"):
            hit = t
        elif hit is not None and re.match(r"DODGE      [A-Za-z_0-9]+ +dir=", x):
            out.append("%.0f" % ((t - hit) * 1000))
            hit = None
    return out


def clean_dodge_distances(trace):
    out, start = [], None
    for t, x in trace:
        if re.match(r"DODGE      [A-Za-z_0-9]+ +dir=", x):
            start = t
        elif start is not None and x.startswith("DODGE END"):
            dur, start = t - start, None
            r, d = _f(x, "right"), None
            m = re.search(r"\bdist=([0-9.-]+)", x)
            if m:
                d = m.group(1)
            if dur >= DODGE_MIN_DURATION and r is not None and abs(float(r)) <= DODGE_LATERAL_MAX and d:
                out.append(d)
    return out


def dodge_fit_lengths(trace):
    out = []
    for _, x in trace:
        if re.match(r"DODGE      [A-Za-z_0-9]+ +dir=", x):
            sec, fit = sfield(x, "section"), _f(x, "fitLen")
            if sec and sec != "None" and fit:
                out.append(fit)
    return out


def dodge_from_full_remaining(trace):
    out, expect = [], True
    for _, x in trace:
        if x.startswith("EXHAUSTION END") or x.startswith("REVIVE"):
            expect = True
        elif re.match(r"DODGE      [A-Za-z_0-9]+ +dir=", x):
            if expect:
                rem = _f(x, "remaining")
                if rem:
                    out.append(rem)
                expect = False
    return out


def exhaust_stamina(trace, prefix):
    return [_f(x, "stamina") for _, x in trace if x.startswith(prefix) and _f(x, "stamina")]


def _regen_anchor(trace, i):
    """Where the exhausted regen starts counting from: the exhaustion itself, or the end of the
    action that spent the bar, since its pause runs from that end -- the guard's stun after a
    break, the dodge after a dodge."""
    t0, who = trace[i][0], trace[i][1].split()[1]
    for t, x in trace[i:]:
        if t - t0 > 0.05:
            break
        if x.startswith("GUARD BREAK") and who in x:
            return next((t2 for t2, x2 in trace[i:] if x2.startswith("GUARD END") and who in x2 and t2 >= t), t0), "break"
        if re.match(r"DODGE      [A-Za-z_0-9]+ +dir=", x) and who in x:
            return next((t2 for t2, x2 in trace[i:] if x2.startswith("DODGE END") and who in x2 and t2 >= t), t0), "dodge"
    return t0, "none"


def exhaust_spans_with_knockdown(trace, pause, regen, stun):
    out, start, is_open, brk, kd, extra = [], 0.0, False, False, 0, 0.0
    for i, (t, x) in enumerate(trace):
        if x.startswith("EXHAUSTED"):
            start, is_open, brk, kd = t, True, False, 0
            anchor, cause = _regen_anchor(trace, i)
            extra = anchor - t if cause == "dodge" else 0.0
        elif is_open and x.startswith("GUARD BREAK"):
            if t - start < 0.05:
                brk = True
        elif is_open and x.startswith("KNOCKDOWN") and " type=" in x:
            kd += 1
        elif is_open and x.startswith("EXHAUSTION END"):
            if kd > 0:
                out.append((t - start, pause + regen + (stun if brk else 0.0) + extra))
            is_open = False
    return out


def kd_airborne_pairs(trace):
    out, down = [], {}
    for _, x in trace:
        p = x.split()
        if x.startswith("KNOCKDOWN") and " type=" in x:
            if _f(x, "airborne") == "1":
                down[p[1]] = _f(x, "z")
        elif x.startswith("KNOCKDOWN STAND") and p[2] in down:
            out.append((down[p[2]], _f(x, "z")))
            del down[p[2]]
    return out


def kd_ground_stand_z(trace):
    out, down = [], set()
    for _, x in trace:
        p = x.split()
        if x.startswith("KNOCKDOWN") and " type=" in x:
            if _f(x, "airborne") == "0":
                down.add(p[1])
        elif x.startswith("KNOCKDOWN STAND") and p[2] in down:
            out.append(_f(x, "z"))
            down.discard(p[2])
    return out


def _deaths(trace):
    return [(t, x.split()[1]) for t, x in trace if x.startswith("DEATH ") and x.split()[1] != "SETTLE"]


def death_health(trace):
    out, dt = [], {}
    for t, x in trace:
        p = x.split()
        if x.startswith("DEATH ") and p[1] != "SETTLE":
            dt[p[1]] = t
        elif x.startswith("DAMAGED ") and p[1] in dt and t == dt[p[1]]:
            h = _f(x, "health")
            if h:
                out.append(_g(h))
            del dt[p[1]]
    return out


def death_revive_spans(trace):
    out, dead = [], {}
    for t, x in trace:
        p = x.split()
        if x.startswith("DEATH ") and p[1] != "SETTLE":
            dead[p[1]] = t
        elif x.startswith("REVIVE ") and p[1] in dead:
            out.append("%.3f" % (t - dead[p[1]]))
            del dead[p[1]]
    return out


def damage_while_dead(trace):
    n, dead = 0, {}
    for t, x in trace:
        p = x.split()
        if x.startswith("DEATH ") and p[1] != "SETTLE":
            dead[p[1]] = t
        elif x.startswith("REVIVE "):
            dead.pop(p[1], None)
        elif x.startswith("DAMAGED ") and p[1] in dead and t > dead[p[1]]:
            n += 1
    return n


def deaths_that_also_floored(trace):
    n, dt = 0, {}
    for t, x in trace:
        p = x.split()
        if x.startswith("DEATH ") and p[1] != "SETTLE":
            dt[p[1]] = t
        elif x.startswith("REVIVE "):
            dt.pop(p[1], None)
        elif re.match(r"KNOCKDOWN  [^ ]+  type=", x) and p[1] in dt and t == dt[p[1]]:
            n += 1
    return n


# --- shared assertions ------------------------------------------------------------------------------

def _ledger(r, trace):
    viol, dcount = damaged_ledger_violations(trace), len(damaged_values(trace))
    if viol:
        r.add(False, "health ledger steps by damage", ";".join(viol))
    else:
        r.add(True, "health ledger steps by damage", "n=%d, all consecutive steps exact" % dcount)


def _never_inward(r, trace):
    bad, n = knockback_inward_violations(trace), _n(trace, "KNOCKBACK")
    if n == 0:
        r.add(False, "knockback never pulls inward", "no KNOCKBACK lines at all -- nothing was asserted")
    elif not bad:
        r.add(True, "knockback never pulls inward", "n=%d, every spacing >= its authored value" % n)
    else:
        r.add(False, "knockback never pulls inward", " ".join(bad[:3]))


def _string_shape(r, trace):
    counts = swing_index_counts(trace)
    shown = " ".join("%d %s" % (counts[k], k) for k in sorted(counts))
    if len(counts) != STRING_SWINGS:
        r.add(False, "string is %d swings" % STRING_SWINGS,
              "saw %d distinct swing indices: %s" % (len(counts), shown))
        return
    uneven = max(counts.values()) - min(counts.values())
    if uneven <= 1:
        r.add(True, "string is %d swings" % STRING_SWINGS, shown)
    else:
        r.add(False, "string is %d swings" % STRING_SWINGS, "counts differ by %d: %s" % (uneven, shown))


def _hitstun_band(r, trace):
    want = float(mirror("GA_Attack", "Positions[0].Cells[0].HitstunSeconds"))
    _band(r, "HITSTUN span", hitstun_spans(trace), _plus(want, -HITSTUN_TOL), _plus(want, HITSTUN_TOL), "s")


def _exhaustion_ends_on_time(r, ctx, pawn_obj):
    """Each exhaustion ends pause + max/regen after the action that spent the bar released it: the
    guard's stun end after a break, the dodge's end after a dodge."""
    pause = float(mirror(pawn_obj, "StaminaRegenPauseSeconds"))
    regen = float(mirror(pawn_obj, "StartingMaxStamina")) / float(mirror(pawn_obj, "ExhaustedStaminaRegenPerSecond"))
    spans, anchor, cause = [], None, ""
    for i, (t, x) in enumerate(ctx.trace):
        if x.startswith("EXHAUSTED"):
            anchor, cause = _regen_anchor(ctx.trace, i)
        elif anchor is not None and x.startswith("EXHAUSTION END"):
            spans.append((round(t - anchor, 3), cause))
            anchor = None
    want = pause + regen
    bad = [s for s in spans if abs(s[0] - want) > EXHAUST_SPAN_TOL]
    r.add(bool(spans) and not bad, "exhaustion ends pause + regen after its cause releases the bar",
          "want %.3f; n=%d: %s" % (want, len(spans), ["%.3f after the %s" % s for s in spans]))


# --- the rows -----------------------------------------------------------------------------------------

def _tier(ctx, r, branch):
    rel = float(mirror("GA_Attack", "Branches[%d].ReleaseAtSeconds" % branch))
    ela = rel + float(mirror("GA_Attack", "Positions[0].Cells[%d].ReleaseSeconds" % branch)) \
        + float(mirror("GA_Attack", "Positions[0].Cells[%d].RecoverySeconds" % branch))
    rel_ms = int(round(rel * 1000))
    esc, coil = ESCALATIONS[branch]
    _band(r, "press->RELEASE BEGIN", press_to_release(ctx.trace),
          str(rel_ms - TOL_RELEASE_MS), str(rel_ms + TOL_RELEASE_MS), "ms")
    _band(r, "ABILITY END elapsed", elapsed_values(ctx.trace), _plus(ela, ELAPSED_MIN), _plus(ela, ELAPSED_MAX), "s")
    _equal(r, "ESCALATE per attack", count_per_attack(ctx.trace, "ESCALATE"), str(esc))
    _equal(r, "COIL START per attack", count_per_attack(ctx.trace, "COIL START"), str(coil))
    _count(r, "no unanswered inertialization request",
           sum(1 for l in ctx.raw if "No Inertialization node found" in l), 0)


def tier_light(ctx, r, s):
    _tier(ctx, r, 0)


def tier_heavy(ctx, r, s):
    _tier(ctx, r, 1)


def tier_charged(ctx, r, s):
    _tier(ctx, r, 2)


def _block(ctx, r, branch):
    trace = ctx.trace
    cell = "Positions[0].Cells[%d]." % branch
    _equal(r, "BLOCKED staminaDamage", stamina_damage_values(trace), mirror("GA_Attack", cell + "StaminaDamage"))
    _equal(r, "DAMAGED health damage", damaged_values(trace), mirror("GA_Attack", cell + "Damage"))
    _ledger(r, trace)
    _count(r, "BLOCK cost per BLOCK up", _n(trace, "BLOCK      cost"), _n(trace, "BLOCK      up"))
    breaks = _n(trace, "GUARD BREAK")
    zeros = sum(1 for _, x in trace if x.startswith("BLOCKED") and "remaining=0.0" in x)
    _count(r, "GUARD BREAK == blocks at 0", breaks, zeros)
    if breaks > 0:
        stun = float(mirror("BP_PlayerCharacter", "GuardBreakStunSeconds"))
        _band(r, "guard break stun", guardstun_spans(trace), _plus(stun, -TOL_GUARDSTUN), _plus(stun, TOL_GUARDSTUN), "s")
    if float(mirror("GA_Attack", cell + "StaminaDamage")) >= float(mirror("BP_PlayerCharacter", "StartingMaxStamina")):
        _count(r, "BLOCKSTUN never fires", _n(trace, "BLOCKSTUN  "), 0)
    else:
        bs = float(mirror("GA_Attack", cell + "BlockstunSeconds"))
        _band(r, "BLOCKSTUN span", blockstun_spans(trace), _plus(bs, -TOL_BLOCKSTUN), _plus(bs, TOL_BLOCKSTUN), "s")
    _exhaustion_ends_on_time(r, ctx, "BP_PlayerCharacter")


def block_light(ctx, r, s):
    _block(ctx, r, 0)


def block_heavy(ctx, r, s):
    _block(ctx, r, 1)


def block_charged(ctx, r, s):
    _block(ctx, r, 2)


def dodge_cycle(ctx, r, s):
    trace = ctx.trace
    starts = len(_m(trace, r"^DODGE      [A-Za-z_0-9]+ +dir="))
    _count(r, "DODGE/DODGE END paired", _n(trace, "DODGE END"), starts)
    if starts == 0:
        r.add(False, "no DODGE RECOVERY (gap retired)", "no dodges in log; absence proves nothing")
        r.add(False, "dodge fits the dash not the section", "no dodges in log; absence proves nothing")
    else:
        _count(r, "no DODGE RECOVERY (gap retired)", len(_m(trace, r"DODGE RECOVERY [^E]")), 0)
        _equal(r, "dodge fits the dash not the section", dodge_fit_lengths(trace), mirror("GA_Dodge", "DodgeClipSeconds"))
    clean = clean_dodge_distances(trace)
    dist = float(mirror("GA_Dodge", "DodgeTargetDistanceCm"))
    if not clean:
        r.add(False, "dodge travel (clean)", "no uncontaminated samples")
    else:
        _band(r, "dodge travel (clean)", clean, "%.0f" % (dist + DODGE_BAND_CM[0]), "%.0f" % (dist + DODGE_BAND_CM[1]), "cm")
    _equal(r, "dodge from full costs 50", dodge_from_full_remaining(trace), "%.1f" % DODGE_COST)
    exh_starts, exh_ends = _n(trace, "EXHAUSTED "), _n(trace, "EXHAUSTION END")
    expected = exh_starts
    if exh_starts == exh_ends + 1:
        last = [x for _, x in trace if x.startswith("EXHAUST")]
        if not last or "EXHAUSTION END" not in last[-1]:
            expected = exh_starts - 1
    _count(r, "EXHAUSTED/END paired", exh_ends, expected)
    mx = float(mirror("BP_PlayerCharacter", "StartingMaxStamina"))
    _band(r, "exhaustion enters at 0", exhaust_stamina(trace, "EXHAUSTED "), "%.2f" % -STAMINA_TOL, "%.2f" % STAMINA_TOL)
    _band(r, "exhaustion clears at Max", exhaust_stamina(trace, "EXHAUSTION END"), "%.2f" % (mx - STAMINA_TOL), "%.2f" % (mx + STAMINA_TOL))
    _exhaustion_ends_on_time(r, ctx, "BP_PlayerCharacter")


def string_cadence(ctx, r, s):
    trace = ctx.trace
    _string_shape(r, trace)
    _band(r, "chain gap (cadence)", chain_gaps(trace), _plus(CHAIN_GAP, -CHAIN_GAP_TOL), _plus(CHAIN_GAP, CHAIN_GAP_TOL), "s")
    _band(r, "chain latency", chain_latency_ms(trace), str(CHAIN_LATENCY_MS[0]), str(CHAIN_LATENCY_MS[1]), "ms")
    _equal(r, "DAMAGED health damage", damaged_values(trace), mirror("GA_Attack", "Positions[0].Cells[0].Damage"))
    _ledger(r, trace)
    _hitstun_band(r, trace)
    _never_inward(r, trace)


def string_guarantee(ctx, r, s):
    trace = ctx.trace
    _string_shape(r, trace)
    refused = sum(1 for _, x in trace if ": hitstun" in x)
    r.add(refused > 0, "REFUSED names hitstun", "%d refusals attributed to State.Hitstun" % refused)
    inside = dodges_inside_hitstun(trace)
    r.add(inside == 0, "zero dodges inside hitstun", "%d DODGE lines between HITSTUN and HITSTUN END" % inside)
    _hitstun_band(r, trace)
    player = ctx.who("player")
    n, full = 0, 0
    for seg in ctx.reps:
        if not lines(seg, "HITSTUN", player):
            continue
        n += 1
        if len(lines(seg, "DAMAGED", player)) >= 3:
            full += 1
    r.add(n > 0 and full == n, "every swing after the first lands on the stunned body",
          "%d of %d reps took all three hits" % (full, n))


def string_finisher_arc(ctx, r, s):
    rows = targets_per_window(ctx.trace)
    strings = max([a for a, _, _ in rows] or [0])
    r.add(strings > 1, "more than one string observed", "%d strings" % strings)

    def shown(vals):
        u = sorted(set(str(v) for v in vals))
        return (" ".join(u) + " ") if u else "none", " ".join(u)
    early = shown([n for a, sw, n in rows if sw < 2])
    first_late = shown([n for a, sw, n in rows if a == 1 and sw == 2])
    later_late = shown([n for a, sw, n in rows if a > 1 and sw == 2])
    r.add(early[1] == "0", "60-degree attacks reach neither, in every string",
          "attacks 1-2 damaged: %s distinct targets across %d strings" % (early[0], strings))
    r.add(first_late[1] == "2", "the 360 finisher reaches both in string 1",
          "string 1 attack 3 damaged: %s distinct targets" % first_late[0])
    r.add(later_late[1] == "1", "and exactly the re-homed body after that",
          "later strings' attack 3 damaged: %s distinct targets" % later_late[0])
    kdns = sum(1 for t in kd_types(ctx.trace) if t == "normal")
    r.add(kdns >= 2, "the finisher floors both bodies", "%d normal-type KNOCKDOWN lines" % kdns)


def input_hold_tier(ctx, r, s):
    def commits(branch):
        n = 0
        for _, x in ctx.trace:
            p = x.split()
            if len(p) > 3 and p[0] == "COMMIT" and "BP_PlayerCharacter" in p[1] and p[2] == "branch" and p[3] == str(branch):
                n += 1
        return n
    light, heavy = commits(0), commits(1)
    r.add(heavy > 0, "a hold across activation reaches the heavy", "%d heavy commits" % heavy)
    r.add(light <= heavy, "no held press flattened to a light", "%d light commits against %d heavy" % (light, heavy))


def attack_cancel(ctx, r, s):
    trace = ctx.trace
    _count(r, "cancelled swings never release", _n(trace, "RELEASE BEGIN"), 0)
    _count(r, "cancelled swings deal no damage", _n(trace, "DAMAGED"), 0)
    costs = _n(trace, "BLOCK      cost")
    r.add(costs > 0, "BLOCK cost per cancel", "%d" % costs if costs else "none -- no guard was raised, so nothing was cancelled")


def attack_waiver(ctx, r, s):
    trace = ctx.trace
    lat = waiver_dodge_latency_ms(trace)
    r.add(bool(lat), "attacker dodges out of its own hit",
          "%d" % len(lat) if lat else "none -- the commitment tag is still refusing")
    _band(r, "waiver dodge latency", lat, "0", str(WAIVER_DODGE_MAX_MS), "ms")
    unlocks = _n(trace, "MOVE UNLOCK")
    r.add(unlocks > 0, "MOVE UNLOCK observed", "%d" % unlocks if unlocks else "none -- movement never came back early")


def _parry_grace(r, ctx):
    trace = ctx.trace
    grace = float(mirror("BP_PlayerCharacter", "ParryGraceSeconds"))
    _band(r, "PARRY GRACE span", parry_grace_spans(trace), _plus(grace, -TOL_PARRY_SPAN), _plus(grace, TOL_PARRY_SPAN), "s")
    n = len(_m(trace, r"^PARRY GRACE [^E]"))
    wins = sum(1 for _, x in trace if x.startswith("PARRY SUCCESS") and "by=window" in x)
    if wins == 0:
        r.add(False, "every window catch starts Grace", "no by=window successes -- nothing was asserted")
    elif n == wins:
        r.add(True, "every window catch starts Grace", "n=%d tails for %d window catches" % (n, wins))
    else:
        r.add(False, "every window catch starts Grace", "%d tails for %d window catches" % (n, wins))
    bad = grace_violations(ctx)
    if n == 0:
        r.add(False, "Grace never re-arms", "no PARRY GRACE lines at all -- nothing was asserted")
    elif not bad:
        r.add(True, "Grace never re-arms", "n=%d tails, each from one window catch and none overlapping" % n)
    else:
        r.add(False, "Grace never re-arms", " ".join(bad[:3]))


def parry_catch(ctx, r, s):
    trace = ctx.trace
    win = float(mirror("GA_Parry", "ParryWindowSeconds"))
    _band(r, "PARRY WINDOW span", parry_window_spans(trace), _plus(win, -TOL_PARRY_SPAN), _plus(win, TOL_PARRY_SPAN), "s")
    successes = _n(trace, "PARRY SUCCESS")
    r.add(successes > 0, "PARRY SUCCESS observed",
          "%d" % successes if successes else "none -- the parry interval may be aliasing against the attacker's")
    _band(r, "parry reward within clamp", parry_success_gained(trace), str(PARRY_GAINED_MIN),
          mirror("BP_PlayerCharacter", "ParryStaminaReward"))
    bad = gesture_outside_window(ctx)
    n = len(_m(trace, r"^PARRY GESTURE BP_"))
    if n == 0:
        r.add(False, "parry gesture reads inside the window",
              "no PARRY GESTURE lines -- AM_Parry is unassigned, or carries no Parry Gesture marker")
    elif not bad:
        r.add(True, "parry gesture reads inside the window", "n=%d, every gesture inside its own window" % n)
    else:
        r.add(False, "parry gesture reads inside the window", " ".join(bad[:3]))
    _parry_grace(r, ctx)
    _count(r, "no STRING continuation after a parry", parried_string_violations(trace), 0)
    resumed = chains_after_first_parry(trace)
    r.add(resumed > 0, "chaining resumes after a parry", "%d chain-outs after the first PARRY SUCCESS" % resumed)
    _equal(r, "PARRY RECOIL travel", parry_recoil_travels(trace), mirror("GA_Attack", "ParryRecoilCm"))
    lock = float(mirror("GA_Attack", "Positions[0].Cells[0].ParryLockoutSeconds"))
    _band(r, "PARRY LOCKOUT span", parry_lockout_spans(trace), _plus(lock, -TOL_PARRY_SPAN), _plus(lock, TOL_PARRY_SPAN), "s")
    lockdmg = damage_during_parry_lockout(trace)
    r.add(lockdmg == 0, "no damage from a locked-out attacker", "%d DAMAGED dealt during a PARRY LOCKOUT" % lockdmg)


def parry_whiff(ctx, r, s):
    trace = ctx.trace
    rec = float(mirror("GA_Parry", "ParryWhiffRecoverySeconds"))
    _band(r, "PARRY RECOVERY span", parry_recovery_spans(trace), _plus(rec, -TOL_PARRY_SPAN), _plus(rec, TOL_PARRY_SPAN), "s")
    win_ref = sum(1 for _, x in trace if x.startswith("REFUSED") and ": parrying" in x)
    rec_ref = sum(1 for _, x in trace if x.startswith("REFUSED") and "parry recovery" in x)
    r.add(win_ref > 0, "REFUSED names the parry window", "%d" % win_ref if win_ref else "none -- the window refused nothing")
    r.add(rec_ref > 0, "REFUSED names parry recovery", "%d" % rec_ref if rec_ref else "none -- the recovery refused nothing")
    bad, n = acts_during_parry_recovery(ctx), len(parry_recovery_spans(trace))
    if n == 0:
        r.add(False, "nothing acts during parry recovery", "no PARRY RECOVERY spans at all -- nothing was asserted")
    elif not bad:
        r.add(True, "nothing acts during parry recovery", "n=%d spans, no ability started inside any of them" % n)
    else:
        r.add(False, "nothing acts during parry recovery", " ".join(bad[:3]))
    bad, n = acts_during_parry_window(ctx), _n(trace, "PARRY WINDOW open")
    if n == 0:
        r.add(False, "nothing acts during a parry window", "no PARRY WINDOW lines at all -- nothing was asserted")
    elif not bad:
        r.add(True, "nothing acts during a parry window", "n=%d windows, no ability started inside any of them" % n)
    else:
        r.add(False, "nothing acts during a parry window", " ".join(bad[:3]))


def _knockdown(ctx, r, want):
    trace = ctx.trace
    types = kd_types(trace)
    kdn = len(types)
    if kdn == 0:
        r.add(False, "KNOCKDOWN fires", "no KNOCKDOWN lines -- the swing's type is None, or nothing connected")
    else:
        wrong = sum(1 for t in types if t != want)
        if wrong == 0:
            r.add(True, "KNOCKDOWN type", "n=%d all type=%s" % (kdn, want))
        else:
            counts = {}
            for t in types:
                counts[t] = counts.get(t, 0) + 1
            r.add(False, "KNOCKDOWN type", "expected %s, saw: %s " % (want, " ".join("%d %s" % (counts[k], k) for k in sorted(counts))))
    rise = float(mirror("BP_PlayerCharacter", "KnockdownLockoutSecondsNormal")) + float(mirror("BP_PlayerCharacter", "KnockdownInputWindowSecondsNormal"))
    stand = float(mirror("BP_PlayerCharacter", "KnockdownRiseSeconds"))
    _band(r, "entry -> auto-rise", kd_entry_to_rise_by(trace), _plus(rise, -KD_SPAN_TOL), _plus(rise, KD_SPAN_TOL), "s")
    _band(r, "rise -> stand", kd_rise_to_stand(trace), _plus(stand, -KD_SPAN_TOL), _plus(stand, KD_SPAN_TOL), "s")
    overruns = kd_fall_overruns_lockout(trace)
    if kdn == 0:
        r.add(False, "fall lands inside lockout", "no knockdowns -- nothing to check")
    elif not overruns:
        r.add(True, "fall lands inside lockout", "n=%d, every fall inside its own lockout" % kdn)
    else:
        r.add(False, "fall lands inside lockout", ";".join(overruns))
    dmg = kd_damage_while_down(trace)
    if kdn == 0:
        r.add(False, "zero DAMAGED while down", "no knockdowns -- nothing was ever invincible to test")
    else:
        r.add(dmg == 0, "zero DAMAGED while down", "%d DAMAGED across %d knockdowns" % (dmg, kdn))


def knockdown_normal(ctx, r, s):
    _knockdown(ctx, r, "normal")
    reasons = kd_rise_reasons(ctx.trace)
    if not reasons:
        r.add(False, "every rise is by=auto", "no rises at all")
    else:
        wrong = sum(1 for x in reasons if x != "auto")
        r.add(wrong == 0, "every rise is by=auto", "n=%d rises, %d not auto" % (len(reasons), wrong))


def knockdown_hard(ctx, r, s):
    _knockdown(ctx, r, "hard")


def knockdown_getup_attack(ctx, r, s):
    trace = ctx.trace
    presses = len(_m(trace, r"DEBUG GETUP  .* mode=attack"))
    r.add(presses > 0, "fixture pressed the get-up attack", "%d presses" % presses)
    rises = sum(1 for x in kd_rise_reasons(trace) if x == "attack")
    r.add(rises > 0, "get-up attack fires as a get-up", "%d rises by=attack" % rises)
    lock_hard = float(mirror("BP_TrainingDummy", "KnockdownLockoutSecondsHard"))
    rise = lock_hard + float(mirror("BP_TrainingDummy", "KnockdownInputWindowSecondsHard"))
    _band(r, "rise inside the hard input window", kd_entry_to_rise_by(trace, "attack"), "%.3f" % lock_hard, _plus(rise, -KD_SPAN_TOL), "s")
    windup = float(mirror("GA_GetUpAttack", "WindupSeconds"))
    total = windup + float(mirror("GA_GetUpAttack", "ReleaseSeconds")) + float(mirror("GA_GetUpAttack", "RecoverySeconds"))
    out, p = [], None
    for t, x in trace:
        if re.match(r"DEBUG GETUP  .* mode=attack", x):
            p = t
        elif p is not None and x.startswith("RELEASE BEGIN"):
            out.append("%.0f" % ((t - p) * 1000))
            p = None
    ms = int(round(windup * 1000))
    _band(r, "press to RELEASE BEGIN", out, str(ms - TOL_RELEASE_MS), str(ms + TOL_RELEASE_MS), "ms")
    elapsed = [_f(x, "elapsed") for _, x in trace
               if re.match(r"ABILITY END  [A-Za-z_0-9]+ GA_GetUpAttack", x) and "(cancelled)" not in x and _f(x, "elapsed")]
    _band(r, "get-up attack total", elapsed, _plus(total, ELAPSED_MIN), _plus(total, ELAPSED_MAX), "s")
    risers = set(x.split()[2] for _, x in trace if x.startswith("DEBUG GETUP  "))
    damaged = sum(1 for _, x in trace if x.startswith("DAMAGED") for rr in risers if " by " + rr in x)
    r.add(damaged > 0, "riser's attack lands on the attacker", "%d DAMAGED by the riser" % damaged)
    rose = set(x.split()[2] for _, x in trace if x.startswith("KNOCKDOWN RISE  ") and "by=attack" in x)
    strings = sum(1 for _, x in trace if x.startswith("STRING") for rr in rose if rr in x)
    r.add(strings == 0, "no STRING line for the riser after its attack get-up", "%d STRING lines" % strings)


def knockdown_airborne(ctx, r, s):
    trace = ctx.trace
    pairs = kd_airborne_pairs(trace)
    n = len(pairs)
    r.add(n > 0, "airborne knockdowns observed", "%d knockdowns entered with airborne=1" % n)
    if n == 0:
        return
    grounded = kd_ground_stand_z(trace)
    if not grounded:
        r.add(False, "floor reference available", "no grounded knockdown in this run to measure it from")
        return
    floor_s = min(grounded, key=float)
    floor = float(floor_s)
    r.add(True, "floor reference available", "z=%s, the lowest of this run's grounded stands" % floor_s)
    high = sum(1 for e, _ in pairs if float(e) - floor >= AIRBORNE_MIN_HEIGHT)
    worst = max([0.0] + [float(e) - floor for e, _ in pairs])
    r.add(high > 0, "at least one floored at height",
          "%d of %d cleared %scm; highest was %.1fcm above the floor" % (high, n, AIRBORNE_MIN_HEIGHT, worst))
    hung = sum(1 for e, st in pairs if float(e) - floor >= AIRBORNE_MIN_HEIGHT
               and float(e) - float(st) < AIRBORNE_MIN_HEIGHT - AIRBORNE_STAND_TOL)
    if high == 0:
        r.add(False, "no airborne body hung", "no sample cleared %scm -- nothing had height to lose" % AIRBORNE_MIN_HEIGHT)
    else:
        r.add(hung == 0, "no airborne body hung", "%d of %d high samples failed to fall back to their own stand" % (hung, high))


def knockdown_regen_exception(ctx, r, s):
    obj = "BP_PlayerCharacter"
    pause = float(mirror(obj, "StaminaRegenPauseSeconds"))
    regen = float(mirror(obj, "StartingMaxStamina")) / float(mirror(obj, "ExhaustedStaminaRegenPerSecond"))
    stun = float(mirror(obj, "GuardBreakStunSeconds"))
    rows = exhaust_spans_with_knockdown(ctx.trace, pause, regen, stun)
    n = len(rows)
    r.add(n > 0, "exhaustions containing a knockdown", "%d spans with a knockdown inside" % n)
    if n == 0:
        return
    bad = sum(1 for a, b in rows if abs(a - b) > EXHAUST_SPAN_TOL)
    worst = max(abs(a - b) for a, b in rows)
    r.add(bad == 0, "a knockdown costs an exhausted player no recovery",
          "%d of %d spans off prediction by more than %.3fs; worst %.3fs" % (bad, n, EXHAUST_SPAN_TOL, worst))


def death_revive(ctx, r, s):
    trace = ctx.trace
    deaths = len(_deaths(trace))
    if deaths == 0:
        r.add(False, "DEATH fires", "no DEATH lines -- nothing died, so every assertion below is vacuous")
        return
    r.add(True, "DEATH fires", "%d deaths" % deaths)
    _equal(r, "death lands at exactly zero health", death_health(trace), "0.0")
    revives = _n(trace, "REVIVE ")
    r.add(deaths - revives <= 0, "every death revives", "%d deaths, %d revives" % (deaths, revives))
    delay = float(mirror("BP_PlayerCharacter", "DebugAutoReviveSeconds"))
    _band(r, "death -> revive", death_revive_spans(trace), _plus(delay, -REVIVE_TOL), _plus(delay, REVIVE_TOL), "s")
    settles = [_g(_f(x, "drift")) for _, x in trace if x.startswith("DEATH SETTLE") and _f(x, "drift")]
    _band(r, "death impulse carries the corpse", settles, str(DEATH_SETTLE_CM[0]), str(DEATH_SETTLE_CM[1]), "cm")
    dmg = damage_while_dead(trace)
    r.add(dmg == 0, "zero DAMAGED while dead", "%d DAMAGED across %d deaths" % (dmg, deaths))


def death_over_knockdown(ctx, r, s):
    trace = ctx.trace
    deaths, kdn = len(_deaths(trace)), _n(trace, "KNOCKDOWN ")
    if deaths == 0:
        r.add(False, "DEATH fires", "no DEATH lines")
        return
    if kdn == 0:
        r.add(False, "graded swings floor when they do not kill",
              "no KNOCKDOWN lines at all -- the fixture is not throwing a graded swing")
        return
    r.add(True, "graded swings floor when they do not kill", "%d knockdowns beside %d deaths" % (kdn, deaths))
    floored = deaths_that_also_floored(trace)
    r.add(floored == 0, "death suppresses the knockdown on its own contact",
          "%d of %d deaths still produced a KNOCKDOWN" % (floored, deaths))


ROWS = {
    "tier-light": tier_light,
    "tier-heavy": tier_heavy,
    "tier-charged": tier_charged,
    "block-light": block_light,
    "block-heavy": block_heavy,
    "block-charged": block_charged,
    "dodge-cycle": dodge_cycle,
    "string-cadence": string_cadence,
    "string-guarantee": string_guarantee,
    "string-finisher-arc": string_finisher_arc,
    "input-hold-tier": input_hold_tier,
    "attack-cancel": attack_cancel,
    "attack-waiver": attack_waiver,
    "parry-catch": parry_catch,
    "parry-whiff": parry_whiff,
    "knockdown-normal": knockdown_normal,
    "knockdown-hard": knockdown_hard,
    "knockdown-getup-attack": knockdown_getup_attack,
    "knockdown-airborne": knockdown_airborne,
    "knockdown-regen-exception": knockdown_regen_exception,
    "death-revive": death_revive,
    "death-over-knockdown": death_over_knockdown,
    "parry-grace-catch": parry_grace_catch,
    "knockdown-floor-per-body": knockdown_floor_per_body,
    "reach-arc": reach_probes,
    "reach-height": reach_probes,
    "reach-aim-wedge": reach_aim_wedge,
    "reach-aim-gap": reach_aim_gap,
    "edge-actionable": edge,
    "edge-hitstun-accept": edge,
    "edge-parry-close": edge,
    "edge-lockout-end": edge,
    "edge-recovery-accept": edge,
    "parry-lockout-light": parry_lockout,
    "parry-lockout-heavy": parry_lockout,
    "parry-lockout-charged": parry_lockout,
    "edge-heavy-checkpoint": edge,
    "edge-chain-open": edge,
    "edge-chain-close": edge,
    "edge-fresh-open": edge,
    "edge-guard-floor": edge,
    "lock-hitstun": lock_hitstun,
    "lock-knockdown": lock_knockdown,
    "lock-attack-recovery": lock_attack_recovery,
    "lock-parry": lock_parry,
    "lock-blockstun-free": lock_blockstun_free,
    "lock-block-speed": lock_block_speed,
    "lock-exhausted-speed": lock_exhausted_speed,
    "attack-airborne": attack_airborne,
    "dodge-airborne": dodge_airborne,
    "attack-whiff-commitment": attack_whiff_commitment,
    "block-facing": block_facing,
    "parry-facing": parry_facing,
    "parry-refused": parry_refused,
    "block-stun-offense-only": block_stun_offense_only,
    "block-commitment": block_commitment,
    "input-last-wins": input_last_wins,
    "input-parry-never-buffers": input_parry_never_buffers,
    "input-block-never-replays": input_block_never_replays,
    "death-midair": death_midair,
    "string-player-cadence": string_player_cadence,
    "string-player-blocked": string_player_blocked,
    "dodge-directions": dodge_directions,
    "dodge-iframes": dodge_iframes,
    "parry-reward": parry_reward,
    "tier-cells": tier_cells,
    "input-accept-hitstun": input_accept_hitstun,
    "input-accept-blockstun": input_accept_blockstun,
    "input-accept-lockout": input_accept_lockout,
    "knockdown-getup-exhausted-held": knockdown_getup_exhausted_held,
    "knockdown-getup-held": knockdown_getup_held,
    "knockdown-getup-held-normal": knockdown_getup_held_normal,
    "knockdown-getup-tap-priority": knockdown_getup_tap_priority,
    "edge-light-checkpoint": edge_light_checkpoint,
    "lock-guard-break": lock_guard_break,
    "reach-light": reach_light,
}


SELF_TEST_SLICE = """LogTDCombatTiming: [1.000] ACTIVATE   Fixture swing=0 pos=0.0000
LogTDCombatTiming: [1.000] INPUT      InputTag.Attack pressed on Fixture
LogTDCombatTiming: [1.200] RELEASE BEGIN  Fixture pos=0.3
LogTDCombatTiming: [1.769] ABILITY END  Fixture pos=0.0000 elapsed=0.769
LogTDCombatTiming: [2.000] DAMAGED    Defender by Fixture  damage=15  health=85.0
LogTDCombatTiming: [2.500] DAMAGED    Defender by Fixture  damage=15  health=70.0
"""


def self_test():
    """A correct band and a wrong one over the same slice: the instrument must pass the first and
    fail the second, for a timing and for a ledger value."""
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".slice.log")
    with os.fdopen(fd, "w") as fh:
        fh.write(SELF_TEST_SLICE)
    try:
        ctx = Context(path)
    finally:
        os.remove(path)
    outcomes = []
    for label, lo, hi in (("control (correct band)", "170", "230"), ("deliberately wrong band", "470", "530")):
        r = Result()
        _band(r, label, press_to_release(ctx.trace), lo, hi, "ms")
        outcomes.append(r.failed)
    for label, want in (("control (damaged=15)", "15"), ("deliberately wrong (damaged=25)", "25")):
        r = Result()
        _equal(r, label, damaged_values(ctx.trace), want)
        outcomes.append(r.failed)
    if outcomes == [0, 1, 0, 1]:
        print("  PASS  both control bands passed and both wrong bands FAILED.")
        return 0
    print("  BROKEN  timing %d/%d, damage %d/%d (want 0/1 and 0/1)." % tuple(outcomes))
    return 1


def main():
    if "--self-test" in sys.argv[1:]:
        return self_test()
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
