"""Drives every scenario of a run inside the editor, from one run-in-editor.py call.

Reads Saved/Regression/run.json:

    {"run": "<id>", "scenarios": ["tier-light", ...], "fixed_step": true, "dt": 0.016666,
     "tapes": true, "screen_percentage": 50}

Arms a slate post-tick state machine and returns immediately; the orchestrator follows the run by
tailing the log for the REGRESSION markers each scenario emits:

    REGRESSION BEGIN <id> run=<run> idx=<n> game=<t> frame=<f>
    REGRESSION ROLES <id> player=<name> attacker=<name> defender=<name>
    REGRESSION INJECT <id> frame=<f> <action> <press|release>
    REGRESSION LOCK <id> rep=<k> <actor> <tag> frame=<f> game=<t>
    REGRESSION REP <id> n=<k> game=<t> <pawn> tags=<...> states=<...> health=<h> stamina=<s>
    REGRESSION TEARDOWN <pawn> tags=<...> states=<...> health=<h> stamina=<s>
    REGRESSION END <id> status=<ok|error|timeout> game=<s> frames=<n>
    REGRESSION DONE run=<run>

Two rep models. A row with expect.period_frames repeats its plan on a fixed period from plan start,
the s8 family's shape. A row with expect.gate runs one plan per rep -- plans[k % len(plans)] for rep
k -- and between reps waits for every pawn to settle, emits a REP readout, resets the pawns and
restores every placement, so each rep starts from the same state. Plan frames count from the rep's
start, or from the most recent lock_to, which rebases them to the frame its tag appeared on.

Anything thrown releases every hold, restores the clock and screen percentage, ends play, and the
run continues with the next scenario.
"""
import importlib
import json
import os
import time
import sys
import traceback
import types

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import scenarios as SC  # noqa: E402

# The editor keeps sys.modules between run-in-editor.py calls, so an edited scenarios.py would
# otherwise run at whatever revision the first call of the session imported -- an edit that appears
# to take and does not. Reload every time.
SC = importlib.reload(SC)

# Survives that reload, and holds the running callback so a later call can cancel it.
STATE = sys.modules.setdefault("_td_regression_state", types.ModuleType("_td_regression_state"))

PROJ = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REG_DIR = os.path.join(PROJ, "Saved", "Regression")
LOG_PATH = os.path.join(PROJ, "Saved", "Logs", "TheDream.log")

# Queried per pawn at every hygiene readout. The set is complete: State.* from
# TDGameplayTags.cpp's native defines plus Config/DefaultGameplayTags.ini.
STATE_TAGS = [
    "State.Attacking", "State.Attacking.Committed", "State.Blocking", "State.Blocking.Committed",
    "State.Blockstun", "State.Dead", "State.DodgeRecovery", "State.Dodging", "State.Exhausted",
    "State.GuardBroken", "State.Hitstun", "State.KnockedDown", "State.ParryLockout",
    "State.ParryRecovery", "State.Parrying", "State.StaminaRegenPaused",
]
# Reported beside the tags. IsKnockedDown, IsMovementLocked and IsFacingLocked are not reflected,
# so the tag set above carries knockdown and the union of these stands in for the locks.
STATE_GETTERS = [
    "is_dead", "is_exhausted", "is_in_hitstun", "is_in_blockstun", "is_guard_broken",
    "is_in_parry_lockout", "is_in_parry_recovery", "is_in_parry_grace", "is_in_dodge_recovery",
    "is_parry_window_open", "is_blocking",
]

SETTLE_TIMEOUT_S = 8.0
WORLD_TIMEOUT_S = 30.0
LOCK_TIMEOUT_S = 15.0
STOP_FILE = os.path.join(REG_DIR, "stop")
DEFAULT_TAIL_FRAMES = 60

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def tag(name):
    t = unreal.GameplayTag()
    t.import_text(name)
    return t


def key(name):
    k = unreal.Key()
    k.import_text(name)
    return k


