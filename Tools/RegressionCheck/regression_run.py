"""Drives a regression run end to end: preflight, the editor-side runner, then evaluation.

Run through regression-run.sh, which is the documented entry point.

    regression-run.sh [--all | <id>... | --family <name>] [--realtime] [--repeat]
                      [--no-mutate] [--dry-run] [--run <id>]

It never drives PIE itself. ue_regression_runner.py does that from inside the editor; this waits
on the REGRESSION markers it emits, saves each scenario's slice, and evaluates it. A scenario whose
END has not arrived by 3x its duration + 60 s is abandoned and marked TIMEOUT.
"""
import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import scenarios as SC  # noqa: E402

PY = r"C:/Program Files (x86)/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
RUN_IN_EDITOR = os.path.join(ROOT, "Tools", "AnimPipeline", "run-in-editor.py")
RUNNER = os.path.join(HERE, "ue_regression_runner.py")
CHECKER = os.path.join(HERE, "regression-check.sh")
LOG = os.path.join(ROOT, "Saved", "Logs", "TheDream.log")
REG = os.path.join(ROOT, "Saved", "Regression")
STOP_FILE = os.path.join(REG, "stop")
DLL = os.path.join(ROOT, "Binaries", "Win64", "UnrealEditor-TheDream.dll")


def binary_stamp():
    """The editor module's modification time, the mark a resume must match."""
    try:
        return int(os.path.getmtime(DLL))
    except OSError:
        return 0

LOCK = os.path.join(REG, ".lock")


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, **kw)


def in_editor(statement):
    return sh([PY, RUN_IN_EDITOR, "-c", statement], timeout=60)


# --- preflight --------------------------------------------------------------

def preflight(args):
    """Everything that makes a run's result meaningless if wrong, checked before PIE."""
    problems = []
    print("  preflight")

    r = in_editor("import unreal; print('PIE', "
                  "unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor())")
    if "RESULT" not in r.stdout:
        problems.append("the editor did not answer -- is it open?")
    elif "PIE True" in r.stdout:
        problems.append("a play session is already running; stop it first")
    else:
        print("    editor answers, not in PIE")

    r = in_editor("import unreal; print('CV', "
                  "unreal.SystemLibrary.get_console_variable_int_value('TD.DebugCombatTiming'))")
    if "CV 1" not in r.stdout:
        problems.append("TD.DebugCombatTiming is not 1, so there is no trace to assert against")
    else:
        print("    TD.DebugCombatTiming 1")

    ini = os.path.join(ROOT, "Saved", "Config", "WindowsEditor", "EditorPerProjectUserSettings.ini")
    clients = 1
    if os.path.exists(ini):
        for line in open(ini, errors="ignore"):
            if line.startswith("PlayNumberOfClients="):
                clients = int(line.split("=")[1].strip() or 1)
    if clients != 1:
        problems.append("PlayNumberOfClients is %d; the checker assumes one world" % clients)
    else:
        print("    PlayNumberOfClients 1")

    # An open montage editor can hold a montage at a scrubbed position, which changes what plays.
    r = in_editor("import unreal; print('EDITED', [a.get_name() for a in "
                  "unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)"
                  ".get_all_edited_assets()])")
    for line in r.stdout.splitlines():
        if "EDITED" in line and line.strip() != "[Info] EDITED []":
            print("    WARNING, asset editors are open: %s" % line.split("EDITED", 1)[1].strip())

    for flag, label in ((["--self-test"], "self-test"), (["--bands-check"], "bands-check")):
        r = sh(["bash", CHECKER] + flag)
        if r.returncode != 0:
            problems.append("%s failed; run it directly to see why" % label)
        else:
            print("    %s green" % label)

    g = sh(["git", "status", "--short"])
    if g.stdout.strip():
        print("    working tree, at the run's start:")
        for line in g.stdout.splitlines():
            print("      " + line)
    else:
        print("    working tree clean")

    return problems


def take_lock():
    """A second run against the same editor would interleave two PIE sessions in one log."""
    try:
        os.makedirs(LOCK)
    except OSError:
        pid_file = os.path.join(LOCK, "pid")
        pid = open(pid_file).read().strip() if os.path.exists(pid_file) else "?"
        alive = sh(["tasklist", "/FI", "PID eq %s" % pid]).stdout
        if pid and pid in alive:
            print("  another run holds the lock (pid %s)" % pid)
            return False
        print("  reclaiming a lock left by dead pid %s" % pid)
    with open(os.path.join(LOCK, "pid"), "w") as fh:
        fh.write(str(os.getpid()))
    return True


def release_lock():
    try:
        os.remove(os.path.join(LOCK, "pid"))
        os.rmdir(LOCK)
    except OSError:
        pass


