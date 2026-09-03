"""The fixture authority: one entry per regression scenario.

Imported by ue_regression_runner.py inside the editor and by regression_run.py outside it, so
nothing here imports `unreal` at module level -- enums and gameplay tags are named as strings and
resolved by the runner.

A scenario entry:

    "tier-light": dict(
        family="tier", legacy=True, legacy_id="s1-light",
        roles=dict(attacker=("BP_TrainingDummy_C_2", (200, 0, 96), 180),
                   defender=("BP_TrainingDummy_C_1", (200, -150, 96), 90)),
        knobs={"attacker": {...}, "defender": {...}},   # merged over BASELINE, written in full
        player=dict(spawn=(-3000, -3000, 100), yaw=0, props={}),
        plan=[],                                        # (frame, actor, op, *args)
        stop=dict(duration=30.0),                       # or dict(until=(pattern, n), timeout=90)
        expect=dict(reps=8),
        mutations=[("shift", "RELEASE BEGIN", 0.100)],
        golden=dict(exclude=["drift="]),
    )

Plan frames are 1/60 s from plan start, which is the tick after SETUP. An injection made in the
post-tick callback of frame N is consumed by the player's input processing in frame N+1, so
"press at frame N" names the frame whose INPUT line carries it.
"""

# --- knobs ------------------------------------------------------------------
# Every Debug* knob at its CDO value, read from Docs/Combat-Values.tsv (BP_TrainingDummy column,
# regenerated 2026-09-02). A scenario's knobs are merged over this and the result is written in
# full on every role actor before each PIE start, so no knob carries over from the run before.
#
# bDebugAutoAttack is **true** here because that is the CDO. A row wanting a silent dummy says so.
BASELINE = {
    "debug_auto_attack": True,
    "debug_auto_attack_input_tag": "InputTag.Attack",
    "debug_auto_attack_interval": 3.0,
    "debug_auto_attack_hold_seconds": 0.1,
    "debug_auto_attack_reset_position": True,
    "debug_auto_attack_reset_delay_seconds": 0.35,
    "debug_auto_attack_string_taps": 1,
    "debug_auto_attack_string_tap_interval_seconds": 0.5,
    "debug_auto_attack_facing_mode": "WHILE_ATTACKING",
    "debug_auto_attack_rotate_targets": False,
    "debug_auto_attack_home_between_attacks": False,
    "debug_dodge_after_hit": False,
    "debug_cancel_attack_into_block": False,
    "debug_cancel_after_press_seconds": 0.1,
    "debug_suppress_lunge": False,
    "debug_auto_defend_mode": "OFF",
    "debug_defend_block_input_tag": "InputTag.Block",
    "debug_defend_dodge_input_tag": "InputTag.Dodge",
    "debug_defend_parry_input_tag": "InputTag.Parry",
    "debug_periodic_jump": False,
    "debug_jump_interval_seconds": 1.3,
    "debug_jump_input_tag": "InputTag.Jump",
    "debug_get_up_mode": "WAIT",
    "debug_get_up_delay_seconds": 0.05,
    "debug_home_at_stand": True,
    "debug_parry_interval_seconds": 1.7,
    "debug_parry_pre_block_seconds": 0.0,
    "debug_dodge_interval_seconds": 1.9,
    "debug_auto_revive_seconds": 3.0,
}

# Knob names whose value is an enum, and the unreal type each resolves against.
ENUM_KNOBS = {
    "debug_auto_attack_facing_mode": "TDDebugFacingMode",
    "debug_auto_defend_mode": "TDDebugDefendMode",
    "debug_get_up_mode": "TDDebugGetUpMode",
}
TAG_KNOBS = [k for k in BASELINE if k.endswith("_input_tag")]

# Silences a dummy outright: no attack loop, no defence, no jump, no get-up press.
SILENT = {"debug_auto_attack": False, "debug_auto_defend_mode": "OFF",
          "debug_periodic_jump": False, "debug_get_up_mode": "WAIT"}

