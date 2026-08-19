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
1.50 s. Interval is read once in `BeginPlay`; only the delay is live at runtime.

**The dummy throws only lights.** `DebugAutoAttackHoldSeconds` is 0.1, below the light's 150 ms
boundary, so it never escalates and never coils — do not expect `ESCALATE`, `COIL START` or any
`CoilTurnRateDegrees` effect from it. **0.22** buys a heavy and 0.8 a charged, and changing it
changes the fixture every prior measurement was taken against.

**The heavy's hold was 0.3 until 2026-08-18 and that value is now a coin flip** — the ladder
re-pole moved the heavy's `HoldUntilSeconds` to **0.30**, which is checkpoint 2, the charged
decision boundary. A hold sitting exactly on a checkpoint escalates or does not depending on
whether the release beats the timer that frame, so `s1-heavy` at 0.3 would silently measure a
mixture of heavies and chargeds. 0.22 is the middle of the surviving band, (0.15, 0.30). **The
general rule: a fixture hold is only valid strictly between two checkpoints, so re-check every
hold whenever any `HoldUntilSeconds` moves** — they are the fixture's real dependency and nothing
links them. `CoilTurnRateDegrees` is set to the
player's 600 so a raised hold does not silently inherit half-rate tracking, and is **unverified
until something actually coils**.

**`DebugAutoAttackStringTaps` makes each cycle a burst** *(2026-08-16, inert at its default 1)*.
Above 1, the dummy re-presses every `DebugAutoAttackStringTapIntervalSeconds` (0.25) so each tap
lands mid-previous-swing — the buffer extension and chain-out exercised as a masher exercises
them. **The home reset waits for the burst**: taps remaining or an open link window suppress it,
or a teleport would sever the spacing chain s4 measures. The whole burst must fit inside
`DebugAutoAttackInterval`, exactly as the single attack must.

**Set the fixture with PIE stopped, and check the world in the returned path** *(2026-08-18)*.
While PIE runs, `find_actors` returns the **`UEDPIE_0_`** world's actors, so a fixture write lands
on the throwaway copy and the editor actor is untouched — the next session then runs the *old*
configuration. It fails loudly at least: `s1-light` reported elapsed values of
`0.493 / 0.491 / 1.182` repeating, which is a three-attack string, because `StringTaps` was still 3.
Assert on the path having no `UEDPIE` in it before writing.

**Two knobs added 2026-08-18, both defaulted off so every existing scenario is untouched.**
`bDebugAutoAttackRotateTargets` takes the next target each attack instead of the nearest, excluding
whatever the last attack went to, and prints a `ROTATE` line; it advances in
`HandleDebugAutoAttackEnded` because that is the one event happening exactly once per attack — the
press path ticks twice as fast and lands back where it started. **It does not steer the attack**:
AI focus and the aim-assist target are independent systems, measured with `ROTATE` choosing one body
while `TARGET commit` picked another at −89.3°. `bDebugAutoAttackHomeBetweenAttacks` re-homes after
every attack rather than at burst end, which is how a **stationary** attacker is obtained; an
attacker whiffing into open space has an open standoff gate and runs its full authored lunge.

**Between bursts, positions are the safe thing to reset; health and stamina are not.** `s3` and
`s2-*` assert depletion accumulating — exhaustion entering at 0, breaks landing as the bar empties,
the health ledger stepping — while `s4-string` is the one position exception: it measures the
spacing chain a *connecting* string produces, which a mid-burst teleport severs.

**The attacker sometimes wedges against the ramp and goes stationary mid-attack** *(the designer,
2026-08-18)*. Nothing warns about it, and it silently corrupts anything measuring attacker travel —
lunge distance, spacing, knockback. If a travel figure looks impossibly small, check where it was
standing before believing it.

**Spawn the player pawn out of the exchange — `startTransform` (0, 800, 100), not the old
(0, 0, 100)** *(2026-08-16)*. The attacker re-focuses on the **nearest living pawn**, so during a
dead defender's revive window it turns on the player; at the old spawn (200 cm out, inside a
heavy's 400 travel + 150 reach) it *farmed* the player — whose `DAMAGED`/`REVIVE` lines then
poisoned the defender's ledgers. Two checker assertions failed exactly this way before the spawn
moved and the ledgers went per-target. At 800+ cm the focus still glances over, the swing whiffs,
and the log stays single-exchange.

