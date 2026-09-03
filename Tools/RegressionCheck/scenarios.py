"""The fixture authority: one entry per regression scenario.

Imported by ue_regression_runner.py inside the editor and by regression_run.py outside it, so
nothing here imports `unreal` at module level -- enums and gameplay tags are named as strings and
resolved by the runner.

A scenario entry:

    "tier-light": dict(
        family="tier", canary=True, covers=["release timing", ...],
        roles=dict(attacker=("BP_TrainingDummy_C_2", (200, 0, 96), 180),
                   defender=("BP_TrainingDummy_C_1", (200, -150, 96), 90)),
        knobs={"attacker": {...}, "defender": {...}},   # merged over BASELINE, written in full
        player=dict(spawn=(-3000, -3000, 100), yaw=0, props={}),
        plan=[],                                        # (frame, actor, op, *args)
        stop=dict(until=("ABILITY END", 8), timeout=30.0),   # or dict(duration=30.0)
        expect=dict(),
        mutations=[("shift", "RELEASE BEGIN", 0.100)],
        golden=dict(exclude=["drift="]),
    )

Plan frames are sixtieths of game time from plan start, which is the tick after SETUP. An injection
made in the post-tick callback of frame N is consumed by the player's input processing in frame N+1,
so "press at frame N" names the frame whose INPUT line carries it. A canary row is one whose timing
holds on the wall clock too -- driven by timers or by locks with offsets of many frames -- and is
what --realtime runs.
"""

import math
import os

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
# Timer-fixture rows keep the positions their matrix rows were measured at. Scripted rows use OPEN_*,
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
# to the frame its tag appears on the named actor; swing asks a dummy for its auto-attack now and
# stops its loop, so the lock after it takes that swing.
OPS = ("tap", "press", "release", "hold", "move", "stop_move", "face", "teleport", "fly", "set",
       "set_stamina", "set_health", "lock_to", "mark", "swing")
SWING = (0, "attacker", "swing")

# Fixture holds must sit strictly between two ladder checkpoints, or the tier they select is a
# coin toss. Sources: GA_Attack's Branches[*].HoldUntilSeconds.
CHECKPOINTS = (0.15, 0.35, 0.75)

# The mechanics a row may claim to cover. The coverage map in Docs/Debug-Instruments.md is
# generated from these, so a claim outside the list fails at load rather than drifting.
MECHANICS = (
    "360 finisher", "acceptance window", "aim wedge", "airborne knockdown", "airborne refusal", "arc",
    "attack total", "blockstun", "buffer expiry", "buffer priority", "cancel into guard", "chain cadence",
    "chain window", "checkpoint", "commitment", "death", "death impulse", "death over knockdown",
    "dodge cost", "dodge direction", "dodge fit", "dodge travel", "escalation", "exhaustion",
    "exhausted regen", "floor invincibility", "get-up attack", "get-up options", "get-up priority",
    "guard break", "guard cost", "guard facing", "guard floor", "guard-break stun", "health damage",
    "health ledger", "height band", "held input", "hit guarantee", "hitstun", "hold tier", "i-frames",
    "knockback spacing", "knockdown spans", "knockdown type", "montage swap", "movement lock",
    "parry catch", "parry facing", "parry grace", "parry lockout", "parry recoil", "parry refusal",
    "parry reward", "parry whiff", "parry window", "reach", "release timing", "revive", "stamina damage",
    "string reset", "tap tier", "two attackers", "waiver", "walk speed", "what buffers",
)


# --- timer-fixture rows -------------------------------------------------------
# The attacker dummy drives the exchange through its Debug knobs, the player is parked out of reach,
# and the row stops when the trace carries its samples -- an until condition, counted by the runner
# off the log -- or at its timeout. _row builds the common shape so an entry is its knobs and stop.


def _row(family, attacker=None, defender=None, until=None, timeout=60.0, duration=None,
         player_spawn=PARKED, player_yaw=0.0, mutations=(), exclude=("drift=",),
         teardown_allow=('Attacking', 'StaminaRegenPaused'), canary=False, covers=()):
    stop = dict(until=tuple(until), timeout=float(timeout)) if until else dict(duration=float(duration))
    return dict(
        family=family, canary=canary, covers=list(covers),
        roles=dict(attacker=PLACED_ATTACKER, defender=PLACED_DEFENDER),
        knobs={"attacker": dict(attacker or {}), "defender": dict(defender or {})},
        player=dict(spawn=player_spawn, yaw=player_yaw, props={}),
        plan=[], stop=stop, expect=dict(),
        mutations=list(mutations), golden=dict(exclude=list(exclude)),
        # States a fixture holds on purpose, so the hygiene check does not read a held guard as a
        # leak. Everything not named here must be gone before the reset runs.
        teardown_allow=list(teardown_allow),
    )


