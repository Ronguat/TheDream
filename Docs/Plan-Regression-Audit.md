# Plan — Regression Audit

**Delete this file when the slice ships**, as every plan before it did (`Verification-Plan.md`,
`Plan-Parry.md`, `Plan-Knockdown.md`, `Plan-Animation.md`). Until then it is the executing
session's authority: the designer approves it, execution follows it without further decisions, and
a session resuming after a pause or a context summary picks up from §0. Anything durable it
contains routes out at delivery by `CLAUDE.md`'s "When a slice ships" table.

**Objective.** An autonomous regression loop that covers the maximum interaction surface in the
minimum wall time without losing accuracy, and a growth mechanism where a new row costs a data
entry and a few assertion lines rather than a C++ knob and bespoke awk.

## 0. Resume state

Ticked as units land, with the commit hash. *Verified* means the unit ran and its check passed;
*written* means it exists and has not.

- [ ] C1 — C++: `UTDTimeTools`, key-level and 2D injection, pawn names on eleven trace lines; extractor and doc updates
- [ ] C2 — `scenarios.py` schema, baseline knobs, key resolution, the runner; three rows driven end to end
- [ ] C3 — orchestrator, marker slicing, `--bands-check` with the format lint
- [ ] C4 — universal invariants, mutations, frames, the frame ledger, `summary.json`, `history.tsv`
- [ ] C5 — all 38 rows ported under the new names; fixed-step matrix, repeatability pair, real-time matrix; `ue_s8_driver.py` retired
- [ ] C6 — Phase 1 docs
- [ ] Phase 2 exemplars: `input-accept-hitstun`, `knockdown-getup-held`, `edge-light-checkpoint`, `lock-guard-break`, `reach-light`
- [ ] Phase 2 rows, in §5 order, one commit per row or family
- [ ] Phase 3 — traps, entry, generated matrix, closedown procedure, roster, baseline, memory, this file deleted

## 1. Decisions taken, so nothing below is re-litigated

The designer, 2026-09-02 and 2026-09-03.

| | Decision |
|---|---|
| D1 | A fixed 1/60 s step is the loop's clock. Real time stays a flag. Closedown runs the matrix at fixed step and `tier-*` plus `string-cadence` at real time as the canary. Assumes 60 fps is the reference frame rate. |
| D2 | The scripted player pawn is the precisely timed actor. Dummies stay periodic attackers or inert targets. No second local player. |
| D3 | Every trace line names its pawn immediately after its tag. |
| D4 | Reps are 3 to 5, with exact counts wherever the fixture is deterministic. |
| D5 | `Tools/RegressionCheck/scenarios.py` is the fixture authority. The scenario matrix in `Docs/Debug-Instruments.md` is generated from it and the checker's labels. |
| D6 | Every row in §5 is in scope. Geometry edges are mechanism verification and belong here. The boundary family asserts consistency and reports which side each threshold falls on; the designer rules on the sides after reading the report. |
| D7 | No delegation. One session executes, commits in verified units, and backgrounds long runs so a usage pause does not lose them. |
| D8 | The editor belongs to the session for the whole slice. It closes once, for the rebuild. |
| D9 | Scenario ids are `family-rule[-variant]`, the family a mechanic. The `s[#]` scheme retires at the port with a bridge row in "Retired names". |

Frame vocabulary is adopted: spans print in frames beside seconds, and each scenario reports a
frame ledger (§4.5), as a readout rather than an assertion.

## 2. Baseline, measured before planning

| Measured 2026-09-02/03 | Value |
|---|---|
| PIE session per scenario today (24 sessions, one editor run) | mean 216 s, typical 150 to 210 |
| Engine cost of a PIE start, request to `Bringing World`, and of a stop | 0.10 s and 0.09 s |
| Checker per scenario; editor Python round trip | 1.8 s; 0.65 s |
| PIE frame rate, `t.MaxFPS` 0, VSync off, game time equal to wall time | 100 to 120 fps, hitches to 4 |
| Full matrix at today's shape | about 95 min of PIE plus every agent round trip |
| Last full UBT build of the editor target | 27 s |

What the loop was built against and no longer holds:

- **The clock is scriptable** *(C++ headers, 2026-09-03)*. `UEngine::UpdateTimeAndHandleMaxTickRate`
  reads `FApp::IsBenchmarking() || FApp::UseFixedTimeStep()` every tick under
  `WITH_FIXED_TIME_STEP_SUPPORT`, which `TargetRules.bWithFixedTimeStepSupport` defaults to true.
  `FApp::SetUseFixedTimeStep` and `FApp::SetFixedDeltaTime` are public statics. Only the command line
  sets them today; no console variable and no Python symbol does *(Python, 2026-09-03: `dir(unreal)`
  carries no fixed-step name)*, so the route is a function library in `TheDreamEditor`.
- **PIE is scriptable from inside the editor** *(Python, 2026-09-03)*. `LevelEditorSubsystem` exposes
  `editor_request_begin_play`, `editor_request_end_play` and `is_in_play_in_editor`. Begin play uses
  the first active level viewport and takes no start transform, and the level holds two
  `PlayerStart`s, so the runner teleports the pawn after spawn. `unreal.log_flush` exists and the live
  log is share-readable from in-editor Python.
- **Input can enter at the key level** *(C++ headers, 2026-09-03)*. `APlayerController::InputKey(const
  FInputKeyEventArgs&)` is `ENGINE_API`, `UEnhancedPlayerInput` overrides it, and it is what the game
  viewport calls — so a wrapper reproduces the shipping path below the viewport: key state, the
  mapping context, its modifiers, the action's triggers, the binding. Today's `UTDInputTools` enters
  one layer lower, at the action, bypassing the mapping. The mappings are readable *(Python,
  2026-09-03)*: attack `LeftMouseButton`, block `RightMouseButton`, dodge `LeftShift`, parry
  `ThumbMouseButton`, jump `SpaceBar`, move `W A S D` with swizzle and negate modifiers; `IA_Attack`
  carries an action-level `InputTriggerDown`.