**`DebugAutoAttackFacingMode` decides whether it aims at you** (`Never` / `WhileAttacking` /
`Always`, set to `WhileAttacking` 2026-08-14). On `Never` the dummy holds its placed yaw no matter
where you stand, which is a *useful control* and an easy thing to mistake for broken hit detection.
**Read `TARGET commit`'s bearing to tell which mode you are in** — a bearing pinned near ±90 while
you circle is a dummy that is not turning.

**The defender is a second dummy, and it is the fixture change of 2026-08-15.** `L_CombatTest` now
holds two: `BP_TrainingDummy_C_2`, the attacker at (200, 0, 96) yaw 180 — **re-placed 2026-08-18 and
named `_C_0` before that**, which is why older entries call it that; and
`BP_TrainingDummy_C_1`, the defender at (200, −150, 96) yaw 90, facing it. **Measurements do not
span this change** — a third pawn moves nearest-target selection, and every travel baseline taken
before it was taken against a level with one dummy in it. Nor do defensive-feel comparisons span
**2026-08-14**, when the dummy learned to track: two different opponents.

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
180 — actor `BP_TrainingDummy_C_2` since it was re-placed 2026-08-18, `_C_0` from 2026-08-14 and
`_C_1` before that; **the suffix climbs every time it is re-placed and no doc can be trusted on it
without a `find_actors` check** — and
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
`BUFFER`, `REFUSED`, `DEATH`, `REVIVE`, `TARGET`, `AIM ASSIST`, `AIM WEDGE` and `LUNGE STOP`;
`DAMAGED` and `ASC RESOLVE` joined 2026-08-15; **`HITSTUN`/`HITSTUN END`, `STRING` and
`KNOCKBACK` joined 2026-08-16**, and all three went live when sitting 2 authored the values that
arm them — they are no longer silent. **`ROTATE` joined 2026-08-18** and fires only while `bDebugAutoAttackRotateTargets` is set;
**`LUNGE SKIP`** the same day, only under `bDebugSuppressLunge`. **`FACING LOCK` gained `camDelta`
and `since press`** — the camera yaw moved between the press and the commit, which is what
separates an aim bug from a flick finished late; `err` alone cannot. Turn the trace off with `TD.DebugCombatTiming 0` when
combat is not under test.

**Block adds several** *(2026-08-14)*: `BLOCK up` / `BLOCK down` for the guard's edges, `BLOCK cost`
when a guard charges its initial stamina, `BLOCKED` when a hit lands on one — carrying the stamina
damage and the bar remaining, which is the pair that says whether the next hit will break it — and
`GUARD BREAK` / `GUARD END` around the stun. **`BLOCKED` with `remaining=0.0` and no `GUARD BREAK`
beside it is the failure to watch for**: it means the break has been moved somewhere that cannot see
a hit landing on an already-empty bar.

**`DAMAGED` is a health hit's ledger line** *(2026-08-15)* — target, attacker, `damage=` and the
clamped `health=` read back after the effect lands, mirroring `BLOCKED`'s stamina pair. It closed
the last silent resource change: between full and `DEATH`, health previously moved with no line at
all, so three guard-down hits took a defender 100 → 55 invisibly. **Between `REVIVE`s, consecutive
`health=` values step by exactly `damage=`** — the checker asserts that ledger in every `s2-*` run.

**`ASC RESOLVE` names which ability system a character bound** *(2026-08-15)* — `(PlayerState)` or
`(owned fallback)` — on every resolution path, including `OnRep_PlayerState`. It exists because the
two-machine recon could not distinguish a resolved client from an unseeded fallback by any other
channel; the next two-machine run reads it off `TheDream_2.log` and settles `OnRep_PlayerState`.

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

**Parry adds nine, and the pairing to read is window → outcome** *(2026-08-18, extended
2026-08-19)*. `PARRY WINDOW open` carries `until=`, so its span is read the way `BLOCKSTUN`'s is —
the end time, not a duration. Exactly one of two lines follows it: `PARRY SUCCESS`, naming the
attacker it caught, or `PARRY WHIFF` with the `recovery=` it just bought. `PARRY RECOVERY` /
`PARRY RECOVERY END` then bracket the refusal.

***That pair had two causes until 2026-08-19 and now has one.*** A `PARRY RECOVERY` used to appear
with no `PARRY WHIFF` beside it whenever a dodge had just ended, which was the post-dodge gap rather
than a missing line. The dodge's gap is its own state now and prints **`DODGE RECOVERY` /
`DODGE RECOVERY END`**, so the two are separable by tag and a `PARRY RECOVERY` without a whiff is
once again a real anomaly. What changed underneath: a whiffed parry now refuses **every** ability
and holds the movement lock, while the dodge's gap still refuses only a parry.