def _periodic(family, plan, mutations, reps=8, period=180, duration=30.0, covers=()):
    """The player repeats one plan on a fixed period, whiffing in open space: a landed hit waives
    commitment and resets the string, a different question from the one such a row asks."""
    return dict(
        family=family, canary=False, covers=list(covers),
        roles=dict(attacker=(PLACED_ATTACKER[0], PARKED_DUMMY, 0.0),
                   defender=(PLACED_DEFENDER[0], PARKED_DUMMY, 0.0)),
        knobs={"attacker": dict(SILENT), "defender": dict(SILENT)},
        player=dict(spawn=(-4000.0, -4000.0, 100.0), yaw=0.0, props={}),
        plan=list(plan), stop=dict(duration=duration),
        expect=dict(reps=reps, period_frames=period),
        mutations=list(mutations), golden=dict(exclude=["drift="]),
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


_TIER = ("release timing", "attack total", "escalation")
SCENARIOS = {

    # --- tier: the ladder's three rungs from position 1, eight swings each. The light swings every
    # 2.2 s; a flooring tier keeps the baseline 3.0 s, since a hit landing during the rise re-floors.
    "tier-light":   _row("tier", _atk(LIGHT, debug_auto_attack_interval=2.2), _def(),
                         until=("ABILITY END", 8), timeout=30,
                         mutations=[("shift", "RELEASE BEGIN", 0.100)], canary=True, covers=_TIER),
    "tier-heavy":   _row("tier", _atk(HEAVY), _def(),
                         until=("ABILITY END", 8), timeout=30,
                         mutations=[("shift", "RELEASE BEGIN", 0.100)], canary=True, covers=_TIER),
    "tier-charged": _row("tier", _atk(CHARGED), _def(),
                         until=("ABILITY END", 8), timeout=30,
                         mutations=[("shift", "RELEASE BEGIN", 0.100)], canary=True, covers=_TIER),

    # --- attack: the cancel into a guard, four times ----------------------------------------------
    "attack-cancel": _row("attack", _atk(LIGHT, debug_cancel_attack_into_block=True), _def(),
                          until=("BLOCK", "cost", 4), timeout=30,
                          mutations=[("drop", "BLOCK      cost", 99)], canary=True,
                          covers=("cancel into guard",)),

    # --- string: four strings at the tapped cadence -----------------------------------------------
    "string-cadence": _row("string", _atk(LIGHT, **STRING), _def(),
                           until=("STRING", "chain out", 8), timeout=60,
                           mutations=[("set", "DAMAGED", "damage", "99")], canary=True,
                           covers=("chain cadence", "health damage", "health ledger", "hitstun",
                                   "knockback spacing")),
    # The 360-degree ender needs a stationary attacker with a body on each side, so the player
    # stands in opposite the defender and the lunge is suppressed. Four stands is three strings.
    "string-finisher-arc": _row("string",
                                _atk(LIGHT, debug_auto_attack_facing_mode="NEVER",
                                     debug_suppress_lunge=True, **STRING),
                                _def(), until=("KNOCKDOWN STAND", 4), timeout=90,
                                player_spawn=(200.0, 150.0, 100.0),
                                mutations=[("drop", "DAMAGED", 1)],
                                teardown_allow=["Attacking", "StaminaRegenPaused", "KnockedDown"],
                                canary=True, covers=("360 finisher", "knockdown type")),

    # --- input: a hold that crosses the actionable frame ------------------------------------------
    # Light 1 whiffing reads 1.000 s, so a press 150 ms before actionable sits at 51 f and is held 15 f
    # across it; the ladder must count all 250 ms and reach the heavy.
    "input-hold-tier": _periodic("input", [(0, "player", "tap", "attack"),
                                           (51, "player", "hold", "attack", 15)],
                                 [("set", "COMMIT", "branch", "0")],
                                 covers=("hold tier", "acceptance window")),

    # --- knockdown: the two types from the dummy's swings, four stands each -----------------------
    "knockdown-normal": _row("knockdown", _atk(LIGHT, **STRING), _def(),
                             until=("KNOCKDOWN STAND", 4), timeout=60,
                             mutations=[("set", "KNOCKDOWN", "type", "hard")], canary=True,
                             covers=("knockdown type", "knockdown spans", "floor invincibility")),
    "knockdown-hard":   _row("knockdown", _atk(HEAVY), _def(),
                             until=("KNOCKDOWN STAND", 4), timeout=60,
                             mutations=[("set", "KNOCKDOWN", "type", "normal")], canary=True,
                             covers=("knockdown type", "knockdown spans", "floor invincibility")),
    # The dummy's own get-up attack, pressed just inside the hard window: the home-at-stand puts the
    # riser back in reach, which is what lets its swing be asserted to land.
    "knockdown-getup-attack": _row("knockdown", _atk(HEAVY), _def(debug_get_up_mode="ATTACK_GET_UP"),
                                   until=("KNOCKDOWN RISE", 4), timeout=60,
                                   mutations=[("shift", "RELEASE BEGIN", 0.150)], canary=True,
                                   covers=("get-up attack",)),
}


# --- scripted rows -----------------------------------------------------------
# The player is the precisely timed actor (D2). Each rep runs one plan -- plans[k % len] for rep k
# -- from the frame a lock_to's tag appears on, and the gate between reps settles, reads the pawn the
# game left, resets it and restores every placement. Assertions live in regression_rows.py.

def _player_defends(family, attacker, plans, mutations, reps, expect=None, tail=60, duration=None,
                    tape_every=2, teardown_allow=("Attacking", "StaminaRegenPaused"), canary=False,
                    covers=(), swing=True):
    """The attacker dummy on open ground, asked for its swing as each rep begins; the player stands
    where the defender stood; the second dummy is parked and silent. swing=False leaves the
    attacker's own loop to time the swings."""
    ex = dict(reps=reps, gate=True, tail_frames=tail)
    ex.update(expect or {})
    return dict(
        family=family, canary=canary, covers=list(covers),
        roles=dict(attacker=(PLACED_ATTACKER[0],) + OPEN_ATTACKER,
                   defender=(PLACED_DEFENDER[0], PARKED_DUMMY, 0.0)),
        knobs={"attacker": dict(attacker), "defender": dict(SILENT)},
        player=dict(spawn=(OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1], 100.0), yaw=OPEN_DEFENDER[1],
                    props={}),
        plans=[([SWING] if swing else []) + list(p) for p in plans], plan=[],
        stop=dict(duration=float(duration or (reps * 8.0 + 20.0))),
        expect=ex, mutations=list(mutations), golden=dict(exclude=["drift="]),
        teardown_allow=list(teardown_allow), tape_every=tape_every,
    )


def _player_alone(family, plans, mutations, reps, expect=None, tail=90, duration=None,
                  target=None, props=None, tape_every=2, defender=None, teardown_allow=(),
                  canary=False, covers=()):
    """Both dummies silent; the player throws in open space, or at a parked target when a row
    places one. defender overrides the target's knobs, for a target that guards."""
    ex = dict(reps=reps, gate=True, tail_frames=tail)
    ex.update(expect or {})
    return dict(
        family=family, canary=canary, covers=list(covers),
        roles=dict(attacker=(PLACED_ATTACKER[0], PARKED_DUMMY, 0.0),
                   defender=(PLACED_DEFENDER[0],) + (target or (PARKED_DUMMY, 0.0))),
        knobs={"attacker": dict(SILENT), "defender": dict(defender or SILENT)},
        player=dict(spawn=(-4000.0, -4000.0, 100.0) if target is None
                    else (OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1], 100.0),
                    yaw=0.0 if target is None else OPEN_DEFENDER[1], props=dict(props or {})),
        plans=[list(p) for p in plans], plan=[],
        stop=dict(duration=float(duration or (reps * 4.0 + 20.0))),
        expect=ex, mutations=list(mutations), golden=dict(exclude=["drift="]),
        teardown_allow=list(teardown_allow), tape_every=tape_every,
    )


LOCK_HITSTUN = (0, "player", "lock_to", "State.Hitstun")
LOCK_DOWN = (0, "player", "lock_to", "State.KnockedDown")
LOCK_ATK = (0, "attacker", "lock_to", "State.Attacking")

# The light's hitstun is 33 f and its hit lands 12 f after the press, so the acceptance window
# opens 21 f into the stun; 19 f is outside it and 23 f inside. Sources: Cells[0].HitstunSeconds,
# Branches[0].ReleaseAtSeconds, InputBufferSeconds.
SCENARIOS["input-accept-hitstun"] = _player_defends(
    "input", _atk(LIGHT),
    plans=[[LOCK_HITSTUN, (19, "player", "tap", "attack")],
           [LOCK_HITSTUN, (23, "player", "tap", "attack")]],
    mutations=[("shift", "HITSTUN END", -0.100)], reps=6, tail=90,
    covers=("acceptance window", "buffer expiry"))

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
    mutations=[("shift", "BLOCKSTUN END", -0.100)], reps=6, tail=90,
    covers=("acceptance window", "buffer expiry"))

# The player throws a light into the defender dummy's parry window and takes the light's 45 f
# lockout; a press at 31 f is outside its acceptance window and one at 35 f inside. Sources:
# Cells[0].ParryLockoutSeconds, InputBufferSeconds.
_INTO_PARRY = [(0, "defender", "lock_to", "State.Parrying"),
               (0, "player", "tap", "attack"),
               (0, "player", "lock_to", "State.ParryLockout")]
SCENARIOS["input-accept-lockout"] = dict(
    family="input", canary=False, covers=["acceptance window", "buffer expiry"],
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
    expect=dict(by=["attack", "auto"], held=["Attack", None], lockout=1.5, auto_at=2.0),
    covers=("get-up options", "exhaustion"))

