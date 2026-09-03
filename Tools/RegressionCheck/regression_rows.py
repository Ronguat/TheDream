"""Per-row evaluators for the scripted rows, and the vocabulary they are written in.

    regression_rows.py <id> <slice> [--tape <tape.tsv>]

Prints one PASS or FAIL line per assertion and a "N passed, M failed" summary; exit 1 on any FAIL.
A row is a function in ROWS taking a Context and a Result. The legacy rows stay in
regression-check.sh; a row is written here when it is new or is being changed for another reason.

Every count-shaped assertion fails on n=0: a row that examined nothing has proven nothing.
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
            # The held input was refused rather than ignored. Any reason: exhaustion shadows the
            # knockdown's own reason whenever it is up, and the absent rise is the fact asserted.
            want_refusals += 1
            if [t for t, _ in lines(seg, "REFUSED", player) if kd_t <= t <= rise_t]:
                refused_stand += 1
    r.counted(n, "knockdowns with a rise", "%d reps" % n)
    r.add(n > 0 and good == n, "rise by the expected option on the expected frame",
          "%d of %d; rise frames %s" % (good, n, rise_frames))
    r.add(held_n > 0 and held_ok == held_n, "rose on held names the winning input",
          "%d of %d" % (held_ok, held_n))
    if want_refusals:
        r.add(refused_stand == want_refusals, "held input refused where nothing may rise",
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
        elapsed = fields(end[1]).get("elapsed", -1)
        over = elapsed - total
        if p == 0:
            tot_first += 1
            if 0.0 <= over <= 0.050 + 1e-6:
                tot_first_ok += 1
            else:
                detail.append("cell %d/%d total %.3f vs %.3f" % (p, b, elapsed, total))
        else:
            tot_chain += 1
            if 0.0 <= over <= 0.050 + 1e-6:
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
    r.add(tot_first > 0 and tot_first_ok == tot_first, "position 1 totals: authored sum, 0 to +3 f",
          "%d of %d%s" % (tot_first_ok, tot_first, "; " + "; ".join(detail[:3]) if detail else ""))
    r.add(tot_chain > 0 and tot_chain_ok == tot_chain, "chained totals: authored sum, 0 to +3 f",
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
    fates = sorted(set(re.sub(r"\d+ms", "Nms", x.split("InputTag.Attack: ", 1)[-1])
                       for seg in ctx.reps for _, x in lines(seg, "BUFFER", player, "InputTag.Attack:")))
    r.add(True, "the buffer's handling of the airborne press: reported", "REPORT %s" % fates)


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
    r.counted(n, "probes with an outcome", "%d reps" % n)
    for v in range(len(labels)):
        outs = seen.get(v, [])
        label = "%s %s" % (probe, labels[v])
        if want[v] is None:
            r.add(True, label + ": reported", "REPORT %s" % outs)
        else:
            r.add(bool(outs) and all(_edge_match(o, want[v]) for o in outs),
                  label + " -> %s" % want[v], "saw %s" % outs)


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


ROWS = {
    "parry-grace-catch": parry_grace_catch,
    "knockdown-floor-per-body": knockdown_floor_per_body,
    "reach-arc": reach_probes,
    "reach-height": reach_probes,
    "reach-aim-wedge": reach_aim_wedge,
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