**The animation adds three, all cosmetic** *(2026-08-19)*. `PARRY MONTAGE` prints once per parry
with the clip length, the marker's trigger time as `gesture=`, and the derived `windowRate=`;
`PARRY GESTURE` fires when the marker passes, carrying the montage `pos=` and `rate=`; `PARRY RATE`
records the switch to the recovery segment's rate.

**`PARRY GESTURE` is ungated, unlike `RELEASE BEGIN`/`END`**, and deliberately: a montage's
notifies cannot be read off the asset by any tool we have, so this line is the only evidence the
marker was ever placed. `gesture=-1.0000` on `PARRY MONTAGE` means no marker was found — the clip
then plays at one rate across window + recovery and an **ungated warning** says so. A missing
`PARRY GESTURE` with `PARRY MONTAGE` present is a marker that was never placed; no `PARRY MONTAGE`
at all means the montage is unassigned on `GA_Parry`.

**`PARRY SUCCESS`'s `gained=` is the *credited* stamina, not the authored reward**, and the two
differ whenever the bar is near full. Today every sample reads `gained=0.0`: a parry costs nothing,
so an unattended parrier never spends and its bar never leaves 100, and the clamp eats the whole
reward. That is the clamp working. It is also why the reward's magnitude is a filed trap rather
than an assertion — see `Docs/Combat-Decisions.md`.

**The waiver adds two, and they are deliberately not simultaneous.** `WAIVER` fires at contact and
names the tag it dropped; `MOVE UNLOCK` fires later, at contact plus that swing's `HitstunSeconds`.
**The gap between them is the derivation, so it is the thing to check** — measured 0.557 against
the light's authored 0.550 on the day it landed. `WAIVER` with no `MOVE UNLOCK` following is
movement never coming back early, which reads in play as an attacker rooted after a hit that
connected.

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

**Arithmetic over authored values counts as measurement, and it is the cheapest instrument here.**
Predict the number from the authored inputs, then check the log against it — a prediction that lands
to the millisecond confirms the whole chain at once, and one that misses tells you which link. On
2026-08-15 exhaustion's recovery was predicted three times as *action end + `StaminaRegenPauseSeconds`
+ Max ÷ `ExhaustedStaminaRegenPerSecond`* and landed on 14.733, 12.028 and 9.279 against measured
14.733, 12.039 and 9.279. **Do this before reaching for a new trace line**; it often removes the need.

**Warnings on `LogTDCombatTiming` are deliberately ungated as a family** — each one describes
authored data that has silently stopped fitting the clip, or an attack that will silently stop
dealing damage. Grep `LogTDCombatTiming, Warning` in `Source/` for
the current list; a count written here would only rot.