- **Directions are one overload away.** `IA_Move` is a 2D axis with X right and Y forward.
- **Eleven trace tags carry no pawn name**: `ABILITY END`, `AIM ASSIST`, `BUFFER`, `COIL START`,
  `COMMIT`, `DODGE`, `DODGE END`, `ESCALATE`, `MONTAGE`, `RELEASE` and its `OFF`, `BEGIN` and `END`
  variants, `STRING chain out`.
- **Background throttling is not in play**: PIE was exempted in the project settings weeks ago and
  it has held *(the designer, 2026-09-03)*. Not re-tested here.

## 3. Conventions that bind every later step

- A trace line is `[t] TAG  <Actor> fields…`, the actor immediately after the tag, as `ACTIVATE` and
  `KNOCKDOWN` already do. A lint over the source's format strings enforces it (§4.5).
- Scenario ids are `family-rule[-variant]`: lowercase, hyphenated, the family a mechanic, no numbers
  unless the number is the meaning, never a value, so a retune renames nothing. Families and their
  order: `tier`, `attack`, `string`, `chain`, `input`, `block`, `dodge`, `parry`, `knockdown`,
  `death`, `lock`, `edge`, `reach`. The family is what `--family` filters and what the generated
  matrix groups by.
- A scenario is one entry: id, family, roles, knobs, player spawn, plan, stop condition, expected
  counts, mutations. Knobs are the full set from a baseline, never a delta. Plans name actions, never
  keys; the runner resolves keys from the mapping contexts at load.
- The player's input enters at the key level by default. Action-level injection stays as the
  fallback and as the route for a plan that needs a value no key produces.
- A rep begins only when the player reads stamina at max, no ability active and its placement
  restored, so every rep of a deterministic plan is identical.
- New rows assert in Python (`regression_eval.py`); the 38 legacy rows keep their bash assertions.
  A legacy row migrates only when it is being changed for another reason.
- Bands mirror the CDO and name the `Combat-Values.tsv` row they mirror.
- n=0 fails; scope by the pawn named on the roles line; select samples by position, never by the
  value under test; every row carries at least one mutation that must turn it red.
- The only gameplay-module change is the trace names. A red that looks like a defect stops the
  line and is reported; nothing is fixed on the way past.
- Commits are the units in §4.8 and one per row or family in §5, each verified before it lands, each
  with the `Co-Authored-By` trailer. The push waits for the designer.

## 4. Phase 1 — infrastructure

### 4.1 C++, one editor-closed rebuild

`Source/TheDreamEditor/Public/TDTimeTools.h` and `Private/TDTimeTools.cpp`: `UTDTimeTools`, a
`UBlueprintFunctionLibrary`, category `TheDream|Time`:

- `static bool SetFixedTimeStep(bool bEnabled, float DeltaSeconds = 1.f / 60.f)` — calls
  `FApp::SetFixedDeltaTime` then `FApp::SetUseFixedTimeStep`, returns `FApp::UseFixedTimeStep()` so a
  build without support reads back false.
- `static bool IsFixedTimeStep()`, `static float GetFixedDeltaTime()`.

`UTDInputTools` gains:

- `InputKey(APlayerController*, FKey, bool bPressed)` — builds `FInputKeyEventArgs` from the local
  player's viewport, its primary input device, `IE_Pressed` or `IE_Released`, amount 1 or 0, and
  `FPlatformTime::Cycles64()`, and calls `PC->InputKey`. A held key is a press followed by a release
  frames later; key state persists between them as it does for a keyboard.
- `InputAxis(APlayerController*, FKey, float Delta)` — the axis constructor, for `MouseX` and `MouseY`.
- `InjectAxis2D`, `StartHoldAxis2D`, `UpdateHoldAxis2D`, each taking `FVector2D`, wrapping the
  subsystem's inject, start-continuous and update-continuous calls with `FInputActionValue(Axis2D)`.

The three existing functions are untouched.

Game module, `.cpp` only, the actor name after the tag: `ABILITY END` (both variants), `AIM ASSIST`
(both), `BUFFER` (five), `COIL START` (two), `COMMIT`, `DODGE` (two), `DODGE END`, `ESCALATE`,
`MONTAGE` (seven), `RELEASE` and `RELEASE OFF` in the charged and get-up abilities, the notify's
`RELEASE BEGIN`/`END`, and `STRING chain out of swing %d` gains ` on <Actor>` after the index.
Name source: `GetNameSafe(GetAvatarActorFromActorInfo())` in abilities, the mesh's owner in the
notify, `GetName()` on the character.

Checker edits in the same commit, so every legacy row stays green: `getup_elapsed` matches
`ABILITY END  [A-Za-z_0-9]+ GA_GetUpAttack`; `s8_commits_branch` reads the branch from `$5` and drops
the `AIM WEDGE` attribution; every `DODGE      dir=` pattern becomes `DODGE      [A-Za-z_0-9]+ +dir=`
(`run_s3`, `clean_dodge_distances`, `dodge_fit_lengths`, `dodge_from_full_remaining`,
`acts_during_parry_window`, `acts_during_parry_recovery`, `dodges_inside_hitstun`,
`waiver_dodge_latency_ms`); `getup_dodge_remaining`, `getup_dodge_travel`, `damage_during_getup_exit`
shift their field tests by one; the `--self-test` fixture lines take the new formats. The
"Reading the combat trace" paragraphs in `Docs/Debug-Instruments.md` that name formats are corrected,
including the sentence saying `DODGE` carries no name.

