# Debug instruments

**Read this before measuring anything in combat**, and not otherwise. It documents *this project's
own* instrumentation — the traces, the cvars, the debug attacker, the test level — as opposed to
`Docs/Working-In-Unreal.md`, which is about the editor and its toolset.

**Split out of that file 2026-08-14, because it was the only thing in it that grew.** Measured: over
two days every other section of `Working-In-Unreal.md` was static while its diagnostics section went
103 → 131 lines, and all of the growth was this content. That is structural rather than careless —
**every combat feature adds a trace line, a cvar or an instrument caveat, forever** — so it needed a
home where growth is expected and is paid for only by the person about to measure something, rather
than by every session.

Everything here fails the same way the toolset does: **silently.** A trace that names the wrong
montage, a cvar that is off, a log window that hides an event — none of them announce themselves, and
each has cost this project a session at least once.

---

## The debug attacker and the test level

**The debug auto-attacker has a configuration that silently invalidates it.**
`DebugAutoAttackResetDelaySeconds` **plus the attack's full length** must fit inside
`DebugAutoAttackInterval`, or the reset fires mid-attack and the numbers still look plausible. The
shipped defaults (0.35 and 3.0) clear all three tiers with room — the charged is the binding case at
1.45 s. Interval is read once in `BeginPlay`; only the delay is live at runtime.

**The dummy throws only lights.** `DebugAutoAttackHoldSeconds` is 0.1, below the light's 150 ms
boundary, so it never escalates and never coils — do not expect `ESCALATE`, `COIL START` or any
`CoilTurnRateDegrees` effect from it. 0.3 buys a heavy and 0.8 a charged, and changing it changes
the fixture every prior measurement was taken against.

**`DebugAutoAttackFacingMode` decides whether it aims at you** (`Never` / `WhileAttacking` /
`Always`, set to `WhileAttacking` 2026-08-14). On `Never` the dummy holds its placed yaw no matter
where you stand, which is a *useful control* and an easy thing to mistake for broken hit detection.
**Read `TARGET commit`'s bearing to tell which mode you are in** — a bearing pinned near ±90 while
you circle is a dummy that is not turning.

**The defender is a second dummy, and it is the fixture change of 2026-08-15.** `L_CombatTest` now
holds two: `BP_TrainingDummy_C_0`, the attacker at (200, 0, 96) yaw 180, unchanged; and
`BP_TrainingDummy_C_1`, the defender at (200, −150, 96) yaw 90, facing it. **Measurements do not
span this change** — a third pawn moves nearest-target selection, and every travel baseline taken
before it was taken against a level with one dummy in it.

**The defender's spacing is chosen against two constraints, and both bite if it moves.** It sits
150 cm from the attacker so that it, not the player at `PlayerStart` (200 cm away), is the
attacker's nearest pawn — that is what binds the pair. And it sits on the **−Y** side because a
stationary dodge resolves backward: placed at +150 it backs into the ramp's near vertical edge face
and travels **107 cm instead of 405**, which reads exactly like a broken dodge. *(Measured both ways
2026-08-15; a line trace at z=96 does **not** find the ramp, because at x=200 the ramp is only ~34
tall — pitch rotates about Y, so it tilts along X and its height varies with x, not y.)*

**`ETDDebugDefendMode` drives it, mirroring `bDebugAutoAttack`.** `HoldBlock` presses the block tag
once in `BeginPlay` and never releases — `GA_Block`'s `bResumeWhileInputHeld` does the rest, so the
guard returns after every break for as long as PIE runs. `PeriodicDodge` taps on
`DebugDodgeIntervalSeconds`, defaulted to **1.9 so it does not alias** against the attacker's 3.0.
Both are instance properties: set them on the placed defender, not the CDO, or the attacker adopts
them too.

