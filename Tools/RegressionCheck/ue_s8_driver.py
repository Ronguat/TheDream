"""Drives the s8 chain-window scenarios on the PIE player pawn.

Run through Tools/AnimPipeline/run-in-editor.py with PIE already up; args in ue_s8_driver.json:

    {"scen": "chain-early"|"chain-late"|"chain-closed"|"stale", "reps": 8, "period": 3.0}

Each rep injects one press pattern and then idles until the next, long enough that the string has
closed and every ability has ended. The pawn is parked away from every other actor so each swing
whiffs -- these scenarios assert the *input* window, and a landed hit would waive commitment and
reset the string, which is a different question.

The dummies' auto-attack is silenced for the run and restored after: their STRING and ABILITY END
lines carry no pawn name, so a second attacker makes the log unattributable.

Scenario timings are read off L1: windup 0.200, release to 0.350, recovery to 0.950, chain-out open
[0.483, 0.683], buffer 0.200.

  chain-early   tap 0.00, tap 0.35  -- press in the buffered slice, fires when chain-out opens
  chain-late    tap 0.00, tap 0.60  -- press inside the open span, fires on the press
  chain-closed  tap 0.00, tap 0.80  -- press past the close: no chain, a fresh swing 0 instead
  stale         hold 0.00-0.40, tap 0.50 -- a tap during a committed heavy must expire unfired
"""
import json, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
A = json.load(open(os.path.join(HERE, "ue_s8_driver.json")))
SCEN = A.get("scen", "chain-early")
REPS = int(A.get("reps", 8))
PERIOD = float(A.get("period", 3.0))

PLAN = {
    "chain-early":  [(0.00, "tap"), (0.35, "tap")],
    "chain-late":   [(0.00, "tap"), (0.60, "tap")],
    "chain-closed": [(0.00, "tap"), (0.80, "tap")],
    "stale":        [(0.00, "hold"), (0.40, "release"), (0.50, "tap")],
}[SCEN]

sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
gw = sub.get_game_world()
pc = unreal.GameplayStatics.get_player_controller(gw, 0)
pawn = pc.get_controlled_pawn()
IA = unreal.load_asset("/Game/TheDream/Combat/Input/IA_Attack.IA_Attack")

HOME = unreal.Vector(-4000.0, -4000.0, 100.0)
pawn.set_actor_location_and_rotation(HOME, unreal.Rotator(0.0, 0.0, 0.0), False, True)

silenced = []
for actor in unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.Actor.static_class()):
    try:
        if actor is pawn or not actor.get_editor_property("bDebugAutoAttack"):
            continue
        actor.set_editor_property("bDebugAutoAttack", False)
        silenced.append(actor)
    except Exception:
        continue

st = {"h": None, "t0": None, "rep": 0, "step": 0, "holding": False}


def restore():
    try:
        if st["holding"]:
            unreal.TDInputTools.stop_hold(pc, IA)
    except Exception:
        pass
    for actor in silenced:
        try:
            actor.set_editor_property("bDebugAutoAttack", True)
        except Exception:
            pass


def finish():
    if st["h"] is not None:
        unreal.unregister_slate_post_tick_callback(st["h"])
        st["h"] = None
    restore()
    unreal.log("S8 %s finished %d reps" % (SCEN, st["rep"]))


def on_tick(delta):
    try:
        now = unreal.GameplayStatics.get_time_seconds(gw)
        if st["t0"] is None:
            st["t0"] = now
        el = now - st["t0"]
        rep = int(el // PERIOD)
        if rep >= REPS:
            finish()
            return
        if rep != st["rep"]:
            st["rep"], st["step"] = rep, 0
        within = el - rep * PERIOD
        while st["step"] < len(PLAN) and within >= PLAN[st["step"]][0]:
            kind = PLAN[st["step"]][1]
            if kind == "tap":
                unreal.TDInputTools.inject_action(pc, IA, 1.0)
            elif kind == "hold":
                unreal.TDInputTools.start_hold(pc, IA, 1.0)
                st["holding"] = True
            elif kind == "release":
                unreal.TDInputTools.stop_hold(pc, IA)
                st["holding"] = False
            st["step"] += 1
    except Exception as exc:
        unreal.log_error("S8 driver: %s" % exc)
        finish()


st["h"] = unreal.register_slate_post_tick_callback(on_tick)
print("S8 armed %s, %d reps at %.2fs" % (SCEN, REPS, PERIOD))
print("DONE")