def asc_of(pawn):
    return unreal.AbilitySystemLibrary.get_ability_system_component(pawn)


def has_tag(pawn, name):
    asc = asc_of(pawn)
    return bool(asc) and asc.has_matching_gameplay_tag(tag(name))


def tags_on(pawn):
    asc = asc_of(pawn)
    if not asc:
        return []
    return [n for n in STATE_TAGS if asc.has_matching_gameplay_tag(tag(n))]


def states_on(pawn):
    out = []
    for g in STATE_GETTERS:
        try:
            if getattr(pawn, g)():
                out.append(g[3:] if g.startswith("is_") else g)
        except Exception:
            pass
    return out


def at_rest(pawn):
    """The game's own settle. Every ability that matters applies a state tag while it runs --
    Attacking, Dodging, Parrying, Blocking -- so the tag set stands for "nothing active"."""
    return not tags_on(pawn) and not states_on(pawn)


def still(pawn):
    """Not being carried: a knockback's root motion outlives the hitstun tag, and a teleport made
    under it is finished by the carry rather than the teleport."""
    v = pawn.get_velocity()
    return abs(v.x) + abs(v.y) < 1.0


# --- key resolution ---------------------------------------------------------
# Actions are named in plans; the keys come from the mapping contexts, so a rebind moves the
# fixture with the game. IMC_Combat's four, IMC_Default's jump and the WASD quartet.
IMC_PATHS = ("/Game/TheDream/Combat/Input/IMC_Combat", "/Game/Input/IMC_Default")
_ACTION_KEYS = {}
_MOVE_KEYS = {}


def resolve_keys():
    _ACTION_KEYS.clear()
    _MOVE_KEYS.clear()
    for path in IMC_PATHS:
        imc = unreal.load_asset(path)
        if not imc:
            continue
        for m in imc.get_editor_property("default_key_mappings").get_editor_property("mappings"):
            action = m.get_editor_property("action")
            if not action:
                continue
            name = action.get_name()          # IA_Attack, IA_Move, ...
            kname = m.get_editor_property("key").export_text()
            if name == "IA_Move":
                if kname in ("W", "A", "S", "D"):
                    _MOVE_KEYS[kname] = kname
                continue
            short = name[3:].lower() if name.startswith("IA_") else name.lower()
            _ACTION_KEYS.setdefault(short, kname)
    return _ACTION_KEYS


def key_for(action):
    return _ACTION_KEYS.get(action)


def move_keys(x, y):
    """The WASD keys whose swizzled sum is the requested direction: X right, Y forward."""
    out = []
    if y > 0:
        out.append("W")
    elif y < 0:
        out.append("S")
    if x > 0:
        out.append("D")
    elif x < 0:
        out.append("A")
    return [k for k in out if k in _MOVE_KEYS]


# --- the run ----------------------------------------------------------------

