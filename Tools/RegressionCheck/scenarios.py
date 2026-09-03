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
# The 38 legacy rows, ported under the names of D9. Their fixtures are the matrix's, unchanged in
# meaning: the attacker dummy drives the exchange through its Debug knobs, and the player is parked
# out of reach -- except the s8 family, where the player throws every swing and both dummies are
# silent.
#
# _row builds the common shape so a row's entry is its knobs and its plan, not its boilerplate.


def _row(family, legacy_id, duration, attacker=None, defender=None, player_spawn=PARKED,
         player_yaw=0.0, plan=(), reps=None, mutations=(), exclude=("drift=",), props=None,
         teardown_allow=('Attacking', 'StaminaRegenPaused')):
    return dict(
        family=family, legacy=True, legacy_id=legacy_id,
        roles=dict(attacker=PLACED_ATTACKER, defender=PLACED_DEFENDER),
        knobs={"attacker": dict(attacker or {}), "defender": dict(defender or {})},
        player=dict(spawn=player_spawn, yaw=player_yaw, props=dict(props or {})),
        plan=list(plan), stop=dict(duration=float(duration)),
        expect=dict(reps=reps) if reps else dict(),
        mutations=list(mutations), golden=dict(exclude=list(exclude)),
        # States a fixture holds on purpose, so the hygiene check does not read a held guard as a
        # leak. Everything not named here must be gone before the reset runs.
        teardown_allow=list(teardown_allow),
    )


def _scripted(family, legacy_id, plan, mutations, reps=8, period=180, duration=30.0):
    """An s8-family row: the player throws every swing at a pawn parked in open space, so each
    swing whiffs -- a landed hit waives commitment and resets the string, a different question."""
    return dict(
        family=family, legacy=True, legacy_id=legacy_id,
        roles=dict(attacker=(PLACED_ATTACKER[0], PARKED_DUMMY, 0.0),
                   defender=(PLACED_DEFENDER[0], PARKED_DUMMY, 0.0)),
        knobs={"attacker": dict(SILENT), "defender": dict(SILENT)},
        player=dict(spawn=(-4000.0, -4000.0, 100.0), yaw=0.0, props={}),
        plan=list(plan), stop=dict(duration=duration),
        expect=dict(reps=reps, period_frames=period),
        mutations=list(mutations), golden=dict(exclude=["drift="]),
        # Nothing is fixture-held here: both dummies are silent and the player's own holds
        # are released at settle, so a scripted row keeps the strict hygiene check.
        teardown_allow=[],
    )


# The dummy's hold selects the tier: 0.1 light, 0.22 heavy, 0.85 charged, each strictly between
# two of the CHECKPOINTS above.
LIGHT, HEAVY, CHARGED = 0.1, 0.22, 0.85
STRING = {"debug_auto_attack_string_taps": 3}


def _atk(hold=LIGHT, **kw):
    k = {"debug_auto_attack_hold_seconds": hold}
    k.update(kw)
    return k


def _def(mode="OFF", **kw):
    k = dict(SILENT)
    k["debug_auto_defend_mode"] = mode
    k.update(kw)
    return k


