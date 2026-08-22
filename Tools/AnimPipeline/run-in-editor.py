"""Run Python inside the open Unreal editor over Epic's remote-execution pipe.

    python run-in-editor.py <script.py> [--timeout S]
    python run-in-editor.py -c "<statement>"

Prints what the script printed, then RESULT/FAILED. Exit 0 on success, 1 on a script error,
2 when no editor answered the multicast discovery.
"""
import argparse, os, sys, time

ENGINE_PY = r"C:\Program Files (x86)\UE_5.8\Engine\Plugins\Experimental\PythonScriptPlugin\Content\Python"
sys.path.insert(0, ENGINE_PY)
import remote_execution as re_  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script", nargs="?")
    ap.add_argument("-c", dest="stmt")
    ap.add_argument("--timeout", type=float, default=10.0)
    a = ap.parse_args()
    if a.stmt:
        command, mode = a.stmt, re_.MODE_EXEC_STATEMENT
    elif a.script:
        command, mode = os.path.abspath(a.script).replace("\\", "/"), re_.MODE_EXEC_FILE
    else:
        ap.error("give a script path or -c")

    rex = re_.RemoteExecution()
    rex.start()
    try:
        deadline = time.time() + a.timeout
        while not rex.remote_nodes and time.time() < deadline:
            time.sleep(0.25)
        if not rex.remote_nodes:
            print("NO EDITOR: no remote-execution node answered", file=sys.stderr)
            return 2
        node = rex.remote_nodes[0]
        rex.open_command_connection(node["node_id"])
        r = rex.run_command(command, unattended=True, exec_mode=mode, raise_on_failure=False)
    finally:
        rex.stop()
    for line in r.get("output", []):
        print(f"[{line.get('type')}] {line.get('output')}".rstrip())
    if r.get("success"):
        print(f"RESULT {r.get('result')!r}")
        return 0
    print(f"FAILED {r.get('result')!r}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