# The player parries phase-locked to the attacker's string: swing 0's hitbox at 12-21 f, caught by a
# window opened at 6 f; swing 1 activates at 30 f, its hitbox at 42-51 f, caught by one opened at
# 36 f, swing 0 having been blocked so the parrier is not in hitstun when it presses. Alternating
# them lets the string chain once before it is caught, which "chaining resumes" reads, and keeps
# every caught swing on the 0.75 s lockout the row asserts.
SCENARIOS["parry-catch"] = _player_defends(
    "parry", _atk(LIGHT, **STRING),
    plans=[[LOCK_ATK, (6, "player", "tap", "parry")],
           [LOCK_ATK, (2, "player", "press", "block"), (22, "player", "release", "block"),
            (36, "player", "tap", "parry")]],
    mutations=[("drop", "PARRY SUCCESS", 1)], reps=6, tail=120, duration=90.0, canary=True,
    covers=("parry window", "parry catch", "parry grace", "parry recoil", "parry lockout",
            "string reset", "parry reward"))

# A whiffed parry with nothing arriving, then a press into each phase: the window at 9 f, the
# recovery at 27 f, 36 f and 51 f, and a guard at 57 f once the recovery has ended at 54 f.
_WHIFF_PROBE = [(0, "player", "tap", "parry"), (9, "player", "tap", "attack"),
                (27, "player", "tap", "attack"), (36, "player", "tap", "dodge"),
                (51, "player", "tap", "block"), (57, "player", "tap", "block")]
SCENARIOS["parry-whiff"] = _player_alone("parry", plans=[_WHIFF_PROBE],
                                         mutations=[("drop", "PARRY WHIFF", 1)], reps=6, tail=90,
                                         canary=True, covers=("parry whiff",))

# The player jumps the frame the heavy begins and is 0.2 s into the jump when it lands, so every
# other knockdown enters airborne at height; the reps between it stay grounded and give the row the
# floor it measures against.
SCENARIOS["knockdown-airborne"] = _player_defends(
    "knockdown", _atk(HEAVY),
    plans=[[LOCK_ATK, (0, "player", "tap", "jump")], [LOCK_ATK]],
    mutations=[("set", "KNOCKDOWN", "airborne", "0")], reps=8, tail=150, duration=120.0,
    canary=True, covers=("airborne knockdown",))

# The reward's magnitude: the bar is written to 50 the frame the swing begins, so the credit has
# room, and every catch must pay the authored reward in full.
SCENARIOS["parry-reward"] = _player_defends(
    "parry", _atk(LIGHT),
    plans=[[LOCK_ATK, (0, "player", "set_stamina", 50.0), (6, "player", "tap", "parry")]],
    mutations=[("set", "PARRY SUCCESS", "gained", "0.0")], reps=4, tail=90,
    covers=("parry reward",))

# All nine cells from the player's own presses, whiffing in open space. Each plan's last swing is the
# cell under test; the taps before it chain at 31 f and 62 f, inside each chain window.
_T, _H, _C = 13, 51, 51
_CELL_PLANS = [
    [(0, "player", "tap", "attack")],
    [(0, "player", "hold", "attack", _T)],
    [(0, "player", "hold", "attack", _H)],
    [(0, "player", "tap", "attack"), (31, "player", "tap", "attack")],
    [(0, "player", "tap", "attack"), (31, "player", "hold", "attack", _T)],
    [(0, "player", "tap", "attack"), (31, "player", "hold", "attack", _C)],
    [(0, "player", "tap", "attack"), (31, "player", "tap", "attack"), (62, "player", "tap", "attack")],
    [(0, "player", "tap", "attack"), (31, "player", "tap", "attack"), (62, "player", "hold", "attack", _T)],
    [(0, "player", "tap", "attack"), (31, "player", "tap", "attack"), (62, "player", "hold", "attack", _C)],
]
SCENARIOS["tier-cells"] = _player_alone(
    "tier", plans=_CELL_PLANS, mutations=[("shift", "RELEASE BEGIN", 0.100)], reps=18, tail=130,
    expect=dict(cells=[(0, 0), (0, 1), (0, 2), (1, 0), (1, 1), (1, 2), (2, 0), (2, 1), (2, 2)]),
    covers=("release timing", "attack total", "escalation", "tap tier", "hold tier", "montage swap",
            "chain window"))

# The player throws the string at a target 150 cm ahead, three taps at the tapped cadence of 30 f
# (D11): the shipping input path on the attack side, where string-cadence drives the dummy's timer.
# The target stands still, or holds its guard for the blocked form.
_TARGET_AHEAD = ((OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1] - 150.0, 96.0), 90.0)
_STRING_TAPS = [(0, "player", "tap", "attack"), (30, "player", "tap", "attack"),
                (60, "player", "tap", "attack")]
SCENARIOS["string-player-cadence"] = _player_alone(
    "string", plans=[_STRING_TAPS], mutations=[("set", "DAMAGED", "damage", "99")], reps=4,
    tail=180, target=_TARGET_AHEAD, expect=dict(blocked=False),
    covers=("chain cadence", "health damage", "hitstun", "knockback spacing", "knockdown type"))
SCENARIOS["string-player-blocked"] = _player_alone(
    "string", plans=[_STRING_TAPS], mutations=[("set", "BLOCKED", "staminaDamage", "99")], reps=4,
    tail=120, target=_TARGET_AHEAD, defender=_def("HOLD_BLOCK"), expect=dict(blocked=True),
    teardown_allow=("Blocking", "StaminaRegenPaused"),
    covers=("chain cadence", "stamina damage", "blockstun", "knockback spacing"))

# The guarantee, from the victim's side: a dodge pressed 6 f into the light's hitstun is refused
# naming the stun and never fires, and the string's remaining swings land.
SCENARIOS["string-guarantee"] = _player_defends(
    "string", _atk(LIGHT, **STRING),
    plans=[[LOCK_HITSTUN, (6, "player", "tap", "dodge")]],
    mutations=[("drop", "REFUSED", 99)], reps=3, tail=120, canary=True,
    covers=("hit guarantee", "hitstun"))

# Eight directions, each held 12 f before the dodge and through it. X right, Y forward, in the
# camera frame the player's control rotation sets.
_DIRS = [("Fw", 0, 1), ("FR", 1, 1), ("R", 1, 0), ("BR", 1, -1),
         ("Bw", 0, -1), ("BL", -1, -1), ("L", -1, 0), ("FL", -1, 1)]
SCENARIOS["dodge-directions"] = _player_alone(
    "dodge",
    plans=[[(0, "player", "move", float(x), float(y), 40), (12, "player", "tap", "dodge")]
           for _d, x, y in _DIRS],
    mutations=[("set", "DODGE END", "dist", "999.9")], reps=16, tail=60,
    expect=dict(dirs=[d for d, _x, _y in _DIRS]),
    covers=("dodge travel", "dodge fit", "dodge direction"))