SCENARIOS = {

    # --- tier: the ladder's three rungs from position 1 ----------------------
    "tier-light":   _row("tier", "s1-light",   30, _atk(LIGHT),   _def(),
                         mutations=[("shift", "RELEASE BEGIN", 0.100)]),
    "tier-heavy":   _row("tier", "s1-heavy",   30, _atk(HEAVY),   _def(),
                         mutations=[("shift", "RELEASE BEGIN", 0.100)]),
    "tier-charged": _row("tier", "s1-charged", 30, _atk(CHARGED), _def(),
                         mutations=[("shift", "RELEASE BEGIN", 0.100)]),

    # --- attack: what a swing hands back, and what it does not ---------------
    "attack-cancel": _row("attack", "s5-cancel", 30,
                          _atk(LIGHT, debug_cancel_attack_into_block=True), _def(),
                          mutations=[("drop", "BLOCK      cost", 99)]),
    "attack-waiver": _row("attack", "s5-waiver", 30,
                          _atk(LIGHT, debug_dodge_after_hit=True), _def(),
                          mutations=[("shift", "DODGE      ", 0.500)]),

    # --- string: three swings at the tapped cadence --------------------------
    "string-cadence":   _row("string", "s4-string", 60, _atk(LIGHT, **STRING), _def(),
                             mutations=[("set", "DAMAGED", "damage", "99")]),
    "string-guarantee": _row("string", "s4-guarantee", 60, _atk(LIGHT, **STRING),
                             _def("PERIODIC_DODGE"),
                             mutations=[("shift", "HITSTUN END", -0.400)]),
    "string-blocked":   _row("string", "s4-block", 60, _atk(LIGHT, **STRING), _def("HOLD_BLOCK"),
                             mutations=[("shift", "BLOCKSTUN END", 0.100)],
                             teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted"]),
    # The 360-degree ender needs a stationary attacker with a body on each side, so the player
    # stands in opposite the defender and the lunge is suppressed.
    "string-finisher-arc": _row("string", "s4-360", 90,
                                _atk(LIGHT, debug_auto_attack_facing_mode="NEVER",
                                     debug_suppress_lunge=True, **STRING),
                                _def(), player_spawn=(200.0, 150.0, 100.0),
                                mutations=[("drop", "DAMAGED", 1)],
                                teardown_allow=["Attacking", "StaminaRegenPaused", "KnockedDown"]),

    # --- chain and input: the s8 family, thrown by the player ----------------
    # Carried from ue_s8_driver.py, retired 2026-09-03 once all six ran green through the runner.
    # Light 1's spans, which every frame below is derived from: windup 0.200, release to 0.350,
    # recovery to 0.950 authored, chain-out open [0.483, 0.683], InputBufferSeconds 0.200.
    #
    # Frames are actionable-relative, and **actionable is measured, not authored**: a whiffing
    # light's total reads 1.000 s, so a press meant to land N ms before it sits at frame
    # 60 - N/16.67. The driver's own frames were derived against the authored 0.950 and are 3
    # frames early, which is why chain-closed and input-discard moved on the port.
    #
    #   chain-early    tap, tap at 21 f (0.35 s)  -- in the buffered slice, fires when chain opens
    #   chain-late     tap, tap at 36 f (0.60 s)  -- inside the open span, fires on the press
    #   chain-closed   tap, tap at 51 f           -- past the close, 150 ms before actionable:
    #                                                no chain, a fresh swing 0 instead
    #   input-stale    hold 24 f, tap at 30 f     -- a tap during a committed heavy must expire
    #   input-discard  tap, tap at 45 f           -- 250 ms before actionable, outside acceptance
    #   input-hold-tier tap, hold 15 f from 51 f  -- accepted 150 ms before actionable and held
    #                                                across it, so the ladder must count all 250 ms
    "chain-early":  _scripted("chain", "s8-chain-early",
                              [(0, "player", "tap", "attack"), (21, "player", "tap", "attack")],
                              [("drop", "STRING     chain out", 1)]),
    "chain-late":   _scripted("chain", "s8-chain-late",
                              [(0, "player", "tap", "attack"), (36, "player", "tap", "attack")],
                              [("drop", "STRING     chain out", 1)]),
    "chain-closed": _scripted("chain", "s8-chain-closed",
                              [(0, "player", "tap", "attack"), (51, "player", "tap", "attack")],
                              [("set", "ACTIVATE", "swing", "1")]),
    "input-stale":  _scripted("input", "s8-stale",
                              [(0, "player", "hold", "attack", 24), (30, "player", "tap", "attack")],
                              [("dup", "ACTIVATE", 1)]),
    "input-discard": _scripted("input", "s8-discard",
                               [(0, "player", "tap", "attack"), (45, "player", "tap", "attack")],
                               [("dup", "ACTIVATE", 1)]),
    "input-hold-tier": _scripted("input", "s8-hold-tier",
                                 [(0, "player", "tap", "attack"),
                                  (51, "player", "hold", "attack", 15)],
                                 [("set", "COMMIT", "branch", "0")]),

    # --- block: the guard's price per tier -----------------------------------
    "block-light":   _row("block", "s2-light", 150, _atk(LIGHT), _def("HOLD_BLOCK"),
                          mutations=[("set", "BLOCKED", "staminaDamage", "99")],
                          teardown_allow=["Blocking", "StaminaRegenPaused"]),
    "block-heavy":   _row("block", "s2-heavy", 150, _atk(HEAVY), _def("HOLD_BLOCK"),
                          mutations=[("set", "BLOCKED", "staminaDamage", "99")],
                          teardown_allow=["Blocking", "StaminaRegenPaused"]),
    "block-charged": _row("block", "s2-charged", 150, _atk(CHARGED), _def("HOLD_BLOCK"),
                          mutations=[("set", "BLOCKED", "staminaDamage", "1")],
                          teardown_allow=["Blocking", "StaminaRegenPaused", "Exhausted"]),

    # --- dodge ---------------------------------------------------------------
    "dodge-cycle": _row("dodge", "s3", 120, _atk(LIGHT), _def("PERIODIC_DODGE"),
                        mutations=[("set", "DODGE END", "dist", "999.9")]),

    # --- parry ---------------------------------------------------------------
    "parry-catch":  _row("parry", "s5-parry", 180, _atk(LIGHT, **STRING), _def("PERIODIC_PARRY"),
                         mutations=[("drop", "PARRY SUCCESS", 1)]),
    "parry-reward": _row("parry", "s5-parry-reward", 60,
                         _atk(LIGHT, debug_auto_attack_interval=3.0, **STRING),
                         _def("PERIODIC_PARRY", debug_parry_interval_seconds=6.0,
                              debug_parry_pre_block_seconds=3.935),
                         mutations=[("set", "PARRY SUCCESS", "gained", "0.0")]),
    "parry-whiff":  _row("parry", "s5-parry-whiff", 60, _atk(LIGHT, **STRING),
                         _def("PERIODIC_PARRY", debug_parry_interval_seconds=0.5,
                              debug_auto_attack=True, debug_auto_attack_interval=0.7,
                              debug_suppress_lunge=True),
                         mutations=[("drop", "PARRY WHIFF", 1)]),

    # --- knockdown -----------------------------------------------------------
    "knockdown-normal": _row("knockdown", "s6-knockdown", 60, _atk(LIGHT, **STRING), _def(),
                             mutations=[("set", "KNOCKDOWN", "type", "hard")]),
    "knockdown-hard":   _row("knockdown", "s6-hard", 60, _atk(HEAVY), _def(),
                             mutations=[("set", "KNOCKDOWN", "type", "normal")]),
    "knockdown-stand":  _row("knockdown", "s6-stand", 90, _atk(LIGHT, **STRING),
                             _def(debug_periodic_jump=True, debug_jump_interval_seconds=1.3),
                             mutations=[("set", "KNOCKDOWN RISE", "by", "block")]),
    "knockdown-getup-attack": _row("knockdown", "s6-getup", 60, _atk(HEAVY),
                                   _def(debug_get_up_mode="ATTACK_GET_UP"),
                                   mutations=[("shift", "RELEASE BEGIN", 0.150)]),
    "knockdown-getup-dodge":  _row("knockdown", "s6-dodge", 60,
                                   _atk(LIGHT, debug_auto_attack_interval=6.0, **STRING),
                                   _def(debug_get_up_mode="DODGE_GET_UP"),
                                   mutations=[("set", "DODGE", "remaining", "99.9")]),
    "knockdown-getup-kipup":  _row("knockdown", "s6-kipup", 60,
                                   _atk(HEAVY, debug_auto_attack_interval=6.0),
                                   _def(debug_get_up_mode="DODGE_GET_UP"),
                                   mutations=[("set", "DODGE END", "dist", "999.9")]),
    "knockdown-getup-block":  _row("knockdown", "s6-block", 60,
                                   _atk(LIGHT, debug_auto_attack_interval=6.0, **STRING),
                                   _def(debug_get_up_mode="BLOCK_GET_UP"),
                                   mutations=[("drop", "KNOCKDOWN RISE", 99)]),
    "knockdown-hard-no-stand": _row("knockdown", "s6-hard-stand", 60, _atk(HEAVY),
                                    _def(debug_get_up_mode="STAND_GET_UP"),
                                    mutations=[("drop", "KNOCKDOWN RISE", 99)]),
    "knockdown-airborne": _row("knockdown", "s6-airborne", 240, _atk(LIGHT, **STRING),
                               _def(debug_periodic_jump=True, debug_jump_interval_seconds=1.3),
                               mutations=[("set", "KNOCKDOWN", "airborne", "0")]),
    # The pre-block drains the defender to a break, and the knockdowns land inside the exhaustion
    # that follows -- the only fixture that holds a character exhausted while it is floored.
    "knockdown-regen-exception": _row("knockdown", "s6-exhaust-regen", 150, _atk(LIGHT, **STRING),
                                      _def("PERIODIC_PARRY", debug_parry_pre_block_seconds=12.0,
                                           debug_parry_interval_seconds=13.0),
                                      mutations=[("shift", "EXHAUSTION END", 0.500)],
                                      teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted", "Dead",
                                                       "KnockedDown"]),
    "knockdown-exhausted-dodge": _row("knockdown", "s6-exhausted", 120, _atk(LIGHT, **STRING),
                                      _def("PERIODIC_PARRY", debug_parry_pre_block_seconds=12.0,
                                           debug_parry_interval_seconds=13.0,
                                           debug_get_up_mode="DODGE_GET_UP"),
                                      mutations=[("dup", "KNOCKDOWN RISE", 1)],
                                      teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted", "Dead",
                                                       "KnockedDown"]),
    "knockdown-exhausted-kipup": _row("knockdown", "s6-exhausted-kipup", 120, _atk(HEAVY),
                                      _def("PERIODIC_PARRY", debug_parry_pre_block_seconds=12.0,
                                           debug_parry_interval_seconds=13.0,
                                           debug_get_up_mode="DODGE_GET_UP"),
                                      mutations=[("dup", "KNOCKDOWN RISE", 1)],
                                      teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted", "Dead",
                                                       "KnockedDown"]),
    "knockdown-exhausted-block": _row("knockdown", "s6-exhausted-block", 120,
                                      _atk(LIGHT, **STRING),
                                      _def("PERIODIC_PARRY", debug_parry_pre_block_seconds=12.0,
                                           debug_parry_interval_seconds=13.0,
                                           debug_get_up_mode="BLOCK_GET_UP"),
                                      mutations=[("dup", "KNOCKDOWN RISE", 1)],
                                      teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted", "Dead",
                                                       "KnockedDown"]),
    "knockdown-exhausted-attack": _row("knockdown", "s6-exhausted-attack", 120,
                                       _atk(LIGHT, **STRING),
                                       _def("PERIODIC_PARRY", debug_parry_pre_block_seconds=12.0,
                                            debug_parry_interval_seconds=13.0,
                                            debug_get_up_mode="ATTACK_GET_UP"),
                                       mutations=[("drop", "KNOCKDOWN RISE", 1)],
                                       teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted", "Dead",
                                                       "KnockedDown"]),

    # --- death ---------------------------------------------------------------
    "death-revive": _row("death", "s7-death", 90, _atk(LIGHT, **STRING),
                         _def(debug_auto_revive_seconds=3.0),
                         mutations=[("shift", "REVIVE", 1.000)]),
    "death-over-knockdown": _row("death", "s7-death-grade", 60, _atk(HEAVY), _def(),
                                 mutations=[("drop", "DEATH ", 99)]),
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
