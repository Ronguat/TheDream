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

# Plan ops the runner implements. tap/press/hold/release/move/stop_move drive the player's keys;
# face, teleport, set, set_stamina and set_health take any role; lock_to rebases the plan's frames
# to the frame its tag appears on the named actor.
OPS = ("tap", "press", "release", "hold", "move", "stop_move", "face", "teleport", "set",
       "set_stamina", "set_health", "lock_to", "mark")

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
                                      mutations=[("set", "KNOCKDOWN RISE", "by", "dodge")],
                                      teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted", "Dead",
                                                       "KnockedDown"]),
    "knockdown-exhausted-kipup": _row("knockdown", "s6-exhausted-kipup", 120, _atk(HEAVY),
                                      _def("PERIODIC_PARRY", debug_parry_pre_block_seconds=12.0,
                                           debug_parry_interval_seconds=13.0,
                                           debug_get_up_mode="DODGE_GET_UP"),
                                      mutations=[("set", "KNOCKDOWN RISE", "by", "kipup")],
                                      teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted", "Dead",
                                                       "KnockedDown"]),
    "knockdown-exhausted-block": _row("knockdown", "s6-exhausted-block", 120,
                                      _atk(LIGHT, **STRING),
                                      _def("PERIODIC_PARRY", debug_parry_pre_block_seconds=12.0,
                                           debug_parry_interval_seconds=13.0,
                                           debug_get_up_mode="BLOCK_GET_UP"),
                                      mutations=[("set", "KNOCKDOWN RISE", "by", "block")],
                                      teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted", "Dead",
                                                       "KnockedDown"]),
    "knockdown-exhausted-attack": _row("knockdown", "s6-exhausted-attack", 120,
                                       _atk(LIGHT, **STRING),
                                       _def("PERIODIC_PARRY", debug_parry_pre_block_seconds=12.0,
                                            debug_parry_interval_seconds=13.0,
                                            debug_get_up_mode="ATTACK_GET_UP"),
                                       mutations=[("set", "KNOCKDOWN RISE", "by", "auto")],
                                       teardown_allow=["Attacking", "StaminaRegenPaused", "Exhausted", "Dead",
                                                       "KnockedDown"]),

    # --- death ---------------------------------------------------------------
    "death-revive": _row("death", "s7-death", 90, _atk(LIGHT, **STRING),
                         _def(debug_auto_revive_seconds=3.0),
                         mutations=[("shift", "REVIVE", 1.000)]),
    "death-over-knockdown": _row("death", "s7-death-grade", 60, _atk(HEAVY), _def(),
                                 mutations=[("drop", "DEATH ", 99)]),
}


# --- scripted rows -----------------------------------------------------------
# The player is the precisely timed actor (D2). Each rep runs one plan -- plans[k % len] for rep k
# -- from the frame a lock_to's tag appears on, and the gate between reps settles, reads the pawn the
# game left, resets it and restores every placement. Assertions live in regression_rows.py.

def _player_defends(family, attacker, plans, mutations, reps, expect=None, tail=60, duration=None,
                    tape_every=2, teardown_allow=("Attacking", "StaminaRegenPaused")):
    """The attacker dummy on open ground with its loop running; the player stands where the
    defender stood; the second dummy is parked and silent."""
    ex = dict(reps=reps, gate=True, tail_frames=tail)
    ex.update(expect or {})
    return dict(
        family=family, legacy=False,
        roles=dict(attacker=(PLACED_ATTACKER[0],) + OPEN_ATTACKER,
                   defender=(PLACED_DEFENDER[0], PARKED_DUMMY, 0.0)),
        knobs={"attacker": dict(attacker), "defender": dict(SILENT)},
        player=dict(spawn=(OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1], 100.0), yaw=OPEN_DEFENDER[1],
                    props={}),
        plans=[list(p) for p in plans], plan=[],
        stop=dict(duration=float(duration or (reps * 8.0 + 20.0))),
        expect=ex, mutations=list(mutations), golden=dict(exclude=["drift="]),
        teardown_allow=list(teardown_allow), tape_every=tape_every,
    )


def _player_alone(family, plans, mutations, reps, expect=None, tail=90, duration=None,
                  target=None, props=None, tape_every=2):
    """Both dummies silent; the player throws in open space, or at a parked target when a row
    places one."""
    ex = dict(reps=reps, gate=True, tail_frames=tail)
    ex.update(expect or {})
    return dict(
        family=family, legacy=False,
        roles=dict(attacker=(PLACED_ATTACKER[0], PARKED_DUMMY, 0.0),
                   defender=(PLACED_DEFENDER[0],) + (target or (PARKED_DUMMY, 0.0))),
        knobs={"attacker": dict(SILENT), "defender": dict(SILENT)},
        player=dict(spawn=(-4000.0, -4000.0, 100.0) if target is None
                    else (OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1], 100.0),
                    yaw=0.0 if target is None else OPEN_DEFENDER[1], props=dict(props or {})),
        plans=[list(p) for p in plans], plan=[],
        stop=dict(duration=float(duration or (reps * 4.0 + 20.0))),
        expect=ex, mutations=list(mutations), golden=dict(exclude=["drift="]),
        teardown_allow=[], tape_every=tape_every,
    )