# --- following the run ------------------------------------------------------

def tail_new(path, offset):
    """Bytes appended since offset, and the new offset."""
    size = os.path.getsize(path)
    if size < offset:
        offset = 0
    with open(path, "rb") as fh:
        fh.seek(offset)
        data = fh.read()
    return data.decode("utf-8", "replace"), offset + len(data)


def save_slice(run_id, sid):
    """The raw log between this scenario's markers, kept beside its tape."""
    out_dir = os.path.join(REG, run_id)
    os.makedirs(out_dir, exist_ok=True)
    begin = "REGRESSION BEGIN %s run=%s " % (sid, run_id)
    end = "REGRESSION END %s " % sid
    keep, started = [], False
    for line in open(LOG, errors="ignore"):
        if not started and begin in line:
            started = True
        if started:
            keep.append(line)
            if end in line:
                break
    path = os.path.join(out_dir, "%s.slice.log" % sid)
    with open(path, "w", errors="replace") as fh:
        fh.writelines(keep)
    return path


EVAL = os.path.join(HERE, "regression_eval.py")
ROWS = os.path.join(HERE, "regression_rows.py")


def row_eval(sid, slice_path):
    """A scripted row's own assertions, from regression_rows.py, with its tape beside it."""
    tape = slice_path.replace(".slice.log", ".tape.tsv")
    return sh([PY, ROWS, sid, slice_path, "--tape", tape])


def evaluate(run_id, sid, slice_path, args):
    """The row's own assertions, the universal set, the golden diff, then its mutations.

    The row's assertions say the mechanic still behaves; the universal set says nothing beside it
    leaked; the golden diff says what moved whether or not anything asserts it; the mutations say
    the assertions could have failed.
    """
    s = SC.SCENARIOS[sid]
    out = dict(rc=0, detail="", universal="", golden="", mutations="")

    if s.get("legacy"):
        r = sh(["bash", CHECKER, s["legacy_id"], slice_path, "--slice", "%s:%s" % (run_id, sid)])
        tail = [ln for ln in r.stdout.splitlines() if "passed," in ln]
        out["rc"] = r.returncode
        out["detail"] = tail[-1].strip() if tail else (r.stdout or r.stderr).strip()[-120:]
    else:
        r = row_eval(sid, slice_path)
        tail = [ln for ln in r.stdout.splitlines() if "passed," in ln]
        out["rc"] = r.returncode
        out["detail"] = tail[-1].strip() if tail else (r.stdout or r.stderr).strip()[-120:]

    allow = ",".join(s.get("teardown_allow", []))
    u = sh([PY, EVAL, slice_path, "--universal", "--allow", allow])
    bad = [ln.strip() for ln in u.stdout.splitlines() if ln.strip().startswith("FAIL")]
    out["universal"] = "clean" if u.returncode == 0 else "; ".join(bad)
    if u.returncode != 0:
        out["rc"] = out["rc"] or 1

    excl = ",".join(s.get("golden", {}).get("exclude", []))
    g = sh([PY, EVAL, slice_path, "--golden", "--id", sid, "--exclude", excl]
           + (["--accept"] if args.accept_golden else []))
    out["golden"] = g.stdout.strip()
    if args.strict_golden and out["golden"].startswith("CHANGED"):
        out["rc"] = out["rc"] or 1

    if args.no_mutate or out["rc"] not in (0, None):
        out["mutations"] = "skipped"
    else:
        out["mutations"] = prove_mutations(run_id, sid, slice_path, s)
        if out["mutations"].startswith("UNPROVEN"):
            out["rc"] = 1
    return out


def prove_mutations(run_id, sid, slice_path, s):
    """Each mutation must turn the row red. One that does not means the assertions do not reach
    what the row claims to test -- a green nobody should trust."""
    proven, unproven = 0, []
    for mut in s.get("mutations", []):
        dst = slice_path.replace(".slice.log", ".mutated.log")
        spec = ":".join(str(x) for x in mut)
        m = sh([PY, EVAL, slice_path, "--mutate", spec, "--out", dst])
        if m.returncode != 0:
            unproven.append("%s (could not apply)" % spec)
            continue
        red = False
        if s.get("legacy"):
            r = sh(["bash", CHECKER, s["legacy_id"], dst, "--slice", "%s:%s" % (run_id, sid)])
            red = r.returncode != 0
        else:
            red = row_eval(sid, dst).returncode != 0
        if not red:
            red = sh([PY, EVAL, dst, "--universal",
                      "--allow", ",".join(s.get("teardown_allow", []))]).returncode != 0
        if red:
            proven += 1
        else:
            unproven.append(spec)
        try:
            os.remove(dst)
        except OSError:
            pass
    if unproven:
        return "UNPROVEN: " + "; ".join(unproven)
    return "%d proven" % proven