# Two dodges from a full bar, half a second apart: the first costs its 50, the second empties the
# bar into exhaustion, and the bar is back at max after the pause and the exhausted regen.
SCENARIOS["dodge-cycle"] = _player_alone(
    "dodge", plans=[[(0, "player", "tap", "dodge"), (30, "player", "tap", "dodge")]],
    mutations=[("set", "DODGE END", "dist", "999.9")], reps=2, tail=330, canary=True,
    covers=("dodge travel", "dodge fit", "dodge cost", "exhaustion", "exhausted regen"))

# I-frames, directly: a dodge opened 9 f into the attacker's swing spans 9-33 f, covering the hitbox
# at 12-21 f, and the swing must run on rather than stop on an evaded body. The control rep does not
# dodge and is hit.
SCENARIOS["dodge-iframes"] = _player_defends(
    "dodge", _atk(LIGHT),
    plans=[[LOCK_ATK, (9, "player", "tap", "dodge")], [LOCK_ATK]],
    mutations=[("drop", "DAMAGED", 99)], reps=6, tail=90, covers=("i-frames",))

# The waiver: the attacker dummy dodges out of its own hit, which its commitment tag would have
# refused, and its movement comes back with the hitstun it inflicted.
SCENARIOS["attack-waiver"] = _player_defends(
    "attack", _atk(LIGHT, debug_dodge_after_hit=True), plans=[[LOCK_ATK]],
    mutations=[("shift", "DODGE      ", 0.500)], reps=3, tail=90, canary=True, covers=("waiver",))


# --- block: the guard's price per tier, from the player's side ------------------------------------
# Three plans per tier: the guard raised two frames into the swing from a full bar, the same raise
# from a bar left with exactly the tier's stamina damage so the block empties it and breaks, and no
# guard at all so the swing lands on health. The exhaustion a break leaves runs to its end inside
# the rep, so the break's regen suppression is measured beside the break.

def _guard(release_at):
    return [(2, "player", "press", "block"), (release_at, "player", "release", "block")]


def _block_row(sid, hold, dmg, mutation, release_at, interval, breaks_always=False):
    cost = 10  # BlockInitialStaminaCost; the bar is set on the press's frame, one tick of regen before the raise
    plans = [[LOCK_ATK] + _guard(release_at)]
    if not breaks_always:
        plans.append([LOCK_ATK, (2, "player", "set_stamina", float(cost + dmg - 2))] + _guard(release_at))
    plans.append([LOCK_ATK])
    SCENARIOS[sid] = _player_defends(
        "block", _atk(hold, debug_auto_attack_interval=interval), plans=plans, mutations=[mutation],
        reps=2 * len(plans), tail=120, canary=True,
        teardown_allow=("Attacking", "StaminaRegenPaused"),
        covers=("stamina damage", "health damage", "health ledger", "guard cost", "guard break",
                "guard-break stun", "blockstun", "exhausted regen"))


# The guard is held past each tier's hit: 12 f, 24 f and 48 f. A flooring tier keeps the baseline
# interval, since a hit landing during the rise re-floors the body.
_block_row("block-light", LIGHT, 5, ("set", "BLOCKED", "staminaDamage", "99"), release_at=30, interval=2.2)
_block_row("block-heavy", HEAVY, 50, ("set", "BLOCKED", "staminaDamage", "99"), release_at=30, interval=3.0)
_block_row("block-charged", CHARGED, 100, ("set", "BLOCKED", "staminaDamage", "1"), release_at=60, interval=3.0,
           breaks_always=True)


# --- death, from the player's side ------------------------------------------------------------------

# Health set to one light's damage as the string begins: the first swing kills, the next two land on
# the corpse, and the revive comes on the debug delay with the settle read at it.
SCENARIOS["death-revive"] = _player_defends(
    "death", _atk(LIGHT, **STRING), plans=[[LOCK_ATK, (0, "player", "set_health", 15.0)]],
    mutations=[("shift", "REVIVE", 1.000)], reps=3, tail=210, canary=True,
    covers=("death", "revive", "death impulse"))

# The heavy grades every swing: on 25 health it kills without flooring, on full health it floors.
SCENARIOS["death-over-knockdown"] = _player_defends(
    "death", _atk(HEAVY),
    plans=[[LOCK_ATK, (0, "player", "set_health", 25.0)], [LOCK_ATK]],
    mutations=[("drop", "DEATH ", 99)], reps=4, tail=210, canary=True,
    covers=("death over knockdown",))


# --- the exhaustion exception, both ways into exhaustion ---------------------------------------------
# A knockdown suppresses regen unless the victim is already exhausted. The first plan exhausts by a
# spend: half a bar, then a dodge as the string begins, which also evades swing 1; swing 3 floors.
# The second exhausts by a break: the bar set so swing 1's block empties it, then swings 2 and 3 land.
# Either way the exhaustion ends on its own arithmetic with the knockdown contributing nothing.
SCENARIOS["knockdown-regen-exception"] = _player_defends(
    "knockdown", _atk(LIGHT, **STRING),
    plans=[[LOCK_ATK, (0, "player", "set_stamina", 50.0), (0, "player", "tap", "dodge")],
           [LOCK_ATK, (2, "player", "set_stamina", 13.0)] + _guard(40)],
    mutations=[("shift", "EXHAUSTION END", 0.500)], reps=4, tail=360, canary=True,
    covers=("exhausted regen", "knockdown spans"))


# --- group C: composition ------------------------------------------------------------------------

# An attack pressed in the air is refused while airborne and, per the spec, not buffered.
SCENARIOS["attack-airborne"] = _player_alone(
    "attack", plans=[[(0, "player", "tap", "jump"), (18, "player", "tap", "attack")]],
    mutations=[("drop", "REFUSED", 99)], reps=3, tail=90, covers=("airborne refusal", "buffer expiry"))

# A dodge pressed in the air is refused while airborne and, per the spec, not buffered.
SCENARIOS["dodge-airborne"] = _player_alone(
    "dodge", plans=[[(0, "player", "tap", "jump"), (18, "player", "tap", "dodge")]],
    mutations=[("drop", "REFUSED", 99)], reps=3, tail=90, covers=("airborne refusal", "buffer expiry"))

# A whiffed light stays committed through its recovery: a dodge pressed at 24 f is refused.
SCENARIOS["attack-whiff-commitment"] = _player_alone(
    "attack", plans=[[(0, "player", "tap", "attack"), (24, "player", "tap", "dodge")]],
    mutations=[("drop", "REFUSED", 99)], reps=3, tail=90, covers=("commitment",))

# The guard is 180 degrees forward in the defender's frame: faced away, the light lands; faced
# toward, it is blocked.
_GUARD_UP = [LOCK_ATK, (2, "player", "press", "block"), (30, "player", "release", "block")]
SCENARIOS["block-facing"] = _player_defends(
    "block", _atk(LIGHT),
    plans=[[(0, "player", "face", 90.0)] + _GUARD_UP, [(0, "player", "face", 270.0)] + _GUARD_UP],
    mutations=[("drop", "DAMAGED", 99)], reps=4, tail=90, expect=dict(away=[True, False]),
    covers=("guard facing",))

# A parry has no facing test: from behind it still catches.
SCENARIOS["parry-facing"] = _player_defends(
    "parry", _atk(LIGHT),
    plans=[[(0, "player", "face", 90.0), LOCK_ATK, (6, "player", "tap", "parry")]],
    mutations=[("drop", "PARRY SUCCESS", 99)], reps=2, tail=90, covers=("parry facing",))