LOCK_HITSTUN = (0, "player", "lock_to", "State.Hitstun")
LOCK_DOWN = (0, "player", "lock_to", "State.KnockedDown")

# The light's hitstun is 33 f and its hit lands 12 f after the press, so the acceptance window
# opens 21 f into the stun; 19 f is outside it and 23 f inside. Sources: Cells[0].HitstunSeconds,
# Branches[0].ReleaseAtSeconds, InputBufferSeconds.
SCENARIOS["input-accept-hitstun"] = _player_defends(
    "input", _atk(LIGHT),
    plans=[[LOCK_HITSTUN, (19, "player", "tap", "attack")],
           [LOCK_HITSTUN, (23, "player", "tap", "attack")]],
    mutations=[("shift", "HITSTUN END", -0.100)], reps=6, tail=90)

# Blockstun is 21 f from the blocked hit and disables offense only, so the acceptance window opens
# at 9 f; presses at 7 f and 11 f sit either side. The guard is raised two frames into the swing and
# released the frame after the block lands; its floor keeps it up a few frames longer, and the attack
# buffers through that regardless. Sources: Cells[0].BlockstunSeconds, MinimumBlockSeconds.
_BLOCK_THEN_STUN = [(0, "attacker", "lock_to", "State.Attacking"),
                    (2, "player", "press", "block"),
                    (0, "player", "lock_to", "State.Blockstun"),
                    (1, "player", "release", "block")]
SCENARIOS["input-accept-blockstun"] = _player_defends(
    "input", _atk(LIGHT),
    plans=[_BLOCK_THEN_STUN + [(7, "player", "tap", "attack")],
           _BLOCK_THEN_STUN + [(11, "player", "tap", "attack")]],
    mutations=[("shift", "BLOCKSTUN END", -0.100)], reps=6, tail=90)

# The player throws a light into the defender dummy's parry window and takes the light's 45 f
# lockout; a press at 31 f is outside its acceptance window and one at 35 f inside. Sources:
# Cells[0].ParryLockoutSeconds, InputBufferSeconds.
_INTO_PARRY = [(0, "defender", "lock_to", "State.Parrying"),
               (0, "player", "tap", "attack"),
               (0, "player", "lock_to", "State.ParryLockout")]
SCENARIOS["input-accept-lockout"] = dict(
    family="input", legacy=False,
    roles=dict(attacker=(PLACED_ATTACKER[0], PARKED_DUMMY, 0.0),
               defender=(PLACED_DEFENDER[0],) + OPEN_DEFENDER),
    knobs={"attacker": dict(SILENT), "defender": _def("PERIODIC_PARRY")},
    player=dict(spawn=(OPEN_ATTACKER[0][0], OPEN_ATTACKER[0][1], 100.0), yaw=OPEN_ATTACKER[1],
                props={}),
    plans=[_INTO_PARRY + [(31, "player", "tap", "attack")],
           _INTO_PARRY + [(35, "player", "tap", "attack")]], plan=[],
    stop=dict(duration=90.0), expect=dict(reps=6, gate=True, tail_frames=90),
    mutations=[("shift", "PARRY LOCKOUT END", -0.100)], golden=dict(exclude=["drift="]),
    teardown_allow=["StaminaRegenPaused"], tape_every=2,
)

# Exhausted from the frame the heavy begins, floored hard, then every option held: only the attack
# rises, at the lockout's end; a held guard alone leaves the wait's auto-rise.
_EXHAUST_THEN_DOWN = [(0, "attacker", "lock_to", "State.Attacking"),
                      (0, "player", "set_stamina", 0.0),
                      LOCK_DOWN]
SCENARIOS["knockdown-getup-exhausted-held"] = _player_defends(
    "knockdown", _atk(HEAVY),
    plans=[_EXHAUST_THEN_DOWN + [(30, "player", "hold", h, 90) for h in ("block", "dodge", "attack")],
           _EXHAUST_THEN_DOWN + [(30, "player", "hold", "block", 90)]],
    mutations=[("set", "KNOCKDOWN RISE", "by", "block")], reps=6, tail=120,
    expect=dict(by=["attack", "auto"], held=["Attack", None], lockout=1.5, auto_at=2.0))

# Hard knockdown: lockout 90 f, window 30 f, auto-rise at 120 f. Each variant holds from 30 f into
# the lockout until past the rise. Priority is guard, dodge, attack, stand (KnockdownGetUpPriority).
_HARD_HOLDS = [["block"], ["dodge"], ["attack"], ["jump"],
               ["block", "dodge"], ["dodge", "attack"], ["attack", "jump"]]
