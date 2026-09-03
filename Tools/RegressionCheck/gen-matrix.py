"""Writes the scenario matrix and the coverage map in Docs/Debug-Instruments.md from scenarios.py.

    gen-matrix.py            rewrite both regions in place
    gen-matrix.py --check    exit 1 if either region is stale, printing nothing else

The matrix used to be maintained by hand beside the fixtures it described, so the two drifted.
scenarios.py is the authority now (D5) and this renders it between the region markers; docs-check
runs --check so a fixture edit that forgets the doc fails there rather than misleading a reader.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import scenarios as SC  # noqa: E402

DOC = os.path.join(ROOT, "Docs", "Debug-Instruments.md")
BEGIN = "<!-- matrix:begin -->"
END = "<!-- matrix:end -->"
COV_BEGIN = "<!-- coverage:begin -->"
COV_END = "<!-- coverage:end -->"

# The order families appear in, per the convention of section 3 of the plan.
ORDER = ["tier", "attack", "string", "chain", "input", "block", "dodge", "parry",
         "knockdown", "death", "lock", "edge", "reach"]


def knob_summary(sid, role):
    """What this row changes from BASELINE for one role, which is what a reader needs."""
    over = SC.SCENARIOS[sid].get("knobs", {}).get(role, {})
    silent = all(over.get(k) == v for k, v in SC.SILENT.items()) if over else False
    parts = []
    for k in sorted(over):
        if silent and k in SC.SILENT:
            continue
        v = over[k]
        short = k.replace("debug_", "").replace("_seconds", "").replace("auto_attack_", "")
        parts.append("%s %s" % (short, v))
    if silent:
        parts.insert(0, "silent")
    return ", ".join(parts) or "baseline"


def step_text(step):
    frame, actor, op = step[0], step[1], step[2]
    rest = " ".join(str(x) for x in step[3:] if not isinstance(x, tuple))
    who = "" if actor == "player" else actor + " "
    return "f%d %s%s %s" % (frame, who, op, rest)


def plan_summary(sid):
    s = SC.SCENARIOS[sid]
    plans = s.get("plans") or ([s["plan"]] if s.get("plan") else [])
    if not plans:
        return "-"
    reps = s.get("expect", {}).get("reps")
    shown = " / ".join("; ".join(step_text(st) for st in p) for p in plans[:3])
    if len(plans) > 3:
        shown += " / +%d more" % (len(plans) - 3)
    return "%s (%s reps)" % (shown, reps) if reps else shown


def stop_summary(sid):
    stop = SC.SCENARIOS[sid]["stop"]
    if "until" in stop:
        u = stop["until"]
        return "%dx %s, or %.0f s" % (u[-1], " ".join(u[:-1]), stop["timeout"])
    if SC.SCENARIOS[sid].get("expect", {}).get("gate"):
        return "reps done, or %.0f s" % stop["duration"]
    return "%.0f s" % stop["duration"]


def render():
    lines = [BEGIN, ""]
    lines.append("| Scenario | Attacker | Defender | Plans | Stop | Canary |")
    lines.append("|---|---|---|---|---|---|")
    for fam in ORDER:
        for sid in SC.by_family(fam):
            s = SC.SCENARIOS[sid]
            lines.append("| `%s` | %s | %s | %s | %s | %s |" % (
                sid, knob_summary(sid, "attacker"), knob_summary(sid, "defender"),
                plan_summary(sid), stop_summary(sid), "yes" if s.get("canary") else ""))
    known = set()
    for fam in ORDER:
        known.update(SC.by_family(fam))
    missing = sorted(set(SC.SCENARIOS) - known)
    if missing:
        lines.append("")
        lines.append("Families not in the order list: %s" % ", ".join(missing))
    lines.append("")
    lines.append("*Generated from `Tools/RegressionCheck/scenarios.py` by "
                 "`Tools/RegressionCheck/gen-matrix.py`. Edit the fixtures there, never this table.*")
    lines.append(END)
    return "\n".join(lines)


def render_coverage():
    lines = [COV_BEGIN, ""]
    lines.append("| Mechanic | Rows asserting it (canary rows starred) |")
    lines.append("|---|---|")
    for mech in SC.MECHANICS:
        rows = sorted(sid for sid, s in SC.SCENARIOS.items() if mech in s.get("covers", []))
        shown = ", ".join("`%s`%s" % (sid, "*" if SC.SCENARIOS[sid].get("canary") else "") for sid in rows)
        lines.append("| %s | %s |" % (mech, shown or "**none**"))
    lines.append("")
    lines.append("*Generated from each row's `covers` in `Tools/RegressionCheck/scenarios.py` by "
                 "`Tools/RegressionCheck/gen-matrix.py`.*")
    lines.append(COV_END)
    return "\n".join(lines)


def main():
    text = open(DOC, encoding="utf-8", newline="").read()
    for b, e in ((BEGIN, END), (COV_BEGIN, COV_END)):
        if b not in text or e not in text:
            print("gen-matrix: no %s region in %s" % (b, DOC))
            return 2
    stale = False
    for b, e, fresh in ((BEGIN, END, render()), (COV_BEGIN, COV_END, render_coverage())):
        i = text.index(b)
        j = text.index(e) + len(e)
        if text[i:j] != fresh:
            stale = True
            text = text[:i] + fresh + text[j:]
    if "--check" in sys.argv:
        return 1 if stale else 0
    open(DOC, "w", encoding="utf-8", newline="").write(text)
    print("gen-matrix: %d scenarios written" % len(SC.SCENARIOS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