# Refused while blocking, dodging, exhausted and airborne, each with no window opened.
SCENARIOS["parry-refused"] = _player_alone(
    "parry",
    plans=[[(0, "player", "press", "block"), (6, "player", "tap", "parry"), (30, "player", "release", "block")],
           [(0, "player", "tap", "dodge"), (6, "player", "tap", "parry")],
           [(0, "player", "set_stamina", 0.0), (3, "player", "tap", "parry")],
           [(0, "player", "tap", "jump"), (6, "player", "tap", "parry")]],
    mutations=[("drop", "REFUSED", 99)], reps=8, tail=90, covers=("parry refusal",))

# Blockstun disables offense and nothing else: a dodge and a parry pressed inside it fire, an
# attack is refused and buffered.
_INTO_BLOCKSTUN = [LOCK_ATK, (2, "player", "press", "block"),
                   (0, "player", "lock_to", "State.Blockstun"), (1, "player", "release", "block")]
SCENARIOS["block-stun-offense-only"] = _player_defends(
    "block", _atk(LIGHT),
    plans=[_INTO_BLOCKSTUN + [(6, "player", "tap", "dodge")],
           _INTO_BLOCKSTUN + [(6, "player", "tap", "parry")],
           _INTO_BLOCKSTUN + [(6, "player", "tap", "attack")]],
    mutations=[("drop", "DODGE", 99)], reps=6, tail=120, covers=("blockstun",))

# A release inside the guard's floor is remembered and applied when the floor ends; one after it
# drops the guard at once. Source: MinimumBlockSeconds, 15 f.
SCENARIOS["block-commitment"] = _player_alone(
    "block",
    plans=[[(0, "player", "press", "block"), (6, "player", "release", "block")],
           [(0, "player", "press", "block"), (24, "player", "release", "block")]],
    mutations=[("regex", r"BLOCK      down on (\S+) \(released\)", r"BLOCK      down on \1 (cancelled)")],
    reps=6, tail=60, expect=dict(down_at=[15, 24]), covers=("guard floor",))

# One buffer slot, last press wins: an attack then a dodge inside hitstun's acceptance window.
SCENARIOS["input-last-wins"] = _player_defends(
    "input", _atk(LIGHT),
    plans=[[LOCK_HITSTUN, (24, "player", "tap", "attack"), (27, "player", "tap", "dodge")]],
    mutations=[("drop", "BUFFER", 99)], reps=3, tail=90, covers=("buffer priority",))

# A parry never buffers: pressed inside hitstun it is refused and no window opens afterwards.
SCENARIOS["input-parry-never-buffers"] = _player_defends(
    "input", _atk(LIGHT),
    plans=[[LOCK_HITSTUN, (25, "player", "tap", "parry")]],
    mutations=[("drop", "REFUSED", 99)], reps=3, tail=90, covers=("what buffers",))

# Block buffers actions, not states: a tap inside hitstun raises nothing afterwards, a hold does.
SCENARIOS["input-block-never-replays"] = _player_defends(
    "input", _atk(LIGHT),
    plans=[[LOCK_HITSTUN, (25, "player", "tap", "block")],
           [LOCK_HITSTUN, (25, "player", "hold", "block", 40)]],
    mutations=[("drop", "BLOCK", 99)], reps=6, tail=90, covers=("what buffers",))

# Death in mid-air leaves nothing stranded: the corpse revives and walks.
SCENARIOS["death-midair"] = _player_defends(
    "death", _atk(LIGHT),
    plans=[[LOCK_ATK, (0, "player", "set_health", 15.0), (0, "player", "tap", "jump"),
            (240, "player", "move", 0.0, 1.0, 40)]],
    mutations=[("drop", "DEATH", 99)], reps=2, tail=90, tape_every=1,
    teardown_allow=("Attacking", "StaminaRegenPaused"), covers=("death", "revive"))

# --- group D: edges -------------------------------------------------------------------------------
# Two reps per probe. The sides must produce their outcomes every time; the probes on and beside a
# threshold are reported, since which side they fall on is a ruling and the frame after a timer's
# deadline is a race. Frames are 1/60.

def _edge(family, sid, plans, probe, want, mutation, reps=None, tail=100, covers=()):
    SCENARIOS[sid] = _player_alone(
        family, plans=plans, mutations=[mutation], reps=reps or 2 * len(plans), tail=tail,
        expect=dict(probe=probe, want=want, labels=[str(p[-1][0]) + "f" if probe != "commit"
                                                    else "%df" % p[-1][4] for p in plans]),
        covers=covers)


_edge("edge", "edge-heavy-checkpoint",
      [[(0, "player", "hold", "attack", h)] for h in (20, 23, 21, 22)],
      "commit", ["1", "2", None, None], ("set", "COMMIT", "branch", "0"), covers=("checkpoint",))

# The chain window as the spec has it: a second press continues the string from 0.283 to 0.683
# after the first, 17 f to 41 f, carried to the 29 f opening or accepted at once; 16 f and 42 f
# fall outside. Past the close a press is stored and expires unless the swing's end, at 57 to 60 f,
# falls inside its 12 f acceptance, which fires it fresh.
_edge("edge", "edge-chain-open",
      [[(0, "player", "tap", "attack"), (f, "player", "tap", "attack")] for f in (16, 17, 18, 19, 20)],
      "chain", ["none", "chain", None, None, None], ("drop", "STRING", 99), covers=("chain window",))

_edge("edge", "edge-chain-close",
      [[(0, "player", "tap", "attack"), (f, "player", "tap", "attack")] for f in (41, 42, 39, 40, 43)],
      "chain", ["chain", "none", None, None, None], ("drop", "STRING", 99), covers=("chain window",))

# Past the close a press is stored and expires unless its 12 f acceptance reaches the 58 f actionable
# frame: 45 f expires, 46 f fires fresh.
_edge("edge", "edge-fresh-open",
      [[(0, "player", "tap", "attack"), (f, "player", "tap", "attack")]
       for f in (45, 46, 44, 47, 48)],
      "chain", ["none", "fresh", None, None, None], ("dup", "ACTIVATE", 1),
      covers=("acceptance window", "buffer expiry", "tap tier"))

_edge("edge", "edge-guard-floor",
      [[(0, "player", "press", "block"), (f, "player", "release", "block")] for f in (14, 16, 15)],
      "guard", ["15", "16", None], ("regex", r"BLOCK      down on (\S+) \(released\)",
                                    r"BLOCK      down on \1 (cancelled)"), covers=("guard floor",))

# --- group G: the movement locks, off the position tape ------------------------------------------
# A held move against a control that holds none: the two trajectories must match through the
# state, and the held one must walk within six frames of the state's end.

_MOVE = (0, "player", "move", 0.0, 1.0, 120)

SCENARIOS["lock-hitstun"] = _player_defends(
    "lock", _atk(LIGHT), plans=[[LOCK_HITSTUN, _MOVE], [LOCK_HITSTUN]],
    mutations=[("drop", "HITSTUN", 99)], reps=4, tail=100, tape_every=1,
    expect=dict(start="HITSTUN", end="HITSTUN END"), covers=("movement lock",))