SCENARIOS["knockdown-getup-held"] = _player_defends(
    "knockdown", _atk(HEAVY),
    plans=[[LOCK_DOWN] + [(30, "player", "hold", h, 90) for h in hs] for hs in _HARD_HOLDS],
    mutations=[("set", "KNOCKDOWN RISE", "by", "auto")], reps=14, tail=120,
    expect=dict(by=["block", "kipup", "attack", "auto", "block", "kipup", "attack"],
                held=["Block", "Dodge", "Attack", None, "Block", "Dodge", "Attack"],
                lockout=1.5, auto_at=2.0))

# Normal knockdown from the string's ender: lockout 60 f. The stand is legal here, and all four
# held together must still yield the guard.
SCENARIOS["knockdown-getup-held-normal"] = _player_defends(
    "knockdown", _atk(LIGHT, **STRING),
    plans=[[LOCK_DOWN, (30, "player", "hold", "jump", 60)],
           [LOCK_DOWN] + [(30, "player", "hold", h, 60) for h in ("block", "dodge", "attack", "jump")]],
    mutations=[("set", "KNOCKDOWN RISE", "by", "auto")], reps=4, tail=120,
    expect=dict(by=["stand", "block"], held=["Jump", "Block"], lockout=1.0, auto_at=2.0))

# Holds against the 0.150 s checkpoint. 8 and 11 frames are the sides and must commit different
# tiers; 9 sits on the checkpoint and 10 on the frame the checkpoint timer can slip to, and both
# are reported rather than asserted -- which side each falls on is a ruling, and 10 is a race.
SCENARIOS["edge-light-checkpoint"] = _player_alone(
    "edge",
    plans=[[(0, "player", "hold", "attack", 8)],
           [(0, "player", "hold", "attack", 11)],
           [(0, "player", "hold", "attack", 9)],
           [(0, "player", "hold", "attack", 10)]],
    mutations=[("set", "COMMIT", "branch", "1")], reps=8, tail=100,
    expect=dict(holds=[8, 11, 9, 10], want=[0, 1, None, None]))

# The guard breaks on one heavy once the bar is set to 40: the raise costs 10 and the heavy's 50
# empties it. The held move must displace nothing through the break's stun and walk the frame it
# ends; the control rep holds no move. Sampled every frame, since the comparison is per frame.
_BREAK = [(0, "player", "set_stamina", 40.0),
          (0, "attacker", "lock_to", "State.Attacking"),
          (2, "player", "press", "block"),
          (0, "player", "lock_to", "State.GuardBroken"),
          (1, "player", "release", "block")]
SCENARIOS["lock-guard-break"] = _player_defends(
    "lock", _atk(HEAVY),
    plans=[_BREAK + [(1, "player", "move", 0.0, 1.0, 90), (10, "player", "tap", "jump")],
           _BREAK],
    mutations=[("drop", "REFUSED", 99)], reps=4, tail=100, tape_every=1)

# Reach is measured to the target's body: a capsule of radius 42 counts while centre distance minus
# 42 is inside MaxReachCm 150, so the transition sits at 192 cm. The player's lunge is suppressed
# so the swing resolves from where it was thrown.
_REACH_IN, _REACH_OUT = 187.0, 197.0
SCENARIOS["reach-light"] = _player_alone(
    "reach",
    plans=[[(0, "defender", "teleport", (OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1] - _REACH_IN, 96.0), 90.0),
            (6, "player", "tap", "attack")],
           [(0, "defender", "teleport", (OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1] - _REACH_OUT, 96.0), 90.0),
            (6, "player", "tap", "attack")]],
    mutations=[("drop", "DAMAGED", 99)], reps=4, tail=90,
    target=((OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1] - _REACH_IN, 96.0), 90.0),
    props={"debug_suppress_lunge": True},
    expect=dict(hits=[True, False]))


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

        all_plans = s.get("plans") or [s.get("plan", [])]
        if s.get("expect", {}).get("gate") and not s.get("plans"):
            problems.append(where + "a gated row needs plans")
        for step in [st for p in all_plans for st in p]:
            frame, actor, op = step[0], step[1], step[2]
            if not isinstance(frame, int) or frame < 0:
                problems.append(where + "plan frame %r is not a frame" % (frame,))
            if op not in OPS:
                problems.append(where + "unknown op '%s'" % op)
            if op in ("tap", "press", "release", "hold", "move", "stop_move") and actor != "player":
                problems.append(where + "op %s is the player's, not %s's" % (op, actor))
            if actor != "player" and actor not in s.get("roles", {}):
                problems.append(where + "op %s names %s, which is not a role" % (op, actor))
            if op in ("tap", "press", "release", "hold"):
                action = step[3]
                if action not in ACTIONS:
                    problems.append(where + "unknown action '%s'" % action)
                elif resolve_action is not None and not resolve_action(action):
                    problems.append(where + "action '%s' resolves to no key" % action)
            # An attack hold on a checkpoint commits a tier by float error; the edge family probes
            # exactly that and is the one place it is meant.
            if op == "hold" and step[3] == "attack" and s.get("family") != "edge":
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