Build procedure, from `Docs/Working-In-Unreal.md`: announce; `quit_editor()` through
`run-in-editor.py`; confirm `PackageRestoreData.json` reads `Packages: []`; the UBT command from that
file; `find Source -newer Binaries/Win64/UnrealEditor-TheDream.dll` empty and no `patch_*` files;
relaunch with `nohup`; poll `run-in-editor.py -c` until it answers; read `TD.DebugCombatTiming` back
as 1. Smoke: one key-level tap of attack on the player pawn, and the new names read off the log.

### 4.2 `scenarios.py`

One dict per scenario:

```
"tier-light": dict(
    family="tier", legacy=True, legacy_id="s1-light",
    roles=dict(attacker="BP_TrainingDummy_C_2", defender="BP_TrainingDummy_C_1"),
    knobs={"attacker": {"debug_auto_attack": True, "debug_auto_attack_hold_seconds": 0.1, ...},
           "defender": {...}},                  # merged over BASELINE, written in full
    player=dict(spawn=(-3000, -3000, 100), yaw=0, props={}),
    plan=[],                                    # (t, actor, op, args); t in frames from plan start
    stop=dict(duration=30.0),                   # or dict(until=("KNOCKDOWN  ", 6), timeout=90)
    expect=dict(reps=8),
    mutations=[("shift", "RELEASE BEGIN", +0.100)],
)
```

- `BASELINE` holds every `Debug*` knob at its CDO value read from `Combat-Values.tsv`, so a
  scenario's knobs are a full reset. The runner writes all of them on both dummies before each start.
- `legacy_id` is what the bash checker's case arm is renamed to match; it is also the bridge table's
  source (§4.6, §6).