# --- placements -------------------------------------------------------------
# Legacy rows keep the positions their matrix rows were measured at. Scripted rows use OPEN_*,
# chosen away from the ramp and inside the floor, the defender 150 cm along the attacker's facing.
PLACED_ATTACKER = ("BP_TrainingDummy_C_2", (200.0, 0.0, 96.0), 180.0)
PLACED_DEFENDER = ("BP_TrainingDummy_C_1", (200.0, -150.0, 96.0), 90.0)

OPEN_ATTACKER = (-1500.0, -1500.0, 96.0), 90.0
OPEN_DEFENDER = (-1500.0, -1350.0, 96.0), 270.0

# Where a pawn is parked to keep it out of every other row's exchange.
PARKED = (-3000.0, -3000.0, 100.0)
PARKED_DUMMY = (-3400.0, -3000.0, 96.0)

# The floor's extent, asserted at load so a placement typo fails before PIE rather than during it.
FLOOR_LIMIT = 4500.0

# Actions a plan may name. The runner resolves each to its key from the mapping contexts at load.
ACTIONS = ("attack", "block", "dodge", "parry", "jump", "move")

# Fixture holds must sit strictly between two ladder checkpoints, or the tier they select is a
# coin toss. Sources: GA_Attack's Branches[*].HoldUntilSeconds.
CHECKPOINTS = (0.15, 0.35, 0.75)


# --- scenarios --------------------------------------------------------------
# C2 drives three of these end to end: tier-light for knobs and placements, chain-early for a
# tapped plan, input-hold-tier for a hold spanning an activation boundary. The remaining 35
# legacy rows port at C5.

SCENARIOS = {

    "tier-light": dict(
        family="tier", legacy=True, legacy_id="s1-light",
        roles=dict(attacker=PLACED_ATTACKER, defender=PLACED_DEFENDER),
        knobs={"attacker": {"debug_auto_attack_hold_seconds": 0.1},
               "defender": dict(SILENT)},
        player=dict(spawn=PARKED, yaw=0.0, props={}),
        plan=[],
        stop=dict(duration=30.0),
        expect=dict(reps=8),
        mutations=[("shift", "RELEASE BEGIN", 0.100)],
        golden=dict(exclude=["drift=", "pos=", "rate="]),
    ),

    # The player throws every swing in the s8 family, at a pawn parked in open space: each swing
    # whiffs, because a landed hit waives commitment and resets the string.
    "chain-early": dict(
        family="chain", legacy=True, legacy_id="s8-chain-early",
        roles=dict(attacker=(PLACED_ATTACKER[0],) + (PARKED_DUMMY, 0.0),
                   defender=(PLACED_DEFENDER[0],) + (PARKED_DUMMY, 0.0)),
        knobs={"attacker": dict(SILENT), "defender": dict(SILENT)},
        player=dict(spawn=(-4000.0, -4000.0, 100.0), yaw=0.0, props={}),
        plan=[(0, "player", "tap", "attack"), (21, "player", "tap", "attack")],
        stop=dict(duration=30.0),
        expect=dict(reps=8, period_frames=180),
        mutations=[("drop", "STRING     chain out", 1)],
        golden=dict(exclude=["pos=", "rate="]),
    ),

    "input-hold-tier": dict(
        family="input", legacy=True, legacy_id="s8-hold-tier",
        roles=dict(attacker=(PLACED_ATTACKER[0],) + (PARKED_DUMMY, 0.0),
                   defender=(PLACED_DEFENDER[0],) + (PARKED_DUMMY, 0.0)),
        knobs={"attacker": dict(SILENT), "defender": dict(SILENT)},
        player=dict(spawn=(-4000.0, -4000.0, 100.0), yaw=0.0, props={}),
        # Accepted 150 ms before actionable and held across it: 250 ms of hold in total, which must
        # buy the heavy rather than only the 100 ms that follows activation.
        #
        # Frame 51 is actionable minus 150 ms, actionable being the light's measured total of
        # 1.000 s for a whiffing swing. The s8 driver's frame 48 was derived against the authored
        # 0.950 and now lands 250 ms out, past InputBufferSeconds 0.200, so the press expires one
        # tick after the ability ends. Re-derive here whenever that total moves.
        plan=[(0, "player", "tap", "attack"), (51, "player", "hold", "attack", 15)],
        stop=dict(duration=30.0),
        expect=dict(reps=8, period_frames=180),
        mutations=[("set", "COMMIT", "branch", "0")],
        golden=dict(exclude=["pos=", "rate="]),
    ),
}