**The dummy mirrors the player's combat values, and that is a rule rather than a coincidence**
*(2026-08-15, the user's call)*. A fixture exists to reproduce the conditions the systems under test
actually run in, so **parity is the default and any divergence is a design decision that has to be
argued for** — not something inherited by one Blueprint authoring a value and the other not. Three
had drifted and were mirrored onto `BP_TrainingDummy`: `BlockInitialStaminaCost` 0 → **10**,
`StaminaRegenPauseSeconds` 1.0 → **0.5**, `InputBufferSeconds` 0.10 → **0.20**. Ten other combat
values already matched.

**Two assertions only became true because of that**, which is the concrete reason it matters:
`BLOCK cost` now appears once per guard raise, and regen resumes at *action end + 0.5 s* as the
verification plan always claimed — against the dummy's old 1.0 s it did not. **Check parity before
blaming a band** if a stamina assertion starts failing.

**A dodger's travel is contaminated whenever the attacker reaches it, and `right=` is the tell.** A
stationary backward dodge reads `right=-0.0`; a sample with `right=-66.8` was one the attacker
collided with mid-dodge, and it measured 303 against a clean 405–414. **Filter on `right`, not on the
distance you were hoping for.** The dodger also sits ~405 cm from home between dodges — it returns
on the *next* press, not when the dodge ends — so the attacker spends much of the cycle chasing it.

**Beware the placed axis when testing facing.** `L_CombatTest` puts the dummy at (200, 0, 96.0) yaw
180 — actor `BP_TrainingDummy_C_0` since it was re-placed 2026-08-14, `_C_1` before that — and
`PlayerStart` at (0, 0), *directly along its facing*, so `bearing=+0.0` there is what a dummy
that never turns also reports. Use `StartPIE`'s `startTransform` to spawn off-axis; (200, −400)
reads +90 for a non-turning dummy and 0 for a turning one.


**`L_CombatTest`'s floor is one scaled `Engine/BasicShapes/Plane`** — scale 100, so 10000×10000
centred on the origin, edges at ±5000. Its size is a measurement constraint: accumulating travel
over many attacks is how displacement is measured, and the dummy walked off the old floor. Verify
any change with two `SceneTools.trace_world` probes, one inside and one beyond, so the check can fail.


---

## Reading the combat trace

**`TD.DebugCombatTiming` defaults to ON** and gives the per-attack phase trace. The full tag list,
enumerated from the source 2026-08-14 rather than remembered — `ACTIVATE`, `COIL START`, `COMMIT`,
`ESCALATE`, `RELEASE` / `RELEASE OFF`, `RELEASE BEGIN` / `END` (from the notify), `ABILITY END`,
`MONTAGE` (seven variants, including the delegate outcomes), `FACING LOCK`, `DODGE`, `DODGE END`,
`BUFFER`, `REFUSED`, `DEATH`, `REVIVE`, `TARGET`, `AIM ASSIST`, `AIM WEDGE` and `LUNGE STOP`. Turn
it off with `TD.DebugCombatTiming 0` when combat is not under test.

**Block adds several** *(2026-08-14)*: `BLOCK up` / `BLOCK down` for the guard's edges, `BLOCK cost`
when a guard charges its initial stamina, `BLOCKED` when a hit lands on one — carrying the stamina
damage and the bar remaining, which is the pair that says whether the next hit will break it — and
`GUARD BREAK` / `GUARD END` around the stun. **`BLOCKED` with `remaining=0.0` and no `GUARD BREAK`
beside it is the failure to watch for**: it means the break has been moved somewhere that cannot see
a hit landing on an already-empty bar.

**`BLOCK down` carries `(released)`, `(cancelled)` or `(exhausted)`, and the first two are logged in
`EndAbility`** — six things end a guard and only one is the button coming up. It was in
`InputReleased` for a day and a guard that survived its own guard break looked exactly like one
correctly cancelled. **`(exhausted)` comes from the commitment tick instead**, and is the guard the
system takes back when a too-expensive block's floor expires; expect it about
`MinimumBlockSeconds` after a `BLOCK cost` that emptied the bar.

**`BLOCKSTUN` / `BLOCKSTUN END` bracket a *successful* block's lockout** *(2026-08-14)*, and
`BLOCKSTUN` prints the `until=` timestamp rather than a duration — a second blocked hit extends it by
taking the max, so the end time is the only figure that stays true. **`BLOCKED` with no `BLOCKSTUN`
beside it means the guard broke instead**, which is correct and supersedes it; `BLOCKED` followed by
*neither* is the failure to watch for. **Nothing will ever print it for a charged** — its stamina
damage empties any bar, so it always breaks. That is a filed trap, not a bug.

**`INPUT <tag> pressed/released` is the button edge, and the only line in the trace that is** —
**but it is not proof of a human** *(clarified 2026-08-15)*. The debug attacker and defender both
drive `OnAbilityInputPressed`, so they emit `INPUT` exactly as a keyboard does; `INPUT
InputTag.Block pressed on BP_TrainingDummy_C_1` at `[0.000]` is the auto-defender seeding its guard.
**Read the avatar name before concluding a player did something.** Against a human at the keyboard,
the rest of this paragraph holds:
Everything else — `BUFFER`, `REFUSED`, the ability edges — describes what the *system* did with a
press, so a replayed press and a real one are indistinguishable by the time they reach an ability.
Reach for this whenever the question is "did the player actually do that", and pair the two edges
when the question is a duration. It is what separated a lost release from a genuine long hold, and
two bugs sat undecidable without it.

**`REFUSED` names the offending tags** and covers `ActivationBlockedTags`, which is where most
refusals live. Before 2026-08-14 it saw only the three C++ checks, so a session could refuse
constantly and log nothing. An empty reason is informative: a cost, a missing required tag, or the
ability already running.

**It over-reported for one day; the fix is confirmed in play** *(2026-08-15)*. It filtered in the
wrong direction, so a blocked tag was named whenever the avatar merely owned a *parent* of it —
every refusal during a block accused `State.Blocking.Committed`, long after the commitment had
expired. **Treat any 2026-08-14 pre-fix log's tag list as unreliable rather than as evidence.**
Confirmed by the first block-then-attack session after the fix: 3 such refusals across 39 guards,
each 150–176 ms after its `BLOCK up` — inside the commitment, where the accusation is true. **It is deduped by reason and by half a second**, because the resume retries
every tick while its input is held — undeduped it emits at frame rate and buries everything else.

**`ABILITY END` carries `elapsed`, which is an attack's true total** — the one number arithmetic
over the authored phases cannot give you, since it includes whatever the coil and the phase
transitions cost. Reach for it before concluding an attack runs long.

**`TD.DebugHUD` also defaults to ON** and draws health, stamina and active tags. It is the fastest
way to read a state tag, and the only way to see one without a log round-trip.

**Never judge a debug wedge's size by eye — read `AIM WEDGE`.** It prints the reach and arc of the
volume being drawn, at every change. A session was lost to comparing remembered radii: the drawn
wedge was one branch's for every tier, and eyeballing it produced two authored values that had never
done anything. One held attack should print three `AIM WEDGE` lines with non-decreasing reach.

**`LUNGE STOP` is the only way to see a lunge end early**, because a stop and a standoff gate that
simply stayed shut leave the character in the same place. Absence of it after a connecting hit is the
tell that the stop did not fire. **It names the *attacker*, not who was hit** — the avatar whose
lunge stopped. Easy to read backwards in a log where both sides are swinging, and the pairing that
disambiguates is the `TARGET release` line 30–150 ms before it.
**`TD.DebugMeleeTrace` defaults to OFF** and draws the authored wedges.

**Reach for the trace early.** Every real bug in the timing system was found by measuring, and
reasoning about play rates on paper mis-diagnosed several confidently.

**Warnings on `LogTDCombatTiming` are deliberately ungated as a family** — each one describes
authored data that has silently stopped fitting the clip, or an attack that will silently stop
dealing damage. Eight exist as of 2026-08-15; grep `LogTDCombatTiming, Warning` in `Source/` for
the list rather than trusting a count written here.


**`EXHAUSTED` / `EXHAUSTION END` bracket exhaustion** *(2026-08-15, closing the "nothing traces
exhaustion" gap)*. Both carry the bar, and **the two numbers are the assertion**: the rule is that
exhaustion begins at 0 and ends at Max rather than on a clock, so `stamina=0.0` on entry and
`stamina=100.0` on exit is the check, and anything else says the mechanism has moved. Both fire only
on a real transition. **`EXHAUSTED` prints *before* the `GUARD BREAK` it shares a frame with** — the
blocked hit empties the bar, the delegate exhausts you, and the break follows.

**An exhausted holder's `REFUSED` stream is expected output, not a fault.** A held guard retries
every tick while exhausted, deduped to one line per reason per half second — measured at ~0.503 s
apart, so roughly 2/s per ability. **Pass bands must whitelist it.** A *dodger* produces nothing
comparable: `GA_Dodge` does not resume, so a refused tap is one line and no more.

**`DODGE` carries `remaining=`** *(2026-08-15)*, parity with `BLOCK cost`, read after the cost is
charged — so a dodge from full reads exactly **50.0**, and the unattended stamina ledger is legible
without a single `GetActiveTags` round-trip.

**Not every state is traced.** The list is greppable: `grep -rn "TD_TIMING_LOG" Source/`.

**`RELEASE BEGIN`/`END` can report the wrong montage** *(found in review)*.
`AnimNotifyState_MeleeWindow` logs via `GetCurrentActiveMontage()`, so anything at higher priority —
a dodge cancelling an attack — supplies the position and rate instead. `DODGE` and `COMMIT` come
from the abilities and are unaffected; cross-check against those.

**Two `BUFFER` traps.** A held buffer does not expire but does not wait either — it fires at the
first opportunity, so holding a button through a lockout will not park one for testing. And **the
hold duration in `released after Nms held` is the value that matters**: it is bounded by how long the
*block* lasted, not by `InputBufferSeconds`, so it can exceed a tier boundary.

**A montage whose section length disagrees with its segment length is misaligned.** Swapping 0.733 s
clips for 0.833 s ones moved every segment while the section markers stayed put, producing cumulative
drift that read in play as "forward is fine and it gets worse round the compass" — an arithmetic
problem wearing an animation problem's clothes. `DODGE` prints `sectionLen=`; that is the tell.

---

## The regression checker

`Tools/RegressionCheck/regression-check.sh` asserts combat invariants against a PIE session's log.
**It is a log evaluator, not a test runner** — UE's Automation framework was considered and declined
for V3, so orchestration stays agent-side: set the fixture, `StartPIE`, poll the log on a condition,
`StopPIE`, then run the checker. It slices from the **last** `LogWorld: Bringing World … up for play`,
so it always reads the most recent session in whatever file it is given.

```bash
./Tools/RegressionCheck/regression-check.sh s2-heavy          # last session in the default log
./Tools/RegressionCheck/regression-check.sh s1-light some.log # or an explicit file
./Tools/RegressionCheck/regression-check.sh --self-test       # prove it can still fail
```

Exit 0 = all passed, 1 = an assertion failed, 2 = usage or no data. **Bands live in one config block
at the top**, so a retune is a one-line change; each carries its source in a comment.

**Run `--self-test` before trusting a green run.** It asserts a known-good band passes *and* a
deliberately wrong one fails, because a checker that cannot fail is indistinguishable from one that
passes everything. A sharper version of the same check: run a scenario against another tier's log
(`s1-light` against an `s1-heavy` session) and watch all four assertions fail with the real numbers.

### Two-player PIE, and why the checker must never be run against one

**The checker assumes a single world and has no way to notice otherwise.** A two-player log
interleaves the server and client worlds, which run **different clocks** — one attack produced
`RELEASE BEGIN` at 2.788 and again at 3.242 *(measured 2026-08-15)*. Pairing a press from one world
with a release from the other yields numbers that look plausible and mean nothing. **Run the loop in
single-player only**, and keep `PlayNumberOfClients=1`.

**The recipe, for when a two-player session is wanted.** Edit
`Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` **with the editor closed** — it is
rewritten on exit, so an in-session edit is lost — setting `PlayNumberOfClients=2` and
`PlayNetMode=PIE_ListenServer` under `[/Script/UnrealEd.LevelEditorPlaySettings]`. That file is
gitignored, so this is machine state and never travels with the repo. **Restore it afterwards.**

- `RunUnderOneProcess=True` is easier to drive but gives **no client-side log**.
- `RunUnderOneProcess=False` spawns a second `UnrealEditor.exe` writing `Saved/Logs/TheDream_2.log`
  — **the only channel to client-side state that exists**, because the MCP toolset returns only
  `UEDPIE_0_` actors and cannot see the client world at all.
- The client receives **6 of the 24 trace tags**: `RELEASE BEGIN`/`END`, `BLOCKSTUN`/`END`,
  `GUARD BREAK`/`END`. Death and exhaustion are absent because their logs sit on the authority-side
  transition rather than in the `Apply*` the `OnRep` calls.
- **`BLOCKSTUN until=` reads `0.000` on a client** — that field is server-only state, not a bug.
- **A second `PlayerStart` makes single-player spawn random**, so the loop's `startTransform` is now
  load-bearing rather than a convenience.

### The loop is a living artifact, and that is a standing rule

**Combat surface and loop coverage stay coupled** (2026-08-15, the user's rule; stated in
`CLAUDE.md`'s Working Rules, repeated here because this is where the work happens). Any package
planning or green-lighting a new combat capability must include **either** the scenarios and band
checks it adds to the checker **in that same package**, **or** a dated trap in
`Docs/Combat-Decisions.md` saying coverage is deferred and naming what is now untested. **There is
no third option, and picking neither is a process violation.**

**The failure it prevents is silent.** A checker whose scenarios lag the combat surface still prints
a full green table — it simply stops asking about the new thing. Nothing in the output distinguishes
"seven scenarios, all passing" from "seven scenarios, and the feature you shipped last week is not
one of them". That is why the choice is made at plan time and written down, rather than left to
whoever notices later.

**Adding a scenario is three edits**: a band block at the top of the script with its source in a
comment, a `run_*` function or a case arm, and a row in the matrix below naming the fixture it
expects. **Then run `--self-test`, and make the new assertion fail once on purpose** before trusting
it — a band nobody has seen reject anything is indistinguishable from one that cannot.

### Scenario matrix

Each row names the fixture it expects. **The knobs are `EditAnywhere` instance writes made before
PIE** — set them on the placed actors, not the CDO, and read the runtime instance back if a value
looks ignored.

| Scenario | Attacker `…HoldSeconds` | Defender `DebugAutoDefendMode` | Asserts |
|---|---|---|---|
| `s1-light` | 0.1 | `Off` | press→`RELEASE BEGIN` 200 ms ±30; elapsed 0.750 +10–35 ms; 0 escalations, 0 coils |
| `s1-heavy` | 0.3 | `Off` | 500 ms ±30; elapsed 1.150 +10–35 ms; exactly 1 escalation, 1 coil |
| `s1-charged` | 0.8 | `Off` | 750 ms ±30; elapsed 1.500 +10–35 ms; exactly 2 escalations, 1 coil |
| `s2-light` | 0.1 | `HoldBlock` | stamina damage exactly 5; `BLOCK cost` per `BLOCK up`; `GUARD BREAK` count equals blocks at `remaining=0.0`; break stun 1.0 s ±25 ms; `BLOCKSTUN` span 0.400 ±20 ms |
| `s2-heavy` | 0.3 | `HoldBlock` | as above, damage 50, `BLOCKSTUN` span 0.500 |
| `s2-charged` | 0.8 | `HoldBlock` | as above, damage 100, and **`BLOCKSTUN` never fires at all** |
| `s3` | 0.1 | `PeriodicDodge` | `DODGE`/`DODGE END` paired; clean travel 400–420 cm; dodge from full leaves exactly 50; `EXHAUSTED`/`EXHAUSTION END` paired, entering at 0 and clearing at 100 |

**`s2-charged`'s blockstun assertion is a filed trap promoted to a standing check.** The charged's
stamina damage equals the whole bar, so it always breaks and can never blockstun. **If that
assertion ever starts failing, the ladder has been retuned rather than the checker having broken** —
drop the charged's stamina damage below `MaxStamina`, or raise `MaxStamina`, and the tier's authored
`BlockstunSeconds` silently comes alive.

**Measured 2026-08-15, and these are what the bands were set from:** press→release 200–208 / 500–508
/ 751–757 ms; elapsed overhead +15–19 / +20–26 / +17–31 ms; clean dodge travel 405.1–414.1 cm across
17 samples. The exhaustion arithmetic reproduces to the millisecond — `GUARD END` + 0.5 s pause +
100 ÷ 25 exhausted regen predicted 14.733 and 9.279, and both landed exactly there.

**The dodger's travel needs the lateral filter or a fifth of the samples are wrong.** The checker
keeps only `DODGE END` lines with `|right| ≤ 1.0`; 5 of 22 in the reference run read ~297 cm with
`right≈-67`, all of them the attacker colliding with a displaced dodger. **Never widen the distance
band to admit them** — that is fitting the band to contamination.