class Run(object):
    def __init__(self, cfg):
        self.cfg = cfg
        self.run_id = cfg["run"]
        self.ids = list(cfg["scenarios"])
        self.dt = float(cfg.get("dt", 1.0 / 60.0))
        self.fixed = bool(cfg.get("fixed_step", True))
        self.tapes = bool(cfg.get("tapes", True))
        self.screen_pct = cfg.get("screen_percentage")
        self.out_dir = os.path.join(REG_DIR, self.run_id)
        os.makedirs(self.out_dir, exist_ok=True)

        self.idx = -1
        self.sid = None
        self.phase = "next"
        self.wait = 0
        self.phase_wall_t0 = time.time()
        self.phase_game_t0 = None
        self.frame = 0
        self.step = 0
        self.holds = []            # keys currently down
        self.tape = None
        self.pawn = None
        self.pc = None
        self.role_actors = {}
        self.pie_roles = {}
        self.pending = []
        self.begin_game_time = 0.0
        self.handle = None
        self.orig_screen_pct = None
        self.errors = 0
        # The gated rep model.
        self.rep = 0
        self.rep_frame = 0
        self.base = 0
        self.lock_seen = False
        self.lock_wait = 0
        self.last_due = 0

    # -- lifecycle ----------------------------------------------------------
    def mark(self, text):
        unreal.log("REGRESSION " + text)
        unreal.log_flush()

    def arm(self):
        resolve_keys()
        problems = SC.validate(known_actors=self.actor_names(), resolve_action=key_for)
        if problems:
            for p in problems:
                unreal.log_error("REGRESSION VALIDATE " + p)
            self.mark("DONE run=%s status=invalid" % self.run_id)
            return False
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
        return True

    def actor_names(self):
        return set(a.get_name() for a in eas.get_all_level_actors())

    def finish(self, stopped=False):
        self.release_all()
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
            self.handle = None
        try:
            if self.fixed:
                unreal.TDTimeTools.set_fixed_time_step(False)
            if self.orig_screen_pct is not None:
                unreal.SystemLibrary.execute_console_command(
                    None, "r.ScreenPercentage %d" % self.orig_screen_pct)
        except Exception:
            pass
        self.mark("DONE run=%s%s" % (self.run_id, " status=stopped" if stopped else ""))

    def release_all(self):
        for k in list(self.holds):
            try:
                unreal.TDInputTools.input_key(self.pc, key(k), False)
            except Exception:
                pass
        self.holds = []
        self.pending = []

    # -- the state machine --------------------------------------------------
    def tick(self, delta):
        try:
            getattr(self, "phase_" + self.phase)()
        except Exception as exc:
            unreal.log_error("REGRESSION ERROR %s: %s" % (self.sid, exc))
            unreal.log_error(traceback.format_exc())
            self.mark("END %s status=error msg=%s" % (self.sid, str(exc)[:120]))
            try:
                self.release_all()
                if self.tape:
                    self.tape.close()
                    self.tape = None
                if les.is_in_play_in_editor():
                    les.editor_request_end_play()
            except Exception:
                pass
            self.errors += 1
            if self.errors > len(self.ids) + 2:
                self.finish()
                return
            self.phase, self.wait = "aborting", 0

    def goto(self, phase):
        self.phase, self.wait = phase, 0
        # Phase budgets count in seconds of the clock they wait on: wall time before a game world
        # exists, game time once it does, since a tick is a sixtieth only under the fixed step.
        self.phase_wall_t0 = time.time()
        gw = ues.get_game_world()
        self.phase_game_t0 = unreal.GameplayStatics.get_time_seconds(gw) if gw else None

    def settled_for(self):
        gw = ues.get_game_world()
        if not gw or self.phase_game_t0 is None:
            return time.time() - self.phase_wall_t0
        return unreal.GameplayStatics.get_time_seconds(gw) - self.phase_game_t0

    def phase_aborting(self):
        self.wait += 1
        if les.is_in_play_in_editor():
            if self.wait % 120 == 1:
                les.editor_request_end_play()
            return
        self.goto("next")

    def phase_next(self):
        self.idx += 1
        if self.idx >= len(self.ids):
            self.finish()
            return
        if os.path.exists(STOP_FILE):
            # Asked to stop between rows: the row just run is complete, the rest are not started.
            self.finish(stopped=True)
            return
        self.sid = self.ids[self.idx]
        self.goto("stopping")

    def phase_stopping(self):
        self.wait += 1
        if les.is_in_play_in_editor():
            if self.wait % 120 == 1:
                les.editor_request_end_play()
            return
        self.goto("apply")

    def phase_apply(self):
        s = SC.SCENARIOS[self.sid]
        self.role_actors = {}
        by_name = dict((a.get_name(), a) for a in eas.get_all_level_actors())
        for role, (name, loc, yaw) in s["roles"].items():
            actor = by_name.get(name)
            if actor is None:
                raise RuntimeError("role %s: no actor %s in the editor world" % (role, name))
            if "UEDPIE" in actor.get_path_name():
                raise RuntimeError("role %s resolved into a PIE world" % role)
            self.role_actors[role] = actor
            actor.set_actor_location_and_rotation(
                unreal.Vector(*loc), unreal.Rotator(0.0, 0.0, yaw), False, True)
            self.write_knobs(actor, SC.knobs_for(self.sid, role))

        if self.fixed:
            unreal.TDTimeTools.set_fixed_time_step(True, self.dt)
        if self.screen_pct and self.orig_screen_pct is None:
            self.orig_screen_pct = unreal.SystemLibrary.get_console_variable_int_value(
                "r.ScreenPercentage") or 100
            unreal.SystemLibrary.execute_console_command(
                None, "r.ScreenPercentage %d" % int(self.screen_pct))
        les.editor_request_begin_play()
        self.goto("wait_world")

    def coerce(self, name, value):
        if name in SC.ENUM_KNOBS:
            return getattr(getattr(unreal, SC.ENUM_KNOBS[name]), value)
        if name in SC.TAG_KNOBS:
            return tag(value) if value else unreal.GameplayTag()
        return value

    def write_knobs(self, actor, knobs):
        for name, value in knobs.items():
            actor.set_editor_property(name, self.coerce(name, value))

    def phase_wait_world(self):
        self.wait += 1
        if time.time() - self.phase_wall_t0 > WORLD_TIMEOUT_S and not les.is_in_play_in_editor():
            raise RuntimeError("PIE did not start")
        if not les.is_in_play_in_editor():
            return
        gw = ues.get_game_world()
        pc = unreal.GameplayStatics.get_player_controller(gw, 0) if gw else None
        pawn = pc.get_controlled_pawn() if pc else None
        if pawn is None:
            if time.time() - self.phase_wall_t0 > WORLD_TIMEOUT_S:
                raise RuntimeError("no player pawn after %.0fs" % WORLD_TIMEOUT_S)
            return
        self.pc, self.pawn = pc, pawn
        self.goto("setup")

    def place_player(self):
        p = SC.SCENARIOS[self.sid]["player"]
        yaw = p.get("yaw", 0.0)
        self.pawn.set_actor_location_and_rotation(
            unreal.Vector(*p["spawn"]), unreal.Rotator(0.0, 0.0, yaw), False, True)
        self.pc.set_control_rotation(unreal.Rotator(0.0, 0.0, yaw))
        for k, v in (p.get("props") or {}).items():
            self.pawn.set_editor_property(k, self.coerce(k, v))

    def place_roles(self):
        s = SC.SCENARIOS[self.sid]
        attackers = set(self.periodic_attackers())
        for role, (_name, loc, yaw) in s["roles"].items():
            actor = self.pie_roles.get(role)
            if actor is None or (role in attackers and not at_rest(actor)):
                continue
            actor.set_actor_location_and_rotation(
                unreal.Vector(*loc), unreal.Rotator(0.0, 0.0, yaw), False, True)

    def phase_setup(self):
        s = SC.SCENARIOS[self.sid]
        self.place_player()

        gw = ues.get_game_world()
        self.begin_game_time = unreal.GameplayStatics.get_time_seconds(gw)
        self.pie_roles = self.find_pie_roles()
        names = self.pie_roles
        self.mark("BEGIN %s run=%s idx=%d game=%.3f frame=0"
                  % (self.sid, self.run_id, self.idx, self.begin_game_time))
        self.mark("ROLES %s player=%s %s"
                  % (self.sid, self.pawn.get_name(),
                     " ".join("%s=%s" % (r, a.get_name()) for r, a in sorted(names.items()))))
        if self.tapes:
            self.tape = open(os.path.join(self.out_dir, "%s.tape.tsv" % self.sid), "w")
            self.tape.write("frame\tt\tpawn\tx\ty\tz\tyaw\thealth\tstamina\ttags\n")
        self.frame, self.step, self.pending = 0, 0, []
        self.rep, self.rep_frame, self.base = 0, 0, 0
        self.lock_seen, self.lock_wait, self.last_due = False, 0, 0
        self.goto("run")

    def find_pie_roles(self):
        """The PIE-world counterparts of the role actors, resolved once per scenario."""
        gw = ues.get_game_world()
        out = {}
        actors = unreal.GameplayStatics.get_all_actors_of_class(gw, unreal.Actor)
        s = SC.SCENARIOS[self.sid]
        for role, (name, _loc, _yaw) in s["roles"].items():
            for a in actors:
                if a.get_name() == name:
                    out[role] = a
                    break
        return out

    # -- running a scenario ---------------------------------------------------
    def plans(self):
        s = SC.SCENARIOS[self.sid]
        return s.get("plans") or [s.get("plan") or []]

    def gated(self):
        return bool(SC.SCENARIOS[self.sid].get("expect", {}).get("gate"))

    def phase_run(self):
        s = SC.SCENARIOS[self.sid]
        gw = ues.get_game_world()
        now = unreal.GameplayStatics.get_time_seconds(gw)
        elapsed = now - self.begin_game_time
        f = self.frame

        if self.gated():
            done = self.run_gated_plan(now)
            if done:
                return
        else:
            plan = self.plans()[0]
            period = s.get("expect", {}).get("period_frames")
            if period:
                rep, within = divmod(f, period)
                if rep >= s.get("expect", {}).get("reps", 1):
                    self.goto("settle")
                    return
                if within == 0:
                    self.step = 0
            else:
                within = f
            while self.step < len(plan) and plan[self.step][0] <= within:
                self.do_op(plan[self.step])
                self.step += 1
        self.expire_holds(f)

        every = int(s.get("tape_every", 2))
        if self.tapes and self.tape and f % every == 0:
            self.sample(f, now)

        stop = s.get("stop", {})
        if "duration" in stop and elapsed >= stop["duration"]:
            self.goto("settle")
            return
        self.frame += 1
        self.rep_frame += 1

    def run_gated_plan(self, now):
        """One rep of the current plan; True when the phase changed."""
        s = SC.SCENARIOS[self.sid]
        expect = s.get("expect", {})
        plans = self.plans()
        plan = plans[self.rep % len(plans)]
        rel = self.rep_frame - self.base
        while self.step < len(plan):
            stepv = plan[self.step]
            if stepv[2] == "lock_to":
                who = self.role_or_player(stepv[1])
                present = has_tag(who, stepv[3]) if who is not None else False
                if self.lock_wait == 0:
                    # First look: record what is already up, and lock only on a later edge.
                    self.lock_seen = present
                    self.lock_wait = 1
                    return False
                if present and not self.lock_seen:
                    # Rebase to the frame the tag appeared on, and read the steps after it against it.
                    self.base = self.rep_frame
                    self.last_due = self.rep_frame
                    self.mark("LOCK %s rep=%d %s %s frame=%d game=%.3f"
                              % (self.sid, self.rep, stepv[1], stepv[3], self.frame, now))
                    self.step += 1
                    self.lock_seen = False
                    self.lock_wait = 0
                    rel = 0
                    continue
                self.lock_seen = present
                self.lock_wait += 1
                if self.lock_wait * self.dt > LOCK_TIMEOUT_S:
                    raise RuntimeError("rep %d: %s never showed %s" % (self.rep, stepv[1], stepv[3]))
                return False
            if stepv[0] <= rel:
                self.do_op(stepv)
                self.last_due = self.rep_frame
                self.step += 1
                continue
            break
        if self.step >= len(plan):
            tail = int(expect.get("tail_frames", DEFAULT_TAIL_FRAMES))
            if self.rep_frame - self.last_due >= tail and not self.pending:
                self.goto("gate")
                return True
        return False

    def do_op(self, stepv):
        _frame, actor, op = stepv[0], stepv[1], stepv[2]
        if op in ("tap", "press", "release", "hold"):
            action = stepv[3]
            kname = key_for(action)
            if op in ("tap", "press", "hold"):
                unreal.TDInputTools.input_key(self.pc, key(kname), True)
                self.mark("INJECT %s frame=%d %s press" % (self.sid, self.frame, action))
                self.holds.append(kname)
                if op == "tap":
                    self.pending.append((self.frame + 2, kname, action))
                elif op == "hold":
                    self.pending.append((self.frame + int(stepv[4]), kname, action))
            else:
                self.up(kname, action)
        elif op == "move":
            x, y = float(stepv[3]), float(stepv[4])
            frames = int(stepv[5]) if len(stepv) > 5 else 0
            for kname in move_keys(x, y):
                if kname not in self.holds:
                    unreal.TDInputTools.input_key(self.pc, key(kname), True)
                    self.holds.append(kname)
                    self.mark("INJECT %s frame=%d move-%s press" % (self.sid, self.frame, kname))
                if frames > 0:
                    self.pending.append((self.frame + frames, kname, "move-" + kname))
        elif op == "stop_move":
            for kname in list(self.holds):
                if kname in _MOVE_KEYS:
                    self.up(kname, "move-" + kname)
        elif op == "face":
            who = self.role_or_player(actor)
            yaw = float(stepv[3])
            who.set_actor_rotation(unreal.Rotator(0.0, 0.0, yaw), False)
            if actor == "player":
                self.pc.set_control_rotation(unreal.Rotator(0.0, 0.0, yaw))
        elif op == "teleport":
            who = self.role_or_player(actor)
            loc = stepv[3]
            yaw = float(stepv[4]) if len(stepv) > 4 else who.get_actor_rotation().yaw
            who.set_actor_location_and_rotation(
                unreal.Vector(*loc), unreal.Rotator(0.0, 0.0, yaw), False, True)
            if actor == "player":
                self.pc.set_control_rotation(unreal.Rotator(0.0, 0.0, yaw))
        elif op == "fly":
            who = self.role_or_player(actor)
            mode = unreal.MovementMode.MOVE_FLYING if stepv[3] else unreal.MovementMode.MOVE_WALKING
            who.get_editor_property("character_movement").set_movement_mode(mode, 0)
        elif op == "set":
            who = self.role_or_player(actor)
            who.set_editor_property(stepv[3], self.coerce(stepv[3], stepv[4]))
        elif op == "set_stamina":
            self.role_or_player(actor).debug_set_stamina(float(stepv[3]))
        elif op == "set_health":
            self.role_or_player(actor).debug_set_health(float(stepv[3]))
        elif op == "mark":
            self.mark("MARK %s %s" % (self.sid, stepv[3]))
        else:
            raise RuntimeError("unknown plan op %r" % (op,))

    def up(self, kname, action):
        if kname in self.holds:
            unreal.TDInputTools.input_key(self.pc, key(kname), False)
            self.holds.remove(kname)
            self.mark("INJECT %s frame=%d %s release" % (self.sid, self.frame, action))

    def expire_holds(self, f):
        pending = self.pending
        still = []
        for due, kname, action in pending:
            if f >= due:
                self.up(kname, action)
            else:
                still.append((due, kname, action))
        self.pending = still

    def role_or_player(self, actor):
        if actor == "player":
            return self.pawn
        return self.pie_roles.get(actor)

    def sample(self, f, now):
        for pawn in [self.pawn] + list(self.pie_roles.values()):
            loc = pawn.get_actor_location()
            try:
                health, stamina = pawn.get_health(), pawn.get_stamina()
            except Exception:
                health = stamina = -1.0
            self.tape.write("%d\t%.3f\t%s\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%s\n" % (
                f, now, pawn.get_name(), loc.x, loc.y, loc.z,
                pawn.get_actor_rotation().yaw, health, stamina, ",".join(tags_on(pawn))))

    def periodic_attackers(self):
        """Roles whose attack loop runs from BeginPlay; a reset would cancel a swing in flight."""
        out = []
        for role in self.pie_roles:
            if SC.knobs_for(self.sid, role).get("debug_auto_attack"):
                out.append(role)
        return out

    def readout(self, kind, extra=""):
        pawns = [self.pawn] + list(self.pie_roles.values())
        for p in pawns:
            try:
                self.mark("%s %s%s tags=%s states=%s health=%.1f stamina=%.1f" % (
                    kind, extra, p.get_name(), ",".join(tags_on(p)) or "-",
                    ",".join(states_on(p)) or "-", p.get_health(), p.get_stamina()))
            except Exception as exc:
                self.mark("%s %s%s unreadable (%s)" % (kind, extra, p.get_name(), exc))

    def tick_tape(self):
        """Sampling through the gate and the settle, so the tape has no gap where the readout and
        the reset happen."""
        s = SC.SCENARIOS[self.sid]
        every = int(s.get("tape_every", 2))
        if self.tapes and self.tape and self.frame % every == 0:
            gw = ues.get_game_world()
            self.sample(self.frame, unreal.GameplayStatics.get_time_seconds(gw))
        self.frame += 1

    def phase_gate(self):
        """Between reps: the game settles, the readout records what it left, then the reset and
        the placements re-establish the starting state."""
        self.tick_tape()
        self.wait += 1
        self.release_all()
        s = SC.SCENARIOS[self.sid]
        expect = s.get("expect", {})
        attackers = set(self.periodic_attackers())
        quiet = [self.pawn] + [a for r, a in self.pie_roles.items() if r not in attackers]
        if self.settled_for() < SETTLE_TIMEOUT_S and not all(at_rest(p) and still(p) for p in quiet):
            return
        gw = ues.get_game_world()
        now = unreal.GameplayStatics.get_time_seconds(gw)
        self.readout("REP", "%s n=%d game=%.3f " % (self.sid, self.rep, now))
        for p in quiet:
            try:
                p.debug_reset_for_fixture()
            except Exception:
                pass
        for r in attackers:
            a = self.pie_roles[r]
            try:
                a.debug_set_health(a.get_max_health())
                a.debug_set_stamina(a.get_max_stamina())
            except Exception:
                pass
        self.place_player()
        self.place_roles()
        self.rep += 1
        if self.rep >= int(expect.get("reps", 1)):
            self.goto("settle")
            return
        self.step, self.rep_frame, self.base = 0, 0, 0
        self.lock_seen, self.lock_wait, self.last_due = False, 0, 0
        self.goto("run")

    def phase_settle(self):
        self.tick_tape()
        self.wait += 1
        self.release_all()
        pawns = [self.pawn] + list(self.pie_roles.values())
        if self.settled_for() < SETTLE_TIMEOUT_S and not all(at_rest(p) and still(p) for p in pawns):
            return
        self.readout("TEARDOWN")
        for p in pawns:
            try:
                p.debug_reset_for_fixture()
            except Exception:
                pass
        gw = ues.get_game_world()
        game_s = unreal.GameplayStatics.get_time_seconds(gw) - self.begin_game_time
        if self.tape:
            self.tape.close()
            self.tape = None
        les.editor_request_end_play()
        self._end = (game_s, self.frame)
        self.goto("ending")

    def phase_ending(self):
        self.wait += 1
        if les.is_in_play_in_editor():
            if self.wait % 120 == 1:
                les.editor_request_end_play()
            return
        game_s, frames = getattr(self, "_end", (0.0, 0))
        self.mark("END %s status=ok game=%.3f frames=%d" % (self.sid, game_s, frames))
        self.goto("next")


def main():
    cfg_path = os.path.join(REG_DIR, "run.json")
    with open(cfg_path) as fh:
        cfg = json.load(fh)
    prior = getattr(STATE, "ACTIVE_RUN", None)
    if prior is not None:
        prior.finish()
    run = Run(cfg)
    STATE.ACTIVE_RUN = run
    if run.arm():
        print("ARMED %s: %d scenarios" % (run.run_id, len(run.ids)))
    print("DONE")


main()