SCENARIOS["lock-knockdown"] = _player_defends(
    "lock", _atk(HEAVY, debug_auto_attack_interval=4.5),
    plans=[[LOCK_DOWN, (0, "player", "move", 0.0, 1.0, 160)], [LOCK_DOWN]],
    mutations=[("drop", "KNOCKDOWN STAND", 99)], reps=4, tail=170, tape_every=1,
    expect=dict(start="KNOCKDOWN", end="KNOCKDOWN STAND"), covers=("movement lock",))

SCENARIOS["lock-attack-recovery"] = _player_alone(
    "lock", plans=[[(0, "player", "move", 0.0, 1.0, 110), (12, "player", "tap", "attack")],
                   [(12, "player", "tap", "attack")]],
    mutations=[("drop", "ABILITY END", 99)], reps=4, tail=100, tape_every=1,
    expect=dict(start="ACTIVATE", end="ABILITY END"), covers=("movement lock",))

# A whiffed parry locks movement across its window and recovery; nothing else moves the pawn, so
# the held move must show no displacement at all until the recovery ends.
SCENARIOS["lock-parry"] = _player_alone(
    "lock", plans=[[(0, "player", "tap", "parry"), (0, "player", "move", 0.0, 1.0, 90)]],
    mutations=[("drop", "PARRY RECOVERY END", 99)], reps=3, tail=90, tape_every=1,
    expect=dict(start="PARRY WINDOW", end="PARRY RECOVERY END", still=True), covers=("movement lock",))

# Blockstun does not lock movement: the held move must displace beyond the control's by the stun's
# end, at the guard's walking speed.
SCENARIOS["lock-blockstun-free"] = _player_defends(
    "lock", _atk(LIGHT),
    plans=[_INTO_BLOCKSTUN[:3] + [(0, "player", "move", 0.0, 1.0, 40)], _INTO_BLOCKSTUN[:3]],
    mutations=[("drop", "BLOCKSTUN END", 99)], reps=4, tail=100, tape_every=1,
    expect=dict(start="BLOCKSTUN", end="BLOCKSTUN END", free=True), covers=("movement lock",))

# Speed caps, read as steady-state travel over frames 30 to 60 of a held move: the guard's
# BlockingMaxWalkSpeed and the exhausted ExhaustedMaxWalkSpeed, against the mirror.
SCENARIOS["lock-block-speed"] = _player_alone(
    "lock", plans=[[(0, "player", "press", "block"), (0, "player", "move", 0.0, 1.0, 90)]],
    mutations=[("drop", "BLOCK", 99)], reps=3, tail=60, tape_every=1,
    expect=dict(speed="BlockingMaxWalkSpeed", tag="BLOCK"), covers=("walk speed",))
SCENARIOS["lock-exhausted-speed"] = _player_alone(
    "lock", plans=[[(0, "player", "set_stamina", 0.0), (0, "player", "move", 0.0, 1.0, 90)]],
    mutations=[("drop", "EXHAUSTED", 99)], reps=3, tail=60, tape_every=1,
    expect=dict(speed="ExhaustedMaxWalkSpeed", tag="EXHAUSTED"), covers=("walk speed",))

# Hard knockdown: lockout 90 f, window 30 f, auto-rise at 120 f. Each variant holds from 30 f into
# the lockout until past the rise. Priority is guard, dodge, attack, stand (KnockdownGetUpPriority).
# The dodge variants read the kip-up's cost, span and travel; the block variants the guard's gap
# from the rise; the jump variant the refusal that names hard's missing stand.
_HARD_HOLDS = [["block"], ["dodge"], ["attack"], ["jump"],
               ["block", "dodge"], ["dodge", "attack"], ["attack", "jump"]]
SCENARIOS["knockdown-getup-held"] = _player_defends(
    "knockdown", _atk(HEAVY),
    plans=[[LOCK_DOWN] + [(30, "player", "hold", h, 90) for h in hs] for hs in _HARD_HOLDS],
    mutations=[("set", "KNOCKDOWN RISE", "by", "auto")], reps=14, tail=120,
    expect=dict(by=["block", "kipup", "attack", "auto", "block", "kipup", "attack"],
                held=["Block", "Dodge", "Attack", None, "Block", "Dodge", "Attack"],
                lockout=1.5, auto_at=2.0, hard=True),
    covers=("get-up options", "get-up priority", "held input", "knockdown spans"))

# Both get-up paths at once: block held from 30 f, attack tapped at 88 f inside the buffer's reach of
# the 90 f open. The rise comes at the open and the held option takes it.
SCENARIOS["knockdown-getup-tap-priority"] = _player_defends(
    "knockdown", _atk(HEAVY, debug_auto_attack_interval=4.5),
    plans=[[LOCK_DOWN, (30, "player", "hold", "block", 90), (88, "player", "tap", "attack")]],
    mutations=[("set", "KNOCKDOWN RISE", "by", "attack")], reps=3, tail=90,
    expect=dict(lockout=1.5, by="block"), covers=("get-up priority", "held input"))

# Normal knockdown from the string's ender: lockout 60 f. The stand is legal here, all four held
# together must still yield the guard, and the dodge alone is the roll: priced, i-framed, travelling.
SCENARIOS["knockdown-getup-held-normal"] = _player_defends(
    "knockdown", _atk(LIGHT, **STRING),
    plans=[[LOCK_DOWN, (30, "player", "hold", "jump", 60)],
           [LOCK_DOWN] + [(30, "player", "hold", h, 60) for h in ("block", "dodge", "attack", "jump")],
           [LOCK_DOWN, (30, "player", "hold", "dodge", 60)]],
    mutations=[("set", "KNOCKDOWN RISE", "by", "auto")], reps=6, tail=120,
    expect=dict(by=["stand", "block", "dodge"], held=["Jump", "Block", "Dodge"], lockout=1.0,
                auto_at=2.0, hard=False),
    covers=("get-up options", "get-up priority", "held input", "knockdown spans", "dodge cost",
            "dodge travel", "i-frames"))

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
    expect=dict(holds=[8, 11, 9, 10], want=[0, 1, None, None]), covers=("checkpoint",))

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
    mutations=[("drop", "REFUSED", 99)], reps=4, tail=100, tape_every=1, covers=("movement lock",))

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
    expect=dict(hits=[True, False]), covers=("reach",))


# --- group E: two attackers ----------------------------------------------------------------------

def _two_attackers(family, first, second, second_at, plans, mutations, reps, expect=None, tail=60,
                   tape_every=2, covers=()):
    """Both dummies attack with their loops running, never reset between reps, so intervals 0.1 s
    apart put their swings 6 f further apart each rep. The first stands where the attacker stands;
    the second where the row puts it."""
    row = _player_defends(family, first, plans, mutations, reps, expect=expect, tail=tail,
                          tape_every=tape_every, covers=covers, swing=False)
    row["roles"]["defender"] = (PLACED_DEFENDER[0],) + second_at
    row["knobs"]["defender"] = dict(second)
    return row