def append_history(run_id, results):
    """One row per scenario per run, so a band can be watched drifting toward an edge across
    runs rather than only failing once it crosses one."""
    path = os.path.join(REG, "history.tsv")
    cols = ("run", "stamp", "id", "status", "rc", "game", "frames",
            "detail", "universal", "golden", "mutations")
    tab, nl = chr(9), chr(10)
    fresh = not os.path.exists(path)
    with open(path, "a") as fh:
        if fresh:
            fh.write(tab.join(cols) + nl)
        for r in results:
            fh.write(tab.join(str(r.get(k, "")).replace(tab, " ") for k in cols) + nl)

def main():
    # A matrix takes tens of minutes, so it is normally backgrounded -- and a block-buffered
    # stdout shows nothing at all until the end, which is indistinguishable from a hang.
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except AttributeError:
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument("ids", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--family")
    ap.add_argument("--realtime", action="store_true")
    ap.add_argument("--repeat", action="store_true")
    ap.add_argument("--no-mutate", action="store_true")
    ap.add_argument("--accept-golden", action="store_true")
    ap.add_argument("--strict-golden", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--run", default=None)
    ap.add_argument("--stop", action="store_true",
                    help="ask the run in progress to finish its current row and end")
    ap.add_argument("--resume", default=None, metavar="RUN",
                    help="run the rows of RUN that have no slice yet, on the same binary")
    a = ap.parse_args()

    if a.stop:
        os.makedirs(REG, exist_ok=True)
        with open(STOP_FILE, "w") as fh:
            fh.write(time.strftime("%Y-%m-%d %H:%M:%S"))
        print("  stop requested; the run ends after its current row")
        return 0

    if a.resume:
        run_dir = os.path.join(REG, a.resume)
        try:
            with open(os.path.join(run_dir, "run.json")) as fh:
                saved = json.load(fh)
        except (OSError, ValueError):
            print("no resumable run at %s" % run_dir)
            return 2
        if saved.get("binary") != binary_stamp():
            print("  %s ran on a different binary; its rows are stale, start a new run" % a.resume)
            return 2
        a.realtime = not saved.get("fixed_step", True)
        ids = [i for i in saved["scenarios"]
               if not os.path.exists(os.path.join(run_dir, i + ".slice.log"))]
        if not ids:
            print("  %s has a slice for every row; nothing to resume" % a.resume)
            return 0
        print("  resuming %s: %d of %d row(s) remain" % (a.resume, len(ids), len(saved["scenarios"])))
        a.run = a.resume
        a.ids, a.all, a.family = ids, False, None

    if a.all:
        ids = sorted(SC.SCENARIOS)
    elif a.family:
        ids = SC.by_family(a.family)
    else:
        ids = a.ids
    if not ids:
        print("nothing selected; pass ids, --family <name> or --all")
        return 2
    unknown = [i for i in ids if i not in SC.SCENARIOS]
    if unknown:
        print("unknown scenario(s): %s" % ", ".join(unknown))
        return 2
    if a.realtime:
        framed = [i for i in ids if SC.SCENARIOS[i].get("plans") or SC.SCENARIOS[i].get("plan")]
        if framed:
            print("  real-time skips %d frame-authored row(s): %s" % (len(framed), ", ".join(framed)))
            ids = [i for i in ids if i not in framed]
        if not ids:
            print("nothing left to run on the wall clock")
            return 2

    shape = SC.validate()
    if shape:
        for p in shape:
            print("  scenarios.py: " + p)
        return 2

    run_id = a.run or time.strftime("%m%d-%H%M%S")
    print()
    print("  regression run %s: %d scenario(s), %s clock"
          % (run_id, len(ids), "real-time" if a.realtime else "fixed 1/60"))
    print()

    problems = preflight(a)
    if problems:
        print()
        for p in problems:
            print("  BLOCKED  " + p)
        return 2
    if a.dry_run:
        print("\n  dry run: %s" % ", ".join(ids))
        return 0
    if not take_lock():
        return 2

    try:
        return drive(run_id, ids, a)
    finally:
        release_lock()


def drive(run_id, ids, a):
    os.makedirs(REG, exist_ok=True)
    if os.path.exists(STOP_FILE):
        os.remove(STOP_FILE)
    cfg = dict(run=run_id, scenarios=ids, fixed_step=not a.realtime, dt=1.0 / 60.0,
               tapes=True, screen_percentage=50, binary=binary_stamp())
    with open(os.path.join(REG, "run.json"), "w") as fh:
        json.dump(cfg, fh)
    # The run's own copy keeps every row the run was asked for, so a resume knows what is left.
    run_dir = os.path.join(REG, run_id)
    os.makedirs(run_dir, exist_ok=True)
    earlier = []
    if os.path.exists(os.path.join(run_dir, "run.json")):
        with open(os.path.join(run_dir, "run.json")) as fh:
            earlier = json.load(fh).get("scenarios", [])
    with open(os.path.join(run_dir, "run.json"), "w") as fh:
        json.dump(dict(cfg, scenarios=sorted(set(earlier) | set(ids))), fh)

    offset = os.path.getsize(LOG)
    r = sh([PY, RUN_IN_EDITOR, RUNNER], timeout=120)
    if "ARMED" not in r.stdout:
        print("  the runner did not arm:")
        print(r.stdout[-800:] or r.stderr[-800:])
        return 2
    print("  armed; following the run")
    print()

    budget = {}
    for sid in ids:
        stop = SC.SCENARIOS[sid].get("stop", {})
        budget[sid] = 3 * float(stop.get("duration", stop.get("timeout", 60))) + 60.0

    results, seen, started_at = [], set(), time.time()
    pending = list(ids)
    deadline = started_at + sum(budget.values()) + 120.0
    buf = ""
    stopped = False
    while pending and time.time() < deadline and not stopped:
        time.sleep(2.0)
        chunk, offset = tail_new(LOG, offset)
        buf += chunk
        for line in buf.splitlines():
            if "REGRESSION DONE " in line and "status=stopped" in line:
                stopped = True
                continue
            if "REGRESSION END " not in line:
                continue
            sid = line.split("REGRESSION END ", 1)[1].split()[0]
            if sid in seen or sid not in pending:
                continue
            seen.add(sid)
            pending.remove(sid)
            status = "ok" if "status=ok" in line else "error"
            game = frames = "-"
            for tok in line.split():
                if tok.startswith("game="):
                    game = tok[5:]
                if tok.startswith("frames="):
                    frames = tok[7:]
            path = save_slice(run_id, sid)
            ev = (dict(rc=1, detail="runner reported an error", universal="-",
                       golden="-", mutations="-") if status != "ok"
                  else evaluate(run_id, sid, path, a))
            results.append(dict(run=run_id, stamp=time.strftime("%Y-%m-%d %H:%M:%S"),
                                id=sid, status=status, game=game, frames=frames, **ev))
            print("    %-22s %-6s %-22s univ=%-8s %s | %s"
                  % (sid, status, ev["detail"][:22], ev["universal"][:8],
                     ev["golden"][:26], ev["mutations"][:18]))
        buf = ""
    if stopped:
        if os.path.exists(STOP_FILE):
            os.remove(STOP_FILE)
        print("    stopped after the current row; %d row(s) not run" % len(pending))
        print("    resume with: regression-run.sh --resume %s" % run_id)
    else:
        for sid in pending:
            results.append(dict(run=run_id, stamp=time.strftime("%Y-%m-%d %H:%M:%S"),
                                id=sid, status="timeout", game="-", frames="-", rc=1,
                                detail="no END marker within %.0fs" % budget[sid],
                                universal="-", golden="-", mutations="-"))
            print("    %-22s TIMEOUT" % sid)

    append_history(run_id, results)
    report(run_id, results, time.time() - started_at)
    if stopped:
        return 3
    return 1 if any(r["status"] != "ok" or r["rc"] not in (0, None) for r in results) else 0


def report(run_id, results, wall):
    """The summary carries every row the run has produced across its sittings: rows from an earlier
    sitting stay unless this one ran them again."""
    out_dir = os.path.join(REG, run_id)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "summary.json")
    earlier, earlier_wall = [], 0.0
    if os.path.exists(path):
        try:
            with open(path) as fh:
                prev = json.load(fh)
            now_ids = set(r["id"] for r in results)
            earlier = [r for r in prev.get("scenarios", []) if r["id"] not in now_ids]
            earlier_wall = float(prev.get("wall_seconds", 0.0))
        except (OSError, ValueError, KeyError):
            earlier, earlier_wall = [], 0.0
    results = earlier + results
    wall += earlier_wall
    with open(path, "w") as fh:
        json.dump(dict(run=run_id, wall_seconds=round(wall, 1), scenarios=results), fh, indent=1)
    ok = sum(1 for r in results if r["status"] == "ok" and r["rc"] in (0, None))
    print()
    print("  %d of %d green, %.0fs wall" % (ok, len(results), wall))
    print("  %s" % os.path.join("Saved", "Regression", run_id, "summary.json"))
    print()


if __name__ == "__main__":
    sys.exit(main())
