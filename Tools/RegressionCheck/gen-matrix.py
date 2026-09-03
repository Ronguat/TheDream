"""Writes the scenario matrix in Docs/Debug-Instruments.md from scenarios.py.

    gen-matrix.py            rewrite the region in place
    gen-matrix.py --check    exit 1 if the region is stale, printing nothing else

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


def plan_summary(sid):
    plan = SC.SCENARIOS[sid].get("plan") or []
    if not plan:
        return "-"
    out = []
    for step in plan:
        frame, _actor, op = step[0], step[1], step[2]
        rest = " ".join(str(x) for x in step[3:])
        out.append("f%d %s %s" % (frame, op, rest))
    return "; ".join(out)


def render():
    lines = [BEGIN, ""]
    lines.append("| Scenario | Was | Attacker | Defender | Player plan | s |")
    lines.append("|---|---|---|---|---|---|")
    for fam in ORDER:
        for sid in SC.by_family(fam):
            s = SC.SCENARIOS[sid]
            lines.append("| `%s` | `%s` | %s | %s | %s | %.0f |" % (
                sid, s.get("legacy_id", "-"),
                knob_summary(sid, "attacker"), knob_summary(sid, "defender"),
                plan_summary(sid), s["stop"].get("duration", 0)))
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


def main():
    text = open(DOC, encoding="utf-8", newline="").read()
    if BEGIN not in text or END not in text:
        print("gen-matrix: no matrix region in %s" % DOC)
        return 2
    i = text.index(BEGIN)
    j = text.index(END) + len(END)
    fresh = render()
    if "--check" in sys.argv:
        return 0 if text[i:j] == fresh else 1
    open(DOC, "w", encoding="utf-8", newline="").write(text[:i] + fresh + text[j:])
    print("gen-matrix: %d scenarios written" % len(SC.SCENARIOS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