# The first light is caught at 6 f; the second attacker's contact lands 6 f later per rep. Grace is
# 9 f from the catch, so rep 1 is caught by grace and every later rep is hit.
SCENARIOS["parry-grace-catch"] = _two_attackers(
    "parry", _atk(LIGHT), _atk(LIGHT, debug_auto_attack_interval=3.1),
    second_at=((OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1] + 150.0, 96.0), 270.0),
    plans=[[LOCK_ATK, (6, "player", "tap", "parry")]],
    mutations=[("set", "PARRY SUCCESS", "by", "window")], reps=6, tail=90,
    covers=("parry grace", "two attackers"))

# The first heavy knocks the player down and carries it 450 cm to where the second stands 150 cm
# past; the second's contact lands 0.3 s later per rep, mid-carry in rep 1 and into the down after.
SCENARIOS["knockdown-floor-per-body"] = _two_attackers(
    "knockdown", _atk(HEAVY, debug_auto_attack_interval=4.5),
    _atk(HEAVY, debug_auto_attack_interval=4.8),
    second_at=((OPEN_ATTACKER[0][0], OPEN_ATTACKER[0][1] + 600.0, 96.0), 270.0),
    plans=[[LOCK_ATK]], mutations=[("set", "KNOCKDOWN RISE", "by", "attack")], reps=5, tail=30,
    covers=("floor invincibility", "two attackers"))


# --- group F: geometry edges ----------------------------------------------------------------------

def _at_bearing(deg, dist=150.0, dz=0.0):
    """A target dist from the player's open-ground spot, deg to the right of its facing, turned to
    face it."""
    a = math.radians(deg)
    x0, y0 = OPEN_DEFENDER[0][0], OPEN_DEFENDER[0][1]
    return ((x0 + dist * math.sin(a), y0 - dist * math.cos(a), 96.0 + dz), 90.0 + deg)


def _reach_row(sid, places, hits, labels, props=None, fly=False, covers=()):
    plans = []
    for loc, yaw in places:
        plan = [(0, "player", "face", OPEN_DEFENDER[1])]
        if fly:
            plan.append((0, "defender", "fly", True))
        plan += [(0, "defender", "teleport", loc, yaw), (6, "player", "tap", "attack")]
        plans.append(plan)
    SCENARIOS[sid] = _player_alone(
        "reach", plans=plans, mutations=[("drop", "DAMAGED", 99)], reps=2 * len(plans), tail=90,
        target=places[0], props=dict(props or {}), expect=dict(hits=list(hits), labels=list(labels)),
        covers=covers)


# The arc is 60 wide and a body of radius 42 at 150 cm subtends 16 more, so the transition sits near
# 46 off centre: 28 is inside on either convention, 60 outside on both.
_ARC_DEG = (28, 60, 32, 40, 44, 48, 52)
_reach_row("reach-arc", [_at_bearing(d) for d in _ARC_DEG],
           [True, False, None, None, None, None, None], ["%d deg" % d for d in _ARC_DEG],
           props={"debug_suppress_lunge": True}, covers=("arc",))

# The band is 70 either side of the attacker; the target flies at the offset, above only, since a
# capsule teleported into the floor is pushed out of it. 65 is inside on either convention, 170
# outside on both. A flying target has no gravity, so a hit carries it sideways and it stays up; the
# row reads only whether the hit landed.
_HEIGHTS = (65, 170, 75, 100, 130, 150)
_reach_row("reach-height", [_at_bearing(0, dz=h) for h in _HEIGHTS],
           [True, False, None, None, None, None], ["+%d cm" % h for h in _HEIGHTS],
           props={"debug_suppress_lunge": True}, fly=True, covers=("height band",))

# The aim wedge is 40 wide and, like reach and the arc, measured to the body, so a target at 150 cm
# is selected out to about 36 off centre: 15 is named and the lunge turned onto it, 45 leaves no
# candidate, and the bearings between are reported. The hit itself is reported throughout.
_WEDGE_DEG = (15, 45, 25, 30, 35, 40)
SCENARIOS["reach-aim-wedge"] = _player_alone(
    "reach",
    plans=[[(0, "player", "face", OPEN_DEFENDER[1]), (0, "defender", "teleport") + _at_bearing(d),
            (6, "player", "tap", "attack")] for d in _WEDGE_DEG],
    mutations=[("drop", "AIM ASSIST", 99)], reps=2 * len(_WEDGE_DEG), tail=90, target=_at_bearing(15),
    expect=dict(degrees=list(_WEDGE_DEG)), covers=("aim wedge",))


# Where the light acquires a target against where it lands, with the lunge on: the wedge's reach is
# base lunge + branch lunge + damage reach + margin, the hit lands after the travel. Both measured to
# the body, the two boundaries should sit AimAssistMarginCm apart. The near and far probes are the
# sides; the rest report.
_GAP_CM = (470, 700, 510, 530, 550, 570, 590, 600, 620, 640, 660, 680)
SCENARIOS["reach-aim-gap"] = _player_alone(
    "reach",
    plans=[[(0, "player", "face", OPEN_DEFENDER[1]), (0, "defender", "teleport") + _at_bearing(0, dist=d),
            (6, "player", "tap", "attack")] for d in _GAP_CM],
    mutations=[("drop", "AIM ASSIST", 99)], reps=2 * len(_GAP_CM), tail=120, target=_at_bearing(0, dist=470),
    expect=dict(distances=list(_GAP_CM)), tape_every=1, covers=("aim wedge", "reach"))


# --- the remaining edges -------------------------------------------------------------------------

def _edge_defends(sid, attacker, plans, probe, want, mutation, tail=100, covers=()):
    SCENARIOS[sid] = _player_defends(
        "edge", attacker, plans=plans, mutations=[mutation], reps=2 * len(plans), tail=tail,
        expect=dict(probe=probe, want=want, labels=["%df" % p[-1][0] for p in plans]), covers=covers)


# The swing ends on its authored 57 f and a stored press fires the tick after, so 58 f is the first
# frame a press fires on its own; a press at 57 is held one frame.
_edge("edge", "edge-actionable",
      [[(0, "player", "tap", "attack"), (f, "player", "tap", "attack")]
       for f in (57, 58, 54, 55, 56)],
      "fire", ["held", "now", None, None, None], ("drop", "ACTIVATE", 99),
      covers=("acceptance window", "tap tier"))

# Hitstun is 33 f on the light: a press 12 f or less before its end is carried to HITSTUN END, an
# earlier one expires.
_edge_defends("edge-hitstun-accept", _atk(LIGHT),
              [[LOCK_HITSTUN, (f, "player", "tap", "attack")] for f in (18, 24, 20, 21, 22)],
              "buffer", ["expired", "fired", None, None, None], ("drop", "BUFFER", 99),
              covers=("acceptance window", "buffer expiry"))

# The parry window is 18 f from the press and the light lands 13 f after its activation: pressed at
# 10 f the window covers the contact, at 16 f it opens after it.
_edge_defends("edge-parry-close", _atk(LIGHT),
              [[LOCK_ATK, (f, "player", "tap", "parry")] for f in (10, 16, 12, 13, 14)],
              "parry", ["caught", "hit", None, None, None], ("drop", "PARRY SUCCESS", 99),
              covers=("parry window",))