**`EXHAUSTED` / `EXHAUSTION END` bracket exhaustion** *(2026-08-15, closing the "nothing traces
exhaustion" gap)*. Both carry the bar, and **the two numbers are the assertion**: the rule is that
exhaustion begins at 0 and ends at Max rather than on a clock, so `stamina=0.0` on entry and
`stamina=100.0` on exit is the check, and anything else says the mechanism has moved. Both fire only
on a real transition. **`EXHAUSTED` prints *before* the `GUARD BREAK` it shares a frame with** — the
blocked hit empties the bar, the delegate exhausts you, and the break follows. **Re-sited the same
day, with `DEATH`/`REVIVE`, from the authority transitions into the `Apply*`/`Clear*` state pairs**
— those run on every machine, so clients now announce all four; the recon's 6-of-24 client-tag
measurement predates this and wants re-measuring.

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

**The notify-drift warning false-positives on a fast windup** *(measured 2026-08-18)*.
`TDReleaseStartTolerance` is **0.03 in montage-seconds** while the runtime open position overshoots
the authored trigger by up to one frame *scaled by the windup rate* — `0.0167 × rate`. At the
light's 1.500 that is 0.025 and clears; at `AM_Attack_S2`'s **3.344** it is 0.056, so the warning
fired on 2 of 6 otherwise-correct swings with a measured overshoot of 0.0338–0.0344. **Compare the
authored value against `MONTAGE`'s `trigger=`, not against the warning** — `trigger=` is the
authored truth and the warning is reading runtime jitter. Do not silence it by nudging
`ReleaseStartSeconds` upward; that fudges authored data to quiet an instrument.

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
- The client received **6 of the 24 trace tags** when measured: `RELEASE BEGIN`/`END`,
  `BLOCKSTUN`/`END`, `GUARD BREAK`/`END`. Death and exhaustion were absent because their logs sat
  on the authority-side transitions — **re-sited into `Apply*`/`Clear*` later the same day**, so
  expect `DEATH`/`REVIVE`/`EXHAUSTED`/`EXHAUSTION END` client-side too; re-measure next run.
- **`BLOCKSTUN until=` reads `0.000` on a client** — that field is server-only state, not a bug.
- **A second `PlayerStart` makes single-player spawn random**, so the loop's `startTransform` is now
  load-bearing rather than a convenience.

### The loop is a living artifact, and that is a standing rule

**Combat surface and loop coverage stay coupled** (2026-08-15, the user's rule) — the binding form
(scenarios in the same package, or a dated trap naming what is now untested; no third option) is in
`CLAUDE.md`'s Working Rules. The failure it prevents is silent: a checker whose scenarios lag the
combat surface still prints a full green table, and nothing in the output distinguishes "seven
scenarios, all passing" from "seven scenarios, and last week's feature is not one of them" — which
is why the choice binds at plan time.

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
| `s1-light` | 0.1 | `Off` | press→`RELEASE BEGIN` 200 ms ±30; elapsed **0.950** +10–35 ms; 0 escalations, 0 coils |
| `s1-heavy` | 0.22 | `Off` | 350 ms ±30; elapsed 1.000 +10–35 ms; exactly 1 escalation, 1 coil |
| `s1-charged` | 0.8 | `Off` | 750 ms ±30; elapsed 1.500 +10–35 ms; exactly 2 escalations, 1 coil |
| `s2-light` | 0.1 | `HoldBlock` | stamina damage exactly 5; `BLOCK cost` per `BLOCK up`; `GUARD BREAK` count equals blocks at `remaining=0.0`; break stun 1.0 s ±25 ms; `BLOCKSTUN` span 0.400 ±20 ms; guard-down `DAMAGED` exactly 15 with the health ledger stepping exactly |
| `s2-heavy` | 0.22 | `HoldBlock` | as above with damage 50, `BLOCKSTUN` span 0.600, `DAMAGED` 25 |
| `s2-charged` | 0.8 | `HoldBlock` | as above with damage 100, `DAMAGED` 40, and **`BLOCKSTUN` never fires at all** |
| `s3` | 0.1 | `PeriodicDodge` | `DODGE`/`DODGE END` paired; clean travel 400–420 cm; dodge from full leaves exactly 50; `EXHAUSTED`/`EXHAUSTION END` paired, entering at 0 and clearing at 100 |
| `s4-string` | 0.1, **taps 3** | `Off` | three swing indices in equal counts; chain gap 0.500 ±45 ms and chain latency 125–175 ms; `DAMAGED` exactly 15 with the ledger stepping; `HITSTUN` spans 0.550 ±20 ms; **`KNOCKBACK` spacing never below the authored value it prints, and n=0 fails** |
| `s4-guarantee` | 0.1, **taps 3** | `PeriodicDodge` | `REFUSED` lines attributed to `State.Hitstun`; **zero `DODGE` between `HITSTUN` and `HITSTUN END`** — the string's guarantee, observable; `HITSTUN` spans as above |
| `s4-block` | 0.1, **taps 3** | `HoldBlock` | `BLOCKED` staminaDamage exactly 5; `BLOCKSTUN` spans 0.350 ±20 ms; knockback never inward |
| `s4-360` | 0.1, **taps 3**, `FacingMode` **Never**, **`bDebugSuppressLunge`** | `Off`, plus the player spawned at (200, 150) opposite the defender | **first burst only**: attacks 1–2 damage **zero** distinct targets, attack 3 damages **two** |
| `s5-parry` | 0.1, **taps 3** | `PeriodicParry` | `PARRY WINDOW` span 0.300 ±25 ms; at least one `PARRY SUCCESS` (**n=0 fails**); credited reward inside [0, 25]; **zero `STRING` link window after a parried swing**; **every `PARRY GESTURE` inside its own window (n=0 fails)** |
| `s5-parry-reward` | 0.1, **taps 3** | `PeriodicParry`, `DebugParryPreBlockSeconds` **4.0**, `DebugParryIntervalSeconds` **5.3** | at least one `PARRY SUCCESS`; `gained` **exactly 25** on every one |
| `s5-parry-whiff` | 0.1, **taps 3** | `PeriodicParry`, `DebugParryIntervalSeconds` **0.5**, **and the defender also auto-attacks** (`bDebugAutoAttack`, interval **0.7**, `bDebugSuppressLunge`) | `PARRY RECOVERY` span 0.600 ±25 ms; `REFUSED` naming **`parry recovery`** at least once; **nothing activates inside a recovery span (n=0 fails)** |
| `s5-cancel` | 0.1, **`bDebugCancelAttackIntoBlock`** | `Off` | zero `RELEASE BEGIN`; zero `DAMAGED`; `BLOCK cost` at least once |
| `s5-waiver` | 0.1, **`bDebugDodgeAfterHit`** | `Off` | attacker `DODGE` within 100 ms of its own `DAMAGED`; `MOVE UNLOCK` present |

**`s5-parry-whiff` needs its own interval, and finding out why cost a run** *(2026-08-18)*. At the
default 1.7 the recovery is **never exercised**: a whiff closes 0.3 s after the press and its
recovery expires 0.6 s later, so the next press arrives 800 ms after the refusal window has already gone.
That run produced 10 whiffs and **zero** `REFUSED` lines naming the recovery — a refusal
that had never once refused anything, while every span assertion passed. At **0.5** the presses land
inside it and roughly every other one is refused.

***And from 2026-08-19 the interval alone is no longer enough — the defender must also attack.***
A parry re-pressed during its own recovery is now **silently dropped with no `REFUSED` line at
all**: `GA_Parry` stays alive across the recovery, so GAS short-circuits re-activating an already
active `InstancedPerActor` ability *before* `CanActivateAbility` runs, and the refusal never
traces. Confirmed by a run where 26 recovery spans and 26 in-span presses produced **zero**
refusals while the behaviour was entirely correct.

**So the fixture presses something else.** `bDebugAutoAttack` on the *defender*, interval 0.7
against the parry's 0.5 to sweep phases, with `bDebugSuppressLunge` to keep it planted. Its attack
presses land inside its own parry recoveries and are refused there — 138 of 139 refusals in the
verifying run were `GA_Attack`. **That is not a workaround, it is the correct test**: the 2026-08-19
ruling is that you cannot *act* during parry recovery, and a fixture that only ever presses parry
cannot observe the rule at all.

**The general form is worth keeping: a span assertion and a refusal assertion need different
fixtures.** Measuring how long a recovery *lasts* only requires it to exist; proving it *does*
anything requires something to arrive while it is up, and a co-prime sweep is specifically designed
to avoid that collision.

**Successes are rare, and the run has to be long enough to admit that.** Measured 2026-08-18 at
taps 1: **1 success in 14 windows** over 40 s, because a 300 ms window has to meet a 150 ms release
inside a 3 s attack cycle. Taps 3 improves it by putting three hitboxes in each cycle. **Budget
minutes, not seconds** — and a run reporting zero successes is a fixture that never met an attack,
which is why `s5-parry` fails on n=0 rather than passing vacuously.

**`s5-cancel` and `s5-waiver` are the two scenarios where the *attacker* defends**, which every
other row treats as a fixture error. They are the exception on purpose: the pre-commit cancel and
the on-hit waiver are both rules about what an attacker may do mid-swing, and no arrangement of one
attacker and one defender can witness them otherwise. Both knobs default off, so no existing
scenario changes.

**`s5-parry-reward` exists because a parry costs nothing, and that makes its own reward invisible.**
An unattended parrier never spends, so its bar sits at 100 and the clamp trims the whole +25 —
`s5-parry`'s samples all read `gained=0.0`, correctly. The fix is to make the parrier *spend first*:
`DebugParryPreBlockSeconds` holds a guard before each attempt, because **blocking is the only
spender that authors no displacement** and so does not carry the parrier out of the exchange the
way a dodge would.

**The timing is the fussy part and the arithmetic is worth keeping.** Raising a guard costs 10 and
holding drains 10/s, so 4 s spends 50 and lands the bar near half — under the 75 above which the
clamp starts trimming. But regen is **40/s** after a 0.5 s pause, so the bar is back over that
threshold about **1.1 s** after the guard drops. The parry is therefore tapped a frame after the
release, and its whole window closes before regen has even resumed. **Raise the pre-block far
enough to break the guard and the fixture measures its own break instead** — a break refuses every
ability, the parry included.

**The `s4-*` bands come from `GA_Attack`'s CDO, not from the plan session.** Three of the plan's
proposals were stale by the time they were built — cadence 350 → **500 ms** once it was measured off
the designer, hitstun 0.400 → **0.550** forced up to outlast it, blockstun 0.400 → **0.350**
re-derived against it. Read the CDO when adding a band; do not copy a plan.

**`s4-360` asserts the first burst and nothing after it, and that is the design rather than a
shortcut.** The finisher hits both bodies and then knocks them onto the attacker's facing axis, so
from the second burst they sit inside the 60° wedge and the earlier attacks reach them too. **Do not
"fix" a failure by widening the sample** — that measures contaminated geometry. The exclusion lifts
when Knockdown & Oki replaces the ender's displacement with a knockdown.

**Its fixture is unlike every other scenario's and all three parts are load-bearing.** `FacingMode`
**Never** holds the placed yaw so both targets sit ~90° off-axis, outside the 60° wedge even with
the ~24° a capsule subtends at that range. **`bDebugSuppressLunge`** is what makes the attacker
stationary: a whiff into open space has an open standoff gate and runs the full authored lunge, so
the hitbox goes live far from the scenario. `bDebugAutoAttackHomeBetweenAttacks` is **not** a
substitute — the lunge and the release both happen *inside* one attack, so re-homing afterwards is
too late. And the player is the second target, via `startTransform`, so no level change is needed.

**Made to fail on purpose 2026-08-18**: with `FacingMode` back on `WhileAttacking` the attacker
turns toward one body, attacks 1–2 reach it, and the first assertion fails — while the second still
passes, because a 360° volume short-circuits before any bearing test and genuinely does not care
where the attacker is looking.

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
band to admit them** — that is fitting the band to contamination. **A duration gate rides beside
it** *(2026-08-15)*: the final dodge before `StopPIE` ends mid-travel with *zero* drift — measured
141 cm at 0.14 s — so only dodges running at least `BAND_DODGE_MIN_DURATION` (DodgeSeconds minus a
frame) count as travel samples at all.

## The post-change verification checklist

**Moved here from `Docs/Working-In-Unreal.md` on 2026-08-18**, by that file's own rule that its
growth belongs to it and the project's does not — this list grows one line per combat feature,
forever, which is the shape of a doc read by whoever is measuring rather than one read every
session. The general rule stays there; the combat specifics are here.


- Damage lands in **exact expected multiples**, not "a bar moved"
- Abilities still grant, and end cleanly (`bIsActive: false` at rest)
- **No stuck state tags.** `State.Attacking` is activation-blocking, so a leak disables all future
  attacks; a leaked `State.Attacking.Committed` forbids every future *defensive* action, and a leaked
  `State.Dodging` leaves the character permanently invulnerable
- Locomotion and jump, whenever input or movement code was touched
- **The attack still plays its montage**, whenever meshes, skeletons or animation assets were
  touched. The tell that it is *not* is the absence of `RELEASE BEGIN`/`END` — those come from a
  notify, so they only fire if the montage really ran, while everything else looks healthy either
  way. **An attack that silently deals no damage is the failure mode.**
- `LogAbilitySystem` free of new warnings
- **Death and revive leave nothing stranded.** Die *in mid-air* specifically: `DisableMovement` stops
  the fall so `Landed()` never fires, and anything keyed to landing stays set past the revive

With the stamina economy involved, add:

- **Exact values, regen resumption and the exhaustion pair are all asserted by `s2-*`/`s3`** — a
  dodge from full reading exactly 50, regen resuming at action end plus `StaminaRegenPauseSeconds`,
  and exhaustion entering at 0 and clearing at Max rather than on a timer. `Docs/Combat-Spec.md`'s
  Stamina section is the rule; the checker is the check
- **Stamina can now be drained unattended** *(2026-08-15, replacing "nothing in the build can drain
  stamina without a human at the keyboard")* — `ETDDebugDefendMode` on the training dummy holds a
  guard or dodges on a timer. **The attribute set still cannot be written through the toolset** —
  `SpawnedAttributes` is not reflection-readable — so *setting* a bar to an arbitrary value remains
  impossible; you drive it by spending, not by assignment
- **Attribute *base* values are clamped, not just current.** A base drifted above Max is invisible on
  the bar and makes every cost read wrong
- **Costs never gate.** Dodging below the cost must still work and empty the bar

Most of this is checkable without UI via `AbilitySystemInspectorToolset` against the `UEDPIE_0_`
actors while PIE runs. **Those calls are separate round-trips, so a snapshot can straddle a state
change** — an ability reading `bIsActive: false` beside a live `State.Attacking` is usually sampling
skew. Take several samples before believing one.