# --- validation -------------------------------------------------------------

def validate(known_knobs=None, known_actors=None, resolve_action=None):
    """Every check that can fail before PIE. Returns a list of problems, empty when sound.

    known_knobs, known_actors and resolve_action come from the editor when the runner calls this
    and are skipped outside it, so the orchestrator can still lint shape offline.
    """
    problems = []
    for sid, s in sorted(SCENARIOS.items()):
        where = "%s: " % sid
        for key in ("family", "roles", "knobs", "player", "plan", "stop", "expect"):
            if key not in s:
                problems.append(where + "missing '%s'" % key)
        if s.get("legacy") and not s.get("legacy_id"):
            problems.append(where + "legacy row with no legacy_id")
        if not s.get("mutations"):
            problems.append(where + "no mutation -- every row carries one that must turn it red")

        for role, placement in s.get("roles", {}).items():
            name, loc, _yaw = placement
            if known_actors is not None and name not in known_actors:
                problems.append(where + "role %s names absent actor %s" % (role, name))
            if max(abs(loc[0]), abs(loc[1])) > FLOOR_LIMIT:
                problems.append(where + "%s placed off the floor at %s" % (role, (loc[0], loc[1])))

        for role, knobs in s.get("knobs", {}).items():
            if role not in s.get("roles", {}):
                problems.append(where + "knobs for role %s, which has no placement" % role)
            for k in knobs:
                if k not in BASELINE:
                    problems.append(where + "unknown knob '%s'" % k)
                elif known_knobs is not None and k not in known_knobs:
                    problems.append(where + "knob '%s' is not a property on the class" % k)

        spawn = s.get("player", {}).get("spawn", (0, 0, 0))
        if max(abs(spawn[0]), abs(spawn[1])) > FLOOR_LIMIT:
            problems.append(where + "player spawn off the floor at %s" % (spawn[:2],))

        for step in s.get("plan", []):
            frame, actor, op = step[0], step[1], step[2]
            if not isinstance(frame, int) or frame < 0:
                problems.append(where + "plan frame %r is not a frame" % (frame,))
            if op in ("tap", "press", "release", "hold", "ready") and actor != "player":
                problems.append(where + "op %s is the player's, not %s's" % (op, actor))
            if op in ("tap", "press", "release", "hold"):
                action = step[3]
                if action not in ACTIONS:
                    problems.append(where + "unknown action '%s'" % action)
                elif resolve_action is not None and not resolve_action(action):
                    problems.append(where + "action '%s' resolves to no key" % action)
            if op == "hold":
                held = step[4] / 60.0
                if any(abs(held - c) < 1.0 / 60.0 for c in CHECKPOINTS):
                    problems.append(where + "hold of %.3fs sits on a checkpoint" % held)

        stop = s.get("stop", {})
        if "duration" not in stop and "until" not in stop:
            problems.append(where + "stop names neither a duration nor an until")
    return problems


def by_family(family):
    return sorted(k for k, v in SCENARIOS.items() if v["family"] == family)


def knobs_for(sid, role):
    """The full knob set for one role: BASELINE with the scenario's overrides merged over it."""
    merged = dict(BASELINE)
    merged.update(SCENARIOS[sid].get("knobs", {}).get(role, {}))
    return merged
