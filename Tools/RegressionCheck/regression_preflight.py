"""Checks on the mirror and the sources that a run's result depends on, run before every run.

    regression_preflight.py

Relationships: values derived from each other where nothing in the code enforces the link, each
naming its source, so a failure says what to re-derive. Parity: the dummy carries the player's
values, or a fixture is not evidence about the player. Format lint: every LogTDCombatTiming call
names a pawn, first unless trace-exceptions.txt lists the tag. Exit 1 on any FAIL.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from regression_rows import mirror  # noqa: E402

EXCEPTIONS = os.path.join(HERE, "trace-exceptions.txt")
A, C, P = "GA_Attack", "BP_TrainingDummy", "BP_PlayerCharacter"
PARITY = ("StartingMaxHealth", "StartingMaxStamina", "StaminaRegenPerSecond", "StaminaRegenPauseSeconds",
          "ExhaustedStaminaRegenPerSecond", "ExhaustedMaxWalkSpeed", "BlockingMaxWalkSpeed",
          "GuardBreakStunSeconds", "TurnRateDegrees", "IdleTurnRateDegrees", "CoilTurnRateDegrees",
          "ForcedFacingTurnRateDegrees", "ParryGraceSeconds", "ParryStaminaReward",
          "KnockdownLockoutSecondsNormal", "KnockdownLockoutSecondsHard",
          "KnockdownInputWindowSecondsNormal", "KnockdownInputWindowSecondsHard",
          "KnockdownRiseSeconds", "KnockdownSpacingCm", "KnockdownFallSeconds")

ROWS = []


def row(status, label, detail):
    ROWS.append((status, label, detail))


def v(obj, prop):
    return float(mirror(obj, prop))


def relationships():
    h0 = v(A, "Branches[0].HoldUntilSeconds")
    r0, r1, r2 = (v(A, "Branches[%d].ReleaseAtSeconds" % i) for i in range(3))
    rl0, chain = v(A, "Positions[0].Cells[0].ReleaseSeconds"), v(A, "ChainOpenAfterRecoverySeconds")
    standoff, hitspacing = v(A, "LungeStandoffCm"), v(A, "HitSpacingCm")
    lungeb, lunge0 = v(A, "LungeDistanceCm"), v(A, "Positions[0].Cells[0].LungeDistanceCm")
    reach = v(A, "Positions[0].Cells[0].Hitboxes[0].MaxReachCm")
    checks = [
        ("turn rate covers the commit", abs(v(C, "TurnRateDegrees") - 180 / h0) < 1e-6,
         "TurnRateDegrees %g against 180/%g (spec, Facing)" % (v(C, "TurnRateDegrees"), h0)),
        ("hitstun outlasts the chain gap", v(A, "Positions[0].Cells[0].HitstunSeconds") > r0 + rl0 + chain,
         "hitstun %g > %g+%g+%g (spec, Hitstun)" % (v(A, "Positions[0].Cells[0].HitstunSeconds"), r0, rl0, chain)),
        ("light blockstun keeps the defender ahead", v(A, "Positions[0].Cells[0].BlockstunSeconds") > 0.5 + r0 - r1,
         "blockstun %g > 0.5+%g-%g: after a block the defender starts before the next chained hit lands"
         % (v(A, "Positions[0].Cells[0].BlockstunSeconds"), r0, r1)),
        ("the charged always breaks a full guard", v(A, "Positions[0].Cells[2].StaminaDamage") >= v(C, "StartingMaxStamina"),
         "charged StaminaDamage %g >= MaxStamina (trap)" % v(A, "Positions[0].Cells[2].StaminaDamage")),
        ("the parry window cannot cover two read-classes", v("GA_Parry", "ParryWindowSeconds") < r2 - r1,
         "window %g < %g-%g, the fast-to-charged gap" % (v("GA_Parry", "ParryWindowSeconds"), r2, r1)),
        ("a whiffed parry stays locked through the charged",
         v("GA_Parry", "ParryWindowSeconds") + v("GA_Parry", "ParryWhiffRecoverySeconds") >= r2,
         "window+recovery >= charged ReleaseAt %g" % r2),
        ("the standoff parks inside the hitbox", standoff < reach, "LungeStandoffCm %g < MaxReachCm %g (trap)" % (standoff, reach)),
        ("the string's connect inequality holds", hitspacing <= lungeb + lunge0 + reach - standoff,
         "HitSpacingCm %g <= %g+%g+%g-%g (trap)" % (hitspacing, lungeb, lunge0, reach, standoff)),
        ("both knockdown types total the same",
         abs((v(C, "KnockdownLockoutSecondsNormal") + v(C, "KnockdownInputWindowSecondsNormal"))
             - (v(C, "KnockdownLockoutSecondsHard") + v(C, "KnockdownInputWindowSecondsHard"))) < 1e-6,
         "normal and hard lockout+window (spec: type-invariant total)"),
    ]
    for label, ok, detail in checks:
        row("PASS" if ok else "FAIL", label, detail)
    covered = lungeb + lunge0 + reach - standoff
    if v(C, "KnockdownSpacingCm") > covered:
        row("WARN", "knockdown spacing is outside the covered range",
            "KnockdownSpacingCm %g against %gcm (deliberate)" % (v(C, "KnockdownSpacingCm"), covered))


def parity():
    bad = 0
    for name in PARITY:
        try:
            pv, dv = mirror(P, name), mirror(C, name)
        except KeyError as exc:
            row("FAIL", "parity %s" % name, "missing from the snapshot: %s" % exc)
            bad += 1
            continue
        if pv != dv:
            row("FAIL", "parity %s" % name, "player %s against dummy %s" % (pv, dv))
            bad += 1
    if not bad:
        row("PASS", "dummy parity", "%d properties equal on both characters" % len(PARITY))


def format_lint():
    skip = [l.split("#")[0].strip() for l in open(EXCEPTIONS) if l.split("#")[0].strip()]
    start = re.compile(r'TD_TIMING_LOG\(TEXT\("\[%\.3f\]|UE_LOG\(LogTDCombatTiming, *Log, *TEXT\("\[%\.3f\]')
    literal = re.compile(r'TEXT\("\[%\.3f\]([^"]*)"\)')
    out = []
    for path in glob.glob(os.path.join(ROOT, "Source", "TheDream", "**", "*.cpp"), recursive=True):
        lines = open(path, errors="replace").read().split("\n")
        i = 0
        while i < len(lines):
            if not start.search(lines[i]):
                i += 1
                continue
            buf, n, at = lines[i], 0, i
            while not re.search(r"\);\s*$", buf) and n < 30 and i + 1 < len(lines):
                i += 1
                buf += " " + lines[i]
                n += 1
            i += 1
            m = literal.search(buf)
            if not m:
                continue
            fmt, args = m.group(1), re.sub(r"^ *, *", "", buf[m.end():])
            tag = fmt.split("%")[0].strip()
            rel = os.path.relpath(path, ROOT).replace("\\", "/")
            if not re.search(r"GetName\(\)|GetNameSafe\(|GetAvatarActorFromActorInfo\(\)", args):
                out.append("%s:%d  %s names no pawn" % (rel, at + 1, tag or "<untagged>"))
            elif not any(tag.startswith(e) for e in skip) and not re.match(r"^[^%]*%s", fmt):
                out.append("%s:%d  %s does not name its pawn first, and is not in the exceptions" % (rel, at + 1, tag))
    if out:
        row("FAIL", "trace format lint", "%d offending call(s): %s" % (len(out), "; ".join(out[:3])))
    else:
        row("PASS", "trace format lint", "every LogTDCombatTiming call names a pawn")


def main():
    relationships()
    parity()
    format_lint()
    for status, label, detail in ROWS:
        print("  %-6s %-46s %s" % (status, label, detail))
    fails = sum(1 for s, _, _ in ROWS if s == "FAIL")
    print("  %d passed, %d failed, %d warned" % (sum(1 for s, _, _ in ROWS if s == "PASS"), fails,
                                                  sum(1 for s, _, _ in ROWS if s == "WARN")))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
