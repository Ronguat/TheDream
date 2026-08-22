"""Run a Python script or statement inside the open Unreal editor through the PythonScriptPlugin
remote channel, and print what it printed.

  ue_remote.py path/to/script.py [arg ...]    # the script sees sys.argv as given
  ue_remote.py -c "statement"

Exit 0 when the editor reports success, 1 on a Python failure, 2 when no editor answered within
UE_REMOTE_WAIT seconds (default 20).
"""
import os
import sys
import time

PLUGIN_PY = r"C:\Program Files (x86)\UE_5.8\Engine\Plugins\Experimental\PythonScriptPlugin\Content\Python"
sys.path.insert(0, PLUGIN_PY)
import remote_execution as remote  # noqa: E402


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    wait_s = float(os.environ.get("UE_REMOTE_WAIT", "20"))
    conn = remote.RemoteExecution()
    conn.start()
    try:
        t0 = time.time()
        while not conn.remote_nodes and time.time() - t0 < wait_s:
            time.sleep(0.25)
        if not conn.remote_nodes:
            print("ue_remote: no editor answered within %.0fs" % wait_s, file=sys.stderr)
            return 2
        conn.open_command_connection(conn.remote_nodes[0]["node_id"])
        if argv[1] == "-c":
            cmd, mode = argv[2], remote.MODE_EXEC_STATEMENT
        else:
            path = os.path.abspath(argv[1])
            conn.run_command("import sys; sys.argv = %r" % ([path] + argv[2:],),
                             exec_mode=remote.MODE_EXEC_STATEMENT)
            cmd, mode = path, remote.MODE_EXEC_FILE
        res = conn.run_command(cmd, exec_mode=mode, raise_on_failure=False)
        for line in res.get("output", []):
            print("[%s] %s" % (line.get("type", "?"), line.get("output", "").rstrip()))
        if res.get("result") not in (None, "", "None"):
            print(res["result"])
        return 0 if res.get("success") else 1
    finally:
        conn.stop()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