# The hard knockdown's lockout is 90 f: a get-up attack tapped inside it is refused naming the
# lockout and waits for the input window to open; one tapped after the open rises on its own frame.
_edge_defends("edge-lockout-end", _atk(HEAVY, debug_auto_attack_interval=4.5),
              [[LOCK_DOWN, (f, "player", "tap", "attack")] for f in (87, 93, 89, 90, 91)],
              "lockout", ["later", "now", None, None, None], ("drop", "KNOCKDOWN RISE", 99), tail=60,
              covers=("acceptance window", "knockdown spans"))

# The heavy cannot chain, so a press in its recovery is stored and expires at 12 f unless the
# swing's end at 63 f falls inside that, which fires it fresh; a tap during its release, at 30 f,
# expires the same way.
_edge("edge", "edge-recovery-accept",
      [[(0, "player", "hold", "attack", 13), (f, "player", "tap", "attack")]
       for f in (48, 58, 30, 51, 52, 53, 54, 55, 56)],
      "buffer", ["expired", "fired", "expired", None, None, None, None, None, None], ("drop", "BUFFER", 99),
      covers=("acceptance window", "buffer expiry"))


# --- the parry's lockout per cell ------------------------------------------------------------------

# The attacker's lockout after each caught cell: the string's three lights, each caught 6 f before
# its hitbox opens with the swings before it blocked, then the heavy and the charged.
_BLOCK_0 = [(2, "player", "press", "block"), (22, "player", "release", "block")]
_BLOCK_01 = [(2, "player", "press", "block"), (52, "player", "release", "block")]
SCENARIOS["parry-lockout-light"] = _player_defends(
    "parry", _atk(LIGHT, **STRING),
    plans=[[LOCK_ATK, (6, "player", "tap", "parry")],
           [LOCK_ATK] + _BLOCK_0 + [(36, "player", "tap", "parry")],
           [LOCK_ATK] + _BLOCK_01 + [(67, "player", "tap", "parry")]],
    mutations=[("set", "PARRY LOCKOUT", "until", "0.000")], reps=6, tail=120, covers=("parry lockout",))
SCENARIOS["parry-lockout-heavy"] = _player_defends(
    "parry", _atk(HEAVY), plans=[[LOCK_ATK, (20, "player", "tap", "parry")]],
    mutations=[("set", "PARRY LOCKOUT", "until", "0.000")], reps=3, tail=120, covers=("parry lockout",))
SCENARIOS["parry-lockout-charged"] = _player_defends(
    "parry", _atk(CHARGED), plans=[[LOCK_ATK, (44, "player", "tap", "parry")]],
    mutations=[("set", "PARRY LOCKOUT", "until", "0.000")], reps=3, tail=150, covers=("parry lockout",))


# --- validation -------------------------------------------------------------

_MIRROR = {}


def _mirror(obj, prop):
    """A value off Docs/Combat-Values.tsv, for the lints that need the authored numbers."""
    if not _MIRROR:
        path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                            "Docs", "Combat-Values.tsv")
        for line in open(path, encoding="utf-8"):
            parts = line.rstrip("\n").split("\t")
            if len(parts) == 3 and not line.startswith("#"):
                _MIRROR[(parts[0], parts[1])] = parts[2]
    return _MIRROR[(obj, prop)]


def _branch_of(hold):
    return min(2, sum(1 for c in CHECKPOINTS if hold > c))


def _fixture_lints(sid, s):
    """Faults the first run of a fixture would find: a guard released before the tier's hit lands,
    and a flooring tier fed faster than the knockdown it causes."""
    problems = []
    atk = knobs_for(sid, "attacker")
    if not atk.get("debug_auto_attack"):
        return problems
    branch = _branch_of(float(atk["debug_auto_attack_hold_seconds"]))
    hit_frame = int(round(float(_mirror("GA_Attack", "Branches[%d].ReleaseAtSeconds" % branch)) * 60))
    floors = branch >= 1 or int(atk.get("debug_auto_attack_string_taps", 1)) >= 3
    if floors:
        down = (float(_mirror("BP_TrainingDummy", "KnockdownLockoutSecondsHard"))
                + float(_mirror("BP_TrainingDummy", "KnockdownInputWindowSecondsHard"))
                + float(_mirror("BP_TrainingDummy", "KnockdownRiseSeconds")))
        if float(atk.get("debug_auto_attack_interval", 3.0)) < down + 0.4:
            problems.append("attacker floors on a %.1f s interval, inside the %.1f s knockdown it causes"
                            % (float(atk.get("debug_auto_attack_interval", 3.0)), down))
    for plan in s.get("plans") or []:
        on_swing, press_at = False, None
        for step in plan:
            op = step[2]
            if op == "swing" or (op == "lock_to" and step[3] == "State.Attacking"):
                on_swing, press_at = True, None
            elif op == "lock_to":
                on_swing = False
            elif on_swing and op == "press" and step[3] == "block":
                press_at = step[0]
            elif on_swing and op == "release" and step[3] == "block" and press_at is not None:
                if press_at <= hit_frame and step[0] < hit_frame + 2:
                    problems.append("guard pressed at %d f and released at %d f, before the tier's hit at %d f"
                                    % (press_at, step[0], hit_frame))
    return problems


def validate(known_knobs=None, known_actors=None, resolve_action=None):
    """Every check that can fail before PIE. Returns a list of problems, empty when sound.

    known_knobs, known_actors and resolve_action come from the editor when the runner calls this
    and are skipped outside it, so the orchestrator can still lint shape offline.
    """
    problems = []
    for sid, s in sorted(SCENARIOS.items()):
        where = "%s: " % sid
        for key in ("family", "roles", "knobs", "player", "plan", "stop", "expect", "covers"):
            if key not in s:
                problems.append(where + "missing '%s'" % key)
        if not s.get("mutations"):
            problems.append(where + "no mutation -- every row carries one that must turn it red")
        if not s.get("covers"):
            problems.append(where + "covers nothing -- name the mechanics the row asserts")
        for c in s.get("covers", []):
            if c not in MECHANICS:
                problems.append(where + "covers '%s', which is not in MECHANICS" % c)
        if not isinstance(s.get("canary", False), bool):
            problems.append(where + "canary must be a bool")

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
            if op == "swing" and actor == "player":
                problems.append(where + "swing names a dummy, not the player")
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

        for p in _fixture_lints(sid, s):
            problems.append(where + p)

        stop = s.get("stop", {})
        if "duration" not in stop and "until" not in stop:
            problems.append(where + "stop names neither a duration nor an until")
        if "until" in stop:
            u = stop["until"]
            if not (isinstance(u, tuple) and len(u) in (2, 3) and isinstance(u[-1], int) and u[-1] > 0
                    and all(isinstance(x, str) for x in u[:-1])):
                problems.append(where + "until must be (tag, n) or (tag, token, n)")
            if "timeout" not in stop:
                problems.append(where + "an until needs a timeout")
    return problems


def by_family(family):
    return sorted(k for k, v in SCENARIOS.items() if v["family"] == family)


def canaries():
    return sorted(k for k, v in SCENARIOS.items() if v.get("canary"))


def knobs_for(sid, role):
    """The full knob set for one role: BASELINE with the scenario's overrides merged over it."""
    merged = dict(BASELINE)
    merged.update(SCENARIOS[sid].get("knobs", {}).get(role, {}))
    return merged