- Plan ops: `tap(action)`, `press(action)`, `release(action)`, `hold(action, frames)`,
  `move(x, y, frames)`, `face(actor, yaw)` (the player's control rotation plus actor yaw, since
  facing and aim are camera-relative; a dummy's actor rotation), `look(dx, dy)` (mouse axes),
  `teleport(actor, loc, yaw)`, `set(actor, prop, value)` for a runtime instance property such as the
  player's `debug_suppress_lunge`, `ready(player)` (the rep gate of §3), `wait_for(pattern, count)`,
  `lock_to(pattern)` which rebases plan time to the frame the pattern first appears after the current
  point, `mark(text)`.
- Actions: attack, block, dodge, parry, jump, move. At load the runner reads `IMC_Combat` and
  `IMC_Default` and resolves each to its key; `move` resolves to `W A S D` and holds the keys whose
  swizzled sum is the requested direction, or falls back to 2D action injection for a diagonal the
  keys cannot express.
- Load-time validation: every knob name exists on the class, every role actor exists in the editor
  level, every fixture hold sits strictly between two checkpoints (0.15, 0.35, 0.75), every action
  resolves to a key.

### 4.3 The runner, `ue_regression_runner.py`

Runs inside the editor from one `run-in-editor.py` call, reading `Saved/Regression/run.json`
(`run`, `scenarios`, `fixed_step`, `dt`, `tapes`, `screen_percentage`). A slate post-tick state machine:

1. `APPLY_KNOBS` on the editor-world actors, asserting no `UEDPIE` in their paths.
2. Fixed step on (if requested), `r.ScreenPercentage` lowered to the requested value, then
   `editor_request_begin_play()`.
3. `WAIT_WORLD` until the game world, the player pawn and its BeginPlay exist; 30 s wall timeout.
4. `SETUP`: teleport and face the player; apply `player.props`; `log_flush`; emit
   `REGRESSION BEGIN <id> run=<run> idx=<n> game=<t> frame=<f>` and
   `REGRESSION ROLES <id> player=<name> attacker=<name> defender=<name>`. Plan frame 0 is the next tick.
5. `RUN`: each tick, execute due ops, sample the tape, tail the log for `until` and `lock_to`
   patterns, stop on duration, `until`, or timeout. Every injection is logged as
   `REGRESSION INJECT <id> frame=<f> <action> <press|release>` so the trace's `INPUT` line can be
   paired with it.
6. `SETTLE`: release every hold and move; wait until every combat pawn reads stamina at max and no
   ability active, or 8 s; emit `REGRESSION TEARDOWN <pawn> tags=<a,b> states=<hitstun,…> health=<h> stamina=<s>`
   per pawn, tags from `AbilitySystemBlueprintLibrary.get_ability_system_component(pawn).get_owned_gameplay_tags()`,
   states from the exposed `Is*` getters (`IsMovementLocked` is not exposed; the union of the state
   getters stands in for it).
7. `editor_request_end_play()`; wait for `is_in_play_in_editor()` false; `log_flush`; emit
   `REGRESSION END <id> status=ok game=<s> frames=<n>`; next scenario. After the last, fixed step off,
   screen percentage restored, `REGRESSION DONE run=<run>`.

Frame convention: an injection made in the post-tick callback of frame N is consumed by the
player's input processing in frame N+1, so "press at frame N" names the frame whose `INPUT` line
carries it. The universal set asserts that latency is the same constant on every injection (§4.5).

Any exception releases holds, restores dilation, the clock and the screen percentage, ends play,
emits `REGRESSION END <id> status=error msg=<…>` and continues with the next scenario. Tapes go to
`Saved/Regression/<run>/<id>.tape.tsv` as `frame t pawn x y z yaw health stamina tags`. Tailing reads
only bytes appended since the last offset; `log_flush` runs each tick unless it measures over 1 ms,
then every fifth. The per-tick cost of the callback is measured on the first run and recorded.

### 4.4 The orchestrator

`Tools/RegressionCheck/regression_run.py`, run by the engine's Python, behind a thin
`regression-run.sh` so the documented entry point is a shell script like the other tools.

`regression-run.sh [--all | <id>… | --family knockdown] [--realtime] [--no-mutate] [--repeat] [--trend] [--dry-run]`

Preflight: the editor answers; not in PIE; no montage open in an asset editor
(`AssetEditorSubsystem.get_all_edited_assets`, warn and list); `PlayNumberOfClients=1`;
`TD.DebugCombatTiming` 1; `t.MaxFPS` 0; autosave state read from `EditorLoadingSavingSettings` and
printed; `regression-check.sh --self-test` and `--bands-check` pass, the format lint included;
`git status --short` printed; the lock `Saved/Regression/.lock/` taken with the pid, a dead pid
reclaimed.

Run: write `run.json`; launch the runner; every 2 s read new `REGRESSION END` lines; for each, save
the raw slice to `Saved/Regression/<run>/<id>.slice.log`, evaluate (legacy: `regression-check.sh
<legacy_id> --slice <run>:<id>`; new: `regression_eval.py <id> <slice>`; both: the universal set and
the frame ledger), then apply each mutation and require at least one FAIL. A scenario whose END has
not arrived by `3 × duration + 60 s` is ended through `run-in-editor.py` and marked TIMEOUT.
`--repeat` runs the list twice and diffs key event frames per scenario.

Output: the table `scenario | passed | failed | mutations proven | game s | wall s | frames` with
totals and the frame ledger per scenario; `Saved/Regression/<run>/summary.json` with the same plus
every assertion's samples (n, min, max, mean); one row per assertion appended to
`Saved/Regression/history.tsv`, which `--trend` reads to show a band's samples drifting toward an
edge across runs. Exit 1 on any FAIL, TIMEOUT or unproven mutation. Cleanup on exit and on
interrupt: clock and screen percentage restored, dilation 1.0, play ended if running, lock released.

### 4.5 Checker changes

- `--slice <run>:<id>` bounds the slice between that scenario's BEGIN and END lines; `slice_log` and
  `raw_session_count` share one bounds function; without the flag the last `Bringing World` still wins.
- Roles: extractors that hard-code `BP_PlayerCharacter` (`s8_activates`, `s8_swing0`,
  `s8_commits_branch`) read the player's name off the roles line.
- Case arms renamed to the new ids; no aliases.
- `--bands-check`: a table mapping every `BAND_*` to `Combat-Values.tsv` rows and a transform, FAIL
  on mismatch naming both values. Sums for the elapsed bands, ×1000 for the millisecond ones,
  brackets for the dodge travel and chain latency windows. Dummy parity: every property the parity
  paragraph names reads equal on `BP_TrainingDummy` and `BP_PlayerCharacter`.
- Relationships, same flag, each naming its source: `TurnRateDegrees == 180 / Branches[0].HoldUntilSeconds`
  (spec, Facing); light `HitstunSeconds > ReleaseAt + Release + ChainOpenAfterRecovery` (spec, Hitstun);
  light `BlockstunSeconds > 0.5 + Branches[0].ReleaseAt − Branches[1].ReleaseAt` (the band's comment);
  charged `StaminaDamage ≥ StartingMaxStamina` (trap: the charged always breaks);
  `ParryWindowSeconds < Branches[2].ReleaseAt − Branches[1].ReleaseAt` and
  `ParryWindow + ParryWhiffRecovery ≥ Branches[2].ReleaseAt` (the parry bands' comments);
  `LungeStandoffCm < every Hitboxes[*].MaxReachCm` (trap); `HitSpacingCm ≤ LungeDistanceCm +
  light LungeDistanceCm + MaxReachCm − LungeStandoffCm` (trap: the connect inequality); the two
  knockdown types' lockout plus window equal (spec: type-invariant total). `KnockdownSpacingCm` above
  the covered range is a WARN, being deliberate design the fixtures lean on.
- Format lint, same flag: every `TD_TIMING_LOG` format string in `Source/TheDream` begins
  `[%.3f] <TAG>  %s` after the eleven renames, so a future trace line without a pawn fails preflight.
- Frames: every span and latency row prints `(N f)` at 1/60 beside seconds.
- Frame ledger, in `regression_eval.py` and run on every slice: per `DAMAGED` or `BLOCKED`, the
  defender's actionable frame (`HITSTUN END`, `BLOCKSTUN END` or `KNOCKDOWN STAND` for that pawn)
  minus the attacker's (`STRING chain out` for that swing if present, else its `ABILITY END`),
  grouped by the swing's `COMMIT` branch and by hit or block. Printed, not asserted.
- Universal invariants, every slice: pairings per pawn (`ACTIVATE`→`ABILITY END`, `KNOCKDOWN`→`RISE`→`STAND`,
  `BLOCK up`→`down`, `PARRY WINDOW open`→`SUCCESS|WHIFF`, `HITSTUN`→`END`, `BLOCKSTUN`→`END`,
  `GUARD BREAK`→`GUARD END`, `EXHAUSTED`→`END`, `DEATH`→`REVIVE`), one trailing unpaired tolerated at
  the slice end; at most one `DAMAGED` per target inside one attacker's release window; the health
  ledger per target; no `pos=-1.0000`, no `played=` unequal to `len=`, no `opened at 0.0000`;
  monotonic timestamps; a constant frame latency from every `REGRESSION INJECT` to its `INPUT` line;
  zero `Warning` or `Error` lines on `LogTDCombatTiming`, `LogAbilitySystem`, `LogAnimation`,
  `LogScript`, `LogBlueprint` outside `Tools/RegressionCheck/log-allowlist.txt`, seeded from the
  first run with a reason per line; every `TEARDOWN` line empty of tags outside the scenario's
  allowlist, health and stamina at max.
- Mutations: `(regex, replacement)` on the slice text, or a builtin `shift(tag, ms)`, `drop(tag, n)`,
  `dup(tag, n)`, `set(tag, field, value)`. Legacy rows get one or two each in `scenarios.py`.
- Vocabulary for new rows: `span(a, b, key, band)`, `count(tag, per, expect)`, `field(tag, name,
  expect|band)`, `absent(tag, between, key)`, `paired(a, b, key)`, and tape helpers
  `displacement(pawn, f0, f1)`, `slope(pawn, "stamina", f0, f1)`, `first_frame(pawn, predicate)`.

### 4.6 Porting the 38 rows

Knobs from the matrix in `Docs/Debug-Instruments.md`, unchanged in meaning: the attacker's
`debug_auto_attack` on, facing `WHILE_ATTACKING` except `string-finisher-arc` (`NEVER`); the player
parked at (−3000, −3000, 100) except `string-finisher-arc` at (200, 150, 100); both dummies silent for
the `chain-*` and `input-*` rows. The bridge, which becomes the "Retired names" row at delivery:

| Family | Old id → new id |
|---|---|
| `tier` | `s1-light` → `tier-light`; `s1-heavy` → `tier-heavy`; `s1-charged` → `tier-charged` |
| `attack` | `s5-cancel` → `attack-cancel`; `s5-waiver` → `attack-waiver` |
| `string` | `s4-string` → `string-cadence`; `s4-guarantee` → `string-guarantee`; `s4-block` → `string-blocked`; `s4-360` → `string-finisher-arc` |
| `chain` | `s8-chain-early` → `chain-early`; `s8-chain-late` → `chain-late`; `s8-chain-closed` → `chain-closed` |
| `input` | `s8-stale` → `input-stale`; `s8-discard` → `input-discard`; `s8-hold-tier` → `input-hold-tier` |
| `block` | `s2-light` → `block-light`; `s2-heavy` → `block-heavy`; `s2-charged` → `block-charged` |
| `dodge` | `s3` → `dodge-cycle` |
| `parry` | `s5-parry` → `parry-catch`; `s5-parry-reward` → `parry-reward`; `s5-parry-whiff` → `parry-whiff` |
| `knockdown` | `s6-knockdown` → `knockdown-normal`; `s6-hard` → `knockdown-hard`; `s6-stand` → `knockdown-stand`; `s6-getup` → `knockdown-getup-attack`; `s6-dodge` → `knockdown-getup-dodge`; `s6-kipup` → `knockdown-getup-kipup`; `s6-block` → `knockdown-getup-block`; `s6-hard-stand` → `knockdown-hard-no-stand`; `s6-exhausted` → `knockdown-exhausted-dodge`; `s6-exhausted-kipup` → `knockdown-exhausted-kipup`; `s6-exhausted-block` → `knockdown-exhausted-block`; `s6-exhausted-attack` → `knockdown-exhausted-attack`; `s6-airborne` → `knockdown-airborne`; `s6-exhaust-regen` → `knockdown-regen-exception` |
| `death` | `s7-death` → `death-revive`; `s7-death-grade` → `death-over-knockdown` |

Durations:

| Rows | Seconds |
|---|---|
| `tier-*`, `attack-cancel`, `attack-waiver`, `chain-*`, `input-stale`, `input-discard`, `input-hold-tier` | 30 |
| `string-cadence`, `string-guarantee`, `string-blocked`, `parry-reward`, `parry-whiff`, `knockdown-normal`, `knockdown-hard`, `knockdown-getup-*`, `knockdown-hard-no-stand`, `death-over-knockdown` | 60 |
| `string-finisher-arc`, `knockdown-stand`, `death-revive` | 90 |
| `dodge-cycle`, `knockdown-exhausted-*` | 120 |
| `block-*`, `knockdown-regen-exception` | 150 |
| `parry-catch` | 180 |
| `knockdown-airborne` | 240 |

About 3,200 s of game time, roughly 30 min of wall at the measured frame rate. The six `s8` plans
move into `scenarios.py` as written in `ue_s8_driver.py`; once the runner reproduces all six green,
`ue_s8_driver.py` and its JSON are deleted, their docstring's timing notes carried into the entries.

### 4.7 Verification of Phase 1

1. `--self-test` and `--bands-check` green; a band that mismatches is re-derived, never nudged.
2. A fixed-step matrix: every legacy row green except `parry-catch`'s ender lockout, red today and
   replaced by a per-cell row in Phase 2; mutations proven for every row; the universal set green
   with the allowlist seeded and explained.
3. `--repeat`: two fixed-step matrices, key event frames identical except physics rows (`DEATH
   SETTLE` inside its band).
4. A real-time matrix, same expectations; per-row wall time and jitter reported against fixed step.
5. The measured speedup and the runner's per-tick cost, recorded in the decision entry.

### 4.8 Commits

C1 C++ and extractor updates (build verified, names read off a smoke log). C2 schema, baseline,
key resolution, runner (three rows end to end). C3 orchestrator, slicing, bands and lint. C4
universal set, mutations, frames, ledger, summary and history. C5 the port under the new names, the
three matrices, the driver retired. C6 Phase 1 docs (§6).

## 5. Phase 2 — rows

Each row: id, spec basis, fixture, plan, assertions, mutation, reps. "Lock" means `lock_to` the
attacker dummy's `ACTIVATE`; its hit lands 12 frames later. The defender position is the placed
defender's, (200, −150, 100) facing the attacker at (200, 0); an unused dummy is parked at
(−3000, −3000) with everything off. Every rep opens with `ready(player)`. Frames are 1/60.

**A. Inherited from the 2026-09-02 trap.**

| Id | Basis | Fixture and plan | Asserts | Mutation | Reps |
|---|---|---|---|---|---|
| `input-accept-hitstun` | acceptance governs hitstun (spec, chain window) | player defends; attacker lights; lock; press attack at hit + 33f − 12f − 2f (early) or + 2f (late) | early: `BUFFER … expired`, no player `ACTIVATE` before the next cycle; late: `BUFFER … stored`, player `ACTIVATE` within 1f of `HITSTUN END`; `REFUSED … hitstun` each press | shift `HITSTUN END` −100 ms | 3 + 3 |
| `input-accept-blockstun` | same rule, blockstun | player holds block; release at hit + 1f; press attack at `BLOCKSTUN END` − 12f ∓ 2f | as above against `BLOCKSTUN END`; `BLOCKED` each rep | same | 3 + 3 |
| `input-accept-lockout` | same rule, parry lockout | defender dummy `PeriodicParry` 1.7; player attacks at window open − 9f so the hitbox lands inside; press attack at `PARRY LOCKOUT END` − 12f ∓ 2f | `PARRY SUCCESS` per rep; early discarded, late fires within 1f of `PARRY LOCKOUT END` | same | 3 + 3 |
| `knockdown-getup-held` | held inputs reach the window; priority guard, dodge, attack, stand (spec, Knockdown) | hard: attacker heavies; lock; from `KNOCKDOWN` + 30f hold one of block / dodge / attack / jump, then pairs block+dodge, dodge+attack, attack+jump; normal: attacker taps 3, hold jump; hold all four | rise `by=` block / kipup / attack / auto (jump refused: `no stand from a hard knockdown`) at lockout end ± 1f; pairs rise by the priority winner; `rose on held <tag>` names it; normal: `by=stand` at 60f; all four → block | change the `by=` token | 1 each, ×2 |
| `knockdown-getup-exhausted-held` | exhaustion refuses block, dodge, kip-up; leaves the attack (spec) | player holds block 9.5 s before the heavy so stamina hits 0 about 1 s before the hit; then hold block+dodge+attack; second variant hold block only | first: `by=attack`; second: `by=auto` and no `by=block`; `EXHAUSTED` before `KNOCKDOWN` each rep | drop the `EXHAUSTED` line | 3 + 3 |
| `chain-late`, `input-stale`, `input-discard` | the three stale rows | Phase 1's port re-runs them | unchanged | existing | 8 |

**B. Conversions that buy time. The legacy assertions stay; the fixture becomes a plan.**

| Id | Change | Plan | Asserts added | Reps and duration |
|---|---|---|---|---|
| `parry-catch` | player parries, phase-locked | lock; tap parry at +6f (window +6..+24f covers the hitbox at +12..+21f) | `PARRY SUCCESS == reps` | 6, 25 s (was 180) |
| `parry-reward` | player pre-blocks | hold block 3.9 s from cycle start, release, parry 3f later phase-locked | `gained=25` on every success, successes == reps | 4, 30 s |
| `parry-whiff` | player whiffs and probes | tap parry with nothing arriving; attack at +9f and +27f, dodge at +36f, block at +51f and +57f | refusals `parrying`, `parry recovery` at the exact presses; the block at +57f raises | 6, 15 s (was 60) |
| `knockdown-airborne` | player jumps into the hit | dummy schedule known: jump at 3.0k + 0.05 s so the hit lands near apex | `airborne=1` count == reps; height ≥ 20 cm every rep | 5, 20 s (was 240) |
| `tier-cells` (new) | all nine cells | player whiffs in open space; plans from `ue_chart_ab.py`: L1 tap; H1 hold 13f; C1 hold 51f; L2 tap, tap +31f; H2 tap, hold 31–44f; C2 tap, hold 31–82f; L3 tap, tap, tap; H3 tap, tap, hold 62–76f; C3 tap, tap, hold 62–113f | per cell: `ACTIVATE`→`RELEASE BEGIN` = `Branches[b].ReleaseAt` ± 30 ms; elapsed = the cell's release-at + release + recovery, +0 to +35 ms; `ESCALATE` 0/1/2; `COMMIT` branch b; `TIER SWAP` names the cell's montage; no inertialization warning | 3 per cell, 70 s |
| `dodge-directions`, `dodge-iframes` (new) | eight sections and i-frames | hold move in each of 8 directions 12f before a dodge tap and through it; i-frame rep: tap dodge at lock + 9f into an arriving light; control rep without the dodge | `DODGE <player> dir=<D> section=<D>`, `fitLen` 0.667, `dist` 405 ± 15 with `fwd`/`right` split matching the direction; i-frame rep zero `DAMAGED`/`BLOCKED` on the player and no `LUNGE STOP`; control `DAMAGED` | 16 + 3 + 1 |

**C. Composition rows.**

| Id | Basis | Plan | Asserts | Reps |
|---|---|---|---|---|
| `block-facing` | block is 180° forward in the defender's frame | player holds block facing away (`face` yaw); control facing the attacker | away: `DAMAGED`, no `BLOCKED`; control: `BLOCKED` | 2 + 2 |
| `parry-facing` | parry has no facing test | parry from behind, phase-locked | `PARRY SUCCESS` every rep | 2 |
| `parry-refused` | refused while blocking, dodging, exhausted, airborne | hold block then tap parry; tap dodge then parry at +6f; exhausted (drain plan) then parry; jump then parry | a `REFUSED GA_Parry` per press, no `PARRY WINDOW` | 2 each |
| `attack-owns-movement` | movement input suppressed for the whole ability | hold move during a whiffed light; control without | trajectories equal ± 1 cm through `ABILITY END`, then the held move displaces within 3f | 2 + 1 |
| `attack-airborne` | airborne attack refused, not buffered | jump, tap attack at +18f | `REFUSED … airborne`, no `ACTIVATE` after landing, no `BUFFER … stored` | 3 |
| `lock-guard-break` | the break locks movement and jump; the control (trap) | player holds block; attacker heavies; two blocks break; hold move from the break; control without; tap jump inside the stun | displacement equal to control ± 2 cm through `GUARD END`, then moves within 3f; `REFUSED GA_Jump … guard broken` | 2 + control |
| `block-stun-offense-only` | disables offense and nothing else | blocked light, guard released; tap dodge at +6f; parry in another rep; attack in a third | `DODGE` fires; `PARRY WINDOW` opens; the attack is refused | 2 each |
| `block-commitment` | a release inside the floor is remembered | press block, release at +6f; control release at +24f | `BLOCK down (released)` at +15f ± 1f; control at +24f | 3 + 3 |
| `input-last-wins` | single slot, last press wins | in hitstun press attack at end − 9f, dodge at end − 6f | `BUFFER InputTag.Attack: dropped, superseded by InputTag.Dodge`; `DODGE` at `HITSTUN END`, no player `ACTIVATE` | 3 |
| `input-parry-never-buffers` | a replayed parry is a mistimed one | tap parry in hitstun inside acceptance | `REFUSED`, no `PARRY WINDOW` after `HITSTUN END`, no `BUFFER InputTag.Parry` | 3 |
| `input-block-never-replays` | buffer actions, not states | one-frame block tap in hitstun; control holds through | no `BLOCK up` after `HITSTUN END`; control `BLOCK up` at `HITSTUN END` | 3 + 3 |
| `death-midair` | die in mid-air leaves nothing stranded (checklist) | six lights taken, jump before the seventh | `DEATH` while airborne, `REVIVE`, then a move hold displaces and `movement_mode` reads walking | 2 |
| `attack-whiff-commitment` | a whiff hands nothing back | whiff a light; tap dodge at +24f | `REFUSED … Committed`, no `DODGE` | 3 |

**D. Boundary family, `edge-*`.** Two reps per side; assert the two sides differ and each side is
consistent; report the outcome at T − 1f, T and T + 1f. Rulings follow the report.

| Id | Authored | Probe | Outcomes |
|---|---|---|---|
| `edge-light-checkpoint` | 0.150 | hold 8f vs 10f | `COMMIT branch 0` vs `1` |
| `edge-heavy-checkpoint` | 0.350 | hold 20f vs 22f | `branch 1` vs `2` |
| `edge-chain-open` | 0.283 | second tap at 16f vs 18f | `expired` vs `chain out` |
| `edge-chain-close` | 0.683 | tap at 40f vs 42f | `chain out` vs `expired` |
| `edge-fresh-open` | 0.750 | tap at 44f vs 46f | `expired` vs fires at 57f |
| `edge-actionable` | 0.950 | tap at 56f vs 58f | fires at 57f vs at 58f |
| `edge-hitstun-accept` | hit + 0.350 | press at 20f vs 22f after the hit | `expired` vs fires at `HITSTUN END` |
| `edge-parry-close` | 0.300 | parry at hit − 19f vs hit − 17f | `SUCCESS` vs `DAMAGED` |
| `edge-lockout-end` | 1.000 / 1.500 | jump at −1f vs +1f | `REFUSED (lockout)` present vs absent, rise frame equal |
| `edge-guard-floor` | 0.250 | release at 14f vs 16f | `down` at 15f vs 16f |
| `edge-recovery-accept` | 0.700 | attack at 41f vs 43f | `expired` vs fires at 54f |

**E. Two attackers.** Both dummies attack the player, lights at intervals 3.0 and 3.1, the player
between them. Cycle k puts their activations 6k frames apart: cycle 1 is inside Grace, cycle 2 is
not. `parry-grace-catch`: parry the first of the pair; assert `PARRY SUCCESS … by=grace` in cycle 1,
no second `PARRY GRACE` after it, `DAMAGED` in cycle 2; 6 cycles. `knockdown-floor-per-body`:
heavies; the second attacker's hit during the down: zero `DAMAGED`, per the universal set; 4 cycles.

**F. Geometry edges, `reach-*`.** Player attacks with `debug_suppress_lunge` set on its instance;
the target dummy silent and teleported. `reach-light`: read `OverlapsCapsule` first for whether reach
meets the capsule surface or centre, then place the target 5 cm inside and outside `MaxReachCm` plus
that convention. `reach-arc`: bearing 2° inside and outside half the arc plus the subtended angle.
`reach-height`: the target's movement component set to flying at ±65 and ±75 cm. `reach-aim-wedge`:
target at 15° gets an `AIM ASSIST '<dummy>'` line and a `TARGET` bearing near 0; at 25° `no candidate
in wedge`. Two reps each, `DAMAGED` present or absent.

**G. Movement-lock family, `lock-*`, from the position tape.** Each state with a held move and a
control without: `lock-attack-recovery` on a whiff, `lock-hitstun`, `lock-knockdown`, `lock-parry`
(window and recovery on a whiff), `lock-parry-lockout` after the defender dummy's phase-locked parry,
`lock-dodge`. Assert the trajectories equal ± 2 cm through the state's end line, then the held rep
displaces within 3f. `lock-blockstun-free` asserts the opposite: displacement within 3f of
`BLOCKSTUN`. Speed caps: `lock-block-speed`, block held plus move for 30f travels 62 ± 6 cm;
`lock-exhausted-speed`, exhausted plus move travels 200 ± 20 cm.

Order: A, then B, then D, then G, then C, then E, then F. Exemplars, built first and by hand:
`input-accept-hitstun`, `knockdown-getup-held`, `edge-light-checkpoint`, `lock-guard-break`,
`reach-light`.

## 6. Phase 3 — docs, traps, closedown

- `Docs/Debug-Instruments.md`: the checker section describes the orchestrator, marker slicing, the
  clock, tapes, teardown hygiene, mutations, the universal set, frames, the key-level input path;
  the matrix becomes a region between `<!-- matrix:begin -->` and `<!-- matrix:end -->` written by
  `Tools/RegressionCheck/gen-matrix.py` from `scenarios.py` and the checker's labels, with a
  docs-check freshness check that regenerates and diffs; "adding a scenario is three edits" is
  replaced by the new process; the trace-format paragraphs carry the names.
- `Docs/Working-In-Unreal.md`: the fixed step, PIE from Python, `log_flush` and the live tail,
  key-level injection, each with surface and date; "Verifying combat changes" names
  `regression-run.sh`.
- `Docs/Unreal-Findings.md`: a dated entry for the findings and for `create_player`, available and
  unused.
- `Docs/Combat-Decisions.md`: the audit's entry with the measurements and the speedup; discharged
  traps, each saying what discharged it: the 2026-09-02 untested half, the four never-asserted
  cells, the directional dodge, the guard-break walk, the `s5-parry` lockout band, airborne at n=1,
  the get-up press inside the lockout; one new dated trap naming what the session did not reach;
  a "Retired names" row carrying §4.6's bridge; symbol index rows for `UTDTimeTools`, `scenarios.py`,
  `ue_regression_runner.py`, `regression_run.py`, `regression-run.sh`, `regression_eval.py`,
  `gen-matrix.py`.
- `Docs/Closing-Down.md`: a step between 1 and 2: run `regression-run.sh --all` and the real-time
  canary; a red row is a correctness item and blocks the push.
- `CLAUDE.md`: the roster strikes Regression Audit; the pickup paragraph goes; Polish resumes at the
  windup pass. The coupling rule keeps naming `regression-check.sh`, which the docs-check manifest
  expects, and adds the orchestrator.
- `Tools/CommentCheck/baseline.txt`: `regression-check.sh` raised with the reason; new files
  baselined with `--baseline` once the file set is settled.
- Memory: `combat-prototype-state` points at the new state. `Combat-Values.tsv` regenerated only if
  a value moved; none is expected to. This file deleted.

## 7. Risks and fallbacks

- The fixed step misbehaves under injection or physics: `--realtime` is the same runner, slower.
- A slate callback cannot drive PIE start and stop: the orchestrator drives each scenario itself
  through `run-in-editor.py` and `ue-mcp.sh`, about 3 s more per scenario, same scenarios file.
- Key-level injection does not reach Enhanced Input from a script-called `InputKey`: action-level
  injection is the fallback, already proven, and the plan's rows are unchanged.
- Log tailing lags: `log_flush` per tick, then every fifth if it costs; every stop condition also has
  a duration ceiling.
- The 2D injection's axes are swapped: the first move plan prints the tape and the `DODGE dir`; the
  wrapper is fixed before any row depends on it.
- A rename breaks an extractor silently: the fixed-step matrix and the mutation harness catch it;
  the self-test fixture is updated first.
- A usage pause: commits per unit, long runs backgrounded, §0 resumed.
- A real defect: the line stops and the report names it.

## 8. Budget

Phase 1 three to five hours of work plus about 1.5 hours of unattended matrices. Phase 2 exemplars
two to three hours, then 15 to 40 minutes per row. Phase 3 one to two hours. The whole of Phase 2 is
larger than one session, and whatever it does not reach is named in the closing trap.

## 9. Candidates awaiting the designer's ruling

Surfaced on 2026-09-03 while looking for assumptions the plan inherits. Each moves into the plan or
is struck on the designer's word.

1. **Golden-trace change detection.** Under the fixed step a scenario's event sequence is
   identical run to run unless code changed. Keeping the last green run's event skeleton per
   scenario (tags, frames, key fields, physics fields excluded) and diffing every run against it
   reports any change, asserted or not. It turns the loop from "checks what we thought of" into
   "notices anything that moved". Cost: a `--baseline-accept` step after a deliberate change, and
   diffs to read. Recommended, as a Phase 1 mode beside `--repeat`.
2. **The attacker model.** Most rows keep the dummy as attacker, and the dummy's attack path is not
   the player's: it drives the ability by tag, aims by AI focus, has no camera frame. Attack-side
   rules measured on it may not hold for the shipping path. Recommended: player-driven variants
   for the string (`string-player-cadence`, `string-player-blocked`), so both paths are asserted;
   defence-side rows keep the dummy.
3. **The level.** `L_CombatTest` carries the ramp, a documented contaminant, and fixed placements the
   fixture leans on. A flat regression map with the same two dummies is one `LevelFactory` call and
   removes a trap class; it is content, so the designer's call. Recommended, low priority.
4. **The fixture knobs.** Twenty-plus `Debug*` properties on the shipping character class stay
   frozen but present. Once every row is scripted, all but the periodic attacker's could retire.
   Deferred to Structure Audit; recorded here so it is not forgotten.
