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

**`DebugAutoAttackStringTaps` makes each cycle throw a string** *(2026-08-16, inert at its default 1)*.
Above 1, the dummy re-presses every `DebugAutoAttackStringTapIntervalSeconds` (**0.5 as of
2026-09-02**, was 0.25) so each tap lands inside the previous swing's chain-open span. **Anything
under roughly 0.28 no longer chains at all** — the press expires before the span opens, and the old
0.25 worked only because the buffer extension held it; with the extension dropped it produced a
two-swing string while every timing assertion still passed. **The home reset waits for the string**:
taps remaining suppress it, or a teleport would sever the spacing chain s4 measures. The whole
string must fit inside `DebugAutoAttackInterval`, exactly as the single attack must.


**`bDebugPeriodicJump` is orthogonal to `DebugAutoDefendMode`, and that is the point** *(2026-08-20)*.
A defender has exactly one defensive *policy* at a time and that exclusivity is deliberate, but the
jump is a second **input** rather than a second policy — and the rules it exists to observe need a
pawn that blocks *and* jumps, or is floored *and* jumps. So it sits beside the enum with its own
`DebugJumpIntervalSeconds` (default 1.3, co-prime with the attacker's cycle for the reason the
parry's interval is) and `DebugJumpInputTag`. It reaches the neutral stand and nothing else; the
other get-up options still have no fixture — see the traps.

**Every dummy is brought home at the stand boundary by default** *(widened 2026-08-24 from
fixture-armed pawns only; `bDebugHomeAtStand`, instance-writable)*. The home transform is captured
for every pawn at BeginPlay, so the world-origin teleport is gone by construction, and a player
pawn is never teleported. Before the widening a knob-less victim was never homed, each knockdown's
450 cm carry compounded, and the attacker eventually re-targeted the parked player —
**displacement measurements do not span 2026-08-24.**

**Placed-actor fixture knobs are level state, and an editor restart reverts every one of them**
*(2026-08-20, after it silently invalidated a control run)*. They are `EditAnywhere` instance
writes, so they live in the `.umap` — and the level is deliberately never saved, because saving it
while a CDO write is not yet live bakes stale overrides into the placed actors. **So re-set the
whole fixture after every restart**, and treat a scenario that suddenly measures the wrong tier as
a reverted knob before anything else. The tell is cheap: `ESCALATE` counts say which tier is
actually being thrown.

**After a CDO session, save by naming the assets** — the empty-list form sweeps the level in and
bakes stale overrides onto placed actors. The rule and both forms live in
`Docs/Working-In-Unreal.md`; it is recorded there rather than here because it corrected a claim that
file was already making.

**Close the animation editor before measuring — its preview actor fires notifies into the same
log** *(2026-08-19)*. An editor left open on `AM_Parry` loops its preview, and every loop emits a
real `PARRY GESTURE` line from `AnimationEditorPreviewActor_0`. A sweep collected **8 preview
gestures against 6 from the actual defender**, all of the preview ones falling outside any parry
window — so an unscoped assertion reports a montage fault that does not exist. The checker now
scopes gesture lines to the parrier by name, which covers the times somebody forgets, but the
noise is still in the log for anyone reading it by eye. **Any notify on an open asset does this,
not just this one.**

**An apostrophe in a comment inside a single-quoted `awk` program silently truncates it**
*(2026-08-19)*. The checker writes its extractors as `awk '...'`, so a `'` anywhere inside —
including in an explanatory comment like *"the montage's first advance"* — closes the shell string,
and everything after it never reaches awk. **`bash -n` does not catch it**: the result is still
valid shell, it just means something else. The symptom is an extractor that returns nothing, which
presents as an assertion passing on a healthy sample count while examining none of it. It cost an
afternoon of `parry gesture reads inside the window` reporting PASS on 30 samples.

**The reliable protection is not a linter, it is the injection rule below** — an extractor proven
by injecting a violation into a real log cannot be silently truncated, whatever the cause. A scan
for stray apostrophes was tried and abandoned as unreliable: distinguishing a quote inside an awk
program from one in the shell comment two lines later needs a real parser.

**Count samples within the current PIE session, not across the log** *(2026-08-19)*. The log
accumulates across PIE sessions for as long as the editor stays up, so a wait-loop counting the
whole file stops a run on samples an *earlier* session produced — a sweep waited for 8 parry
successes, saw 8 from three sessions back, and stopped a run that had zero. The checker itself is
immune (it slices from the last PIE start), so the failure is loud rather than silent: the run
reports "no samples" instead of passing wrongly. `Tools/RegressionCheck/session-count.sh <pattern>`
counts inside the current session and is what a wait-loop should use.

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
every attack rather than at string end, which is how a **stationary** attacker is obtained; an
attacker whiffing into open space has an open standoff gate and runs its full authored lunge.

**Between cycles, positions are the safe thing to reset; health and stamina are not.** `s3` and
`s2-*` assert depletion accumulating — exhaustion entering at 0, breaks landing as the bar empties,
the health ledger stepping — while `s4-string` is the one position exception: it measures the
spacing chain a *connecting* string produces, which a mid-string teleport severs.

**The attacker sometimes wedges against the ramp and goes stationary mid-attack** *(the designer,
2026-08-18)*. Nothing warns about it, and it silently corrupts anything measuring attacker travel —
lunge distance, spacing, knockback. If a travel figure looks impossibly small, check where it was
standing before believing it.

**Spawn the player pawn out of the exchange — `startTransform` (−3000, −3000, 100)** *(moved
2026-08-24: knockdown's 450 carry let the defender drift past 800, and the attacker then chased the
player onto the ramp; (0, 800, 100) was 2026-08-16's answer to the same failure at (0, 0, 100))*. The attacker re-focuses on the **nearest living pawn**, so during a
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
**`LUNGE SKIP`** the same day, only under `bDebugSuppressLunge`.
**Two traps in scoring an `s8` run, both paid for 2026-09-02.** `bDebugAutoAttack` is read **once at
BeginPlay** to register the attack loop, so silencing a dummy from inside a running PIE does nothing
and the log carries a second attacker — **silence it before StartPIE, like every other fixture knob.**
And `COMMIT` and the chain-out lines carry **no pawn name**: attribute a commit to the pawn on the
`AIM WEDGE` line sharing its timestamp, or counts credit whoever is in the log. A `REFUSED` burst is
also **one line per retry tick, not per press** — 69 lines were three presses, and the burst's length
is now the acceptance window.

**`KNOCKDOWN <pawn> rose on held <tag>` joined 2026-09-02**, printed when the floor's input window admits a get-up whose button was already down. It sits beside the `KNOCKDOWN RISE ... by=` line, which names the option; a rise with no `rose on held` line came from a press, not a hold.

**`STRING`'s variants changed 2026-09-02**: `link window open ... until <t>` is gone with the window
it reported, replaced by `advance marked on <pawn> (after swing N)`, which carries **no deadline**
because there is none — the mark is consumed by the activation in the same tick. `chain out of
swing N, <n>ms into recovery` and `reset on <pawn> (<reason>)` are unchanged. **A grep for the old
string returns nothing and means only that**, not that chaining stopped.

***`ACTIVATE` gained its avatar's name on 2026-08-19 and the format changed: `ACTIVATE   <Actor>
swing=N ...`.*** Anything parsing it with fixed spacing before `swing=` breaks silently.
**The reason is worth generalising:** attack abilities are `InstancedPerActor`, so every combatant
owns one and the line was unattributable in any fixture where more than one of them attacks — an
assertion about the defender counted the attacker's swings. That is exactly why `REFUSED` gained an
avatar name on 2026-08-12, and **the fix had simply never been carried across to the other tags**.
`DODGE` still carries none; `BLOCK`, `PARRY *`, `REFUSED` and `DAMAGED` do. Worth checking before
writing any assertion that has to know *whose* event it is.

**`FACING LOCK` gained `camDelta`
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

***That pair had two causes until 2026-08-19, one until 2026-08-25, and now has one that cannot
fire.*** A `PARRY RECOVERY` used to appear with no `PARRY WHIFF` beside it whenever a dodge had just
ended, which was the post-dodge gap rather than a missing line. The gap took its own state and
printed **`DODGE RECOVERY` / `DODGE RECOVERY END`**, separating them by tag; it is now retired to
0 ms, so neither line appears at all and `s3` asserts their absence. A `PARRY RECOVERY` without a
whiff is a real anomaly either way. What changed underneath: a whiffed parry refuses **every**
ability and holds the movement lock.

**Parry Grace adds a pair, and the override adds one** *(2026-08-19)*. `PARRY GRACE` / `PARRY GRACE
END` bracket the 150 ms tail a *successful* parry leaves behind, read off `until=` like the
recoveries. `PARRY SUCCESS` gained **`by=window` or `by=grace`**, which is what makes the no-re-arm
rule assertable at all — every tail must follow a `by=window`, and a `by=grace` must start none.
`PARRY RECOVERY OVERRIDDEN` fires when a lockout supersedes a running recovery and prints
`remaining=`, so the waived time is visible rather than inferred.

**Each jail phase refuses under its own name — `parrying` for the window, `parry recovery` for the
recovery — and the ratio between them is a health check.** The recovery is twice the window, so it
should collect roughly twice the refusals; measured 81 against 394 across 35 windows.

***This was briefly untrue, and the failure mode is the one worth remembering.*** For a few hours on
2026-08-19 the recovery's refusal was **unreachable**: `State.Parrying` rode in `GA_Parry`'s
`ActivationOwnedTags`, so a whiffed parry keeping the ability alive across its recovery left the tag
up for the whole 900 ms jail, and its check — which runs first — shadowed the recovery's entirely.
It measured 222 "parrying" and **zero** "parry recovery" while the lockout worked perfectly, so the
symptom was an assertion failing against correct behaviour. **The general form: a tag borrowed from
an ability's lifetime silently re-scopes itself whenever that lifetime changes.** The tag now tracks
`bParryWindowOpen` instead, and both halves are asserted separately — together they would hide
either one going silent.

**An ungated warning marks a sacredness violation**: *"GA_Parry ended … with its window still
open"*. Parry is sacred, so an ability ending mid-window means something cancelled a committed
parry, which the design forbids. The window is left running rather than torn down. Nothing in the
project can trigger it today; Knockdown and ability effects are the candidates.

**The animation adds three, all cosmetic** *(2026-08-19)*. `PARRY MONTAGE` prints once per parry
with the clip length, the marker's trigger time as `gesture=`, and the derived `windowRate=`;
`PARRY GESTURE` fires when the marker passes, carrying the montage `pos=` and `rate=`; `PARRY RATE`
records the switch to the recovery segment's rate.

**`PARRY GESTURE` is ungated, unlike `RELEASE BEGIN`/`END`**, and deliberately: a montage's
notifies cannot be read off the asset **by the MCP toolset** — `unreal.AnimationLibrary` reads them
from Python, verified 2026-08-22 and measured again 2026-08-27 (`Working-In-Unreal.md`). This line
remains the cheapest evidence the marker was ever placed, and now it is no longer the only one.
`gesture=-1.0000` on `PARRY MONTAGE` means no marker was found — the clip
then plays at one rate across window + recovery and an **ungated warning** says so. A missing
`PARRY GESTURE` with `PARRY MONTAGE` present is a marker that was never placed; no `PARRY MONTAGE`
at all means the montage is unassigned on `GA_Parry`.

**`PARRY SUCCESS`'s `gained=` is the *credited* stamina, not the authored reward**, and the two
differ whenever the bar is near full. Today every sample reads `gained=0.0`: a parry costs nothing,
so an unattended parrier never spends and its bar never leaves 100, and the clamp eats the whole
reward. That is the clamp working. It is also why the reward's magnitude is a filed trap rather
than an assertion — see `Docs/Combat-Decisions.md`.

**Knockdown adds seven, and the pairing to read is entry → rise → stand** *(2026-08-24, enumerated
from the source rather than remembered)*. `KNOCKDOWN` opens the down state and carries the whole
shape at once — `type=`, `lockout=`, `inputWindow=`, `rise=`, `spacing=`, `bearing=`, `z=` and
`airborne=`. A second form, `KNOCKDOWN <name> retyped type=`, fires when an already-down body is
re-floored: the tag is already correct, but the new type's clock would otherwise start invisibly.
`KNOCKDOWN RISE` names the exit in `by=` — **`auto` `stand` `dodge` `kipup` `block` `attack`**, six
tokens covering the wait, the neutral stand and the four options — and prints `stands=` as the
timestamp the rise completes. `KNOCKDOWN STAND` is that instant, and carries `z=` for the airborne
comparison. `KNOCKDOWN MONTAGE` prints per clip with its fitted rate, and **`played=` is `Montage_Play`'s return** — the length actually playing, against `len=` read off the asset before the call. **They match or the montage was refused**, which also raises an ungated warning naming the skeleton as the likely cause; `PARRY MONTAGE` carries the same field. Added 2026-08-24, because a refused montage previously logged exactly like a played one. **`DEATH SETTLE` carries `drift=`**, the horizontal distance the corpse ended from its capsule, read at ragdoll teardown -- the only moment the resting place still exists, since the revive reattaches the mesh.

**`by=` is the whole discrimination and two of its tokens come from one ability.** `GA_Dodge`
answers `dodge` or `kipup` depending on the type it reads off the character, which is why the base
class *asks* for the label rather than storing a constant — the scenarios assert kip-up travel is
about zero and roll travel is not, and they need to know which they are looking at.

**`HOME RESET` is the fixture, not the game** — `bDebugHomeAtStand`, default **true**, returning a
dummy to its placed transform at the **shared rise mark**, which the stand no longer always
coincides with (2026-08-25: an option that shortens its own rise ends the knockdown earlier, and
teleporting there would land inside the travel that option just made) and **never moving a player pawn**. It prints
`moved=`, which separates a real reset from a no-op. Read it before attributing anything to
geometry: it is what puts a riser back inside the attacker's reach, and `s4-360`'s later strings and
`s6-getup`'s landed hit both depend on it.

**`DEBUG GETUP <name> mode=`** is the get-up fixture's press, naming which option it intended —
without it the log cannot tell a get-up dodge from an ordinary one. **`FACING FORCED`** is **one line per hit,
printed at the end** of the turn every cleanly hit victim takes toward its attacker, carrying
`toward`, the `span=` actually covered and the `rate=`. The span is what the derivation wants
checking against — a rate that cannot finish 180° inside the shortest *felt* hitstun leaves a body
still turning when it regains control.


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

**Both PIE worlds are directly addressable, and the client-world claim is refuted** *(2026-08-24)*.
The recorded reading — that a second process writing `TheDream_2.log` is *"the only channel to
client-side state that exists, because the MCP toolset returns only `UEDPIE_0_` actors"* — is true
of the MCP toolset and false of the process. Under `RunUnderOneProcess=True` both worlds live in one
address space and **Python reaches either by path**:

```python
unreal.find_object(None, "/Game/TheDream/Maps/UEDPIE_0_L_CombatTest.L_CombatTest")   # server
unreal.find_object(None, "/Game/TheDream/Maps/UEDPIE_1_L_CombatTest.L_CombatTest")   # client
```

`GameplayStatics.get_all_actors_of_class` then works per world, and **`get_local_role()` tells you
which is which** *(Python, 2026-08-15)* — the server world reports `ROLE_AUTHORITY` on everything and carries the game
mode; the client world has no game mode, reports `ROLE_AUTONOMOUS_PROXY` on its own pawn and
`ROLE_SIMULATED_PROXY` on the rest. **`UTDInputTools` drives the client's player controller too**, so
both sides of an exchange are scriptable from one place. Measured the same day: a moving dummy read
x=**179.5** on the server against **169.2** on the client, 10.3 cm of interpolation lag — the kind of
number this file previously had no way to take.

**Never match actors across worlds by name.** Each world numbers its own actors, so
`BP_PlayerCharacter_C_0` on the server and `BP_PlayerCharacter_C_0` on the client are **different
pawns** — measured as an 85 cm "desync" that was actually two characters swapped. Anchor on
`get_local_role()`, on position, or on a replicated identity; never on the name.

**None of this softens the rule above.** The checker still assumes one world, the two clocks are
still real, and the loop still runs single-player only. What changed is that inspecting client state
no longer needs a second process or a second log.

**The recipe, for when a two-player session is wanted.** Edit
`Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` **with the editor closed** — it is
rewritten on exit, so an in-session edit is lost — setting `PlayNumberOfClients=2` and
`PlayNetMode=PIE_ListenServer` under `[/Script/UnrealEd.LevelEditorPlaySettings]`. That file is
gitignored, so this is machine state and never travels with the repo. **Restore it afterwards.**

- `RunUnderOneProcess=True` is easier to drive but gives **no client-side log**.
- `RunUnderOneProcess=False` spawns a second `UnrealEditor.exe` writing `Saved/Logs/TheDream_2.log`
  — **the only channel to client-side state that exists**, because the MCP toolset returns only
  `UEDPIE_0_` actors and cannot see the client world at all *(MCP, 2026-08-15; not re-tested since,
  and editor Python has not been pointed at a two-process session)*.
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

**Assertions name their `BAND_*` constant rather than its value** *(2026-08-25)*; the values live in
`regression-check.sh`'s config block and are read there. Fixture numbers stay inline, being
settings rather than assertions — as do measurements, which record what a run produced.

| Scenario | Attacker `…HoldSeconds` | Defender `DebugAutoDefendMode` | Asserts |
|---|---|---|---|
| `s1-light` | 0.1 | `Off` | press→`RELEASE BEGIN` `BAND_RELEASE_LIGHT` ±`BAND_RELEASE_TOL`; elapsed `BAND_ELAPSED_LIGHT` + `BAND_ELAPSED_MIN`–`BAND_ELAPSED_MAX`; `BAND_ESCALATE_LIGHT` escalations, `BAND_COIL_LIGHT` coils; **zero** engine `No Inertialization node found` lines in the raw session *(all three s1 rows, 2026-09-02 — the slice drops engine lines, so this one is counted off the raw log)* |
| `s1-heavy` | 0.22 | `Off` | `BAND_RELEASE_HEAVY` ±`BAND_RELEASE_TOL`; elapsed `BAND_ELAPSED_HEAVY` + the same window; exactly `BAND_ESCALATE_HEAVY` escalation, `BAND_COIL_HEAVY` coil; the inertialization line |
| `s1-charged` | **0.85** | `Off` | `BAND_RELEASE_CHARGED` ±`BAND_RELEASE_TOL`; elapsed `BAND_ELAPSED_CHARGED` + the same window; exactly `BAND_ESCALATE_CHARGED` escalations, `BAND_COIL_CHARGED` coil; the inertialization line |
| `s2-light` | 0.1 | `HoldBlock` | stamina damage `BAND_STAMDMG_LIGHT`; `BLOCK cost` per `BLOCK up`; `GUARD BREAK` count equals blocks at `remaining=0.0`; break stun `BAND_GUARDSTUN` ±`BAND_GUARDSTUN_TOL`; `BLOCKSTUN` span `BAND_BLOCKSTUN_LIGHT` ±`BAND_BLOCKSTUN_TOL`; guard-down `DAMAGED` `BAND_HEALTHDMG_LIGHT` with the health ledger stepping exactly |
| `s2-heavy` | 0.22 | `HoldBlock` | as above with `BAND_STAMDMG_HEAVY`, `BLOCKSTUN` span `BAND_BLOCKSTUN_HEAVY`, `DAMAGED` `BAND_HEALTHDMG_HEAVY` |
| `s2-charged` | **0.85** | `HoldBlock` | as above with `BAND_STAMDMG_CHARGED`, `DAMAGED` `BAND_HEALTHDMG_CHARGED`, and **`BLOCKSTUN` never fires at all** |
| `s3` | 0.1 | `PeriodicDodge` | `DODGE`/`DODGE END` paired; **no `DODGE RECOVERY` at all**, the gap being retired, and n=0 dodges fails rather than passing vacuously; clean travel `BAND_DODGE_MIN`–`BAND_DODGE_MAX`; dodge from full leaves `BAND_DODGE_REMAINING_FROM_FULL`; **`fitLen` exactly `BAND_DODGE_FIT` on every dodge that resolved a section** — the dash portion, not the 0.833 section, with get-ups excluded by their `section=None`, and it sees `Bw` only; `EXHAUSTED`/`EXHAUSTION END` paired, entering at `BAND_EXHAUST_ENTER` and clearing at `BAND_EXHAUST_EXIT` |
| `s4-string` | 0.1, **taps 3** | `Off` | `BAND_STRING_SWINGS` swing indices in equal counts; chain gap `BAND_CHAIN_GAP` ±`BAND_CHAIN_GAP_TOL` and chain latency `BAND_CHAIN_LATENCY_MIN_MS`–`BAND_CHAIN_LATENCY_MAX_MS`; `DAMAGED` `BAND_HEALTHDMG_LIGHT` with the ledger stepping; `HITSTUN` spans `BAND_HITSTUN_LIGHT` ±`BAND_HITSTUN_TOL`; **`KNOCKBACK` spacing never below the authored value it prints, and n=0 fails** |
| `s4-guarantee` | 0.1, **taps 3** | `PeriodicDodge` | `REFUSED` lines attributed to `State.Hitstun`; **zero `DODGE` between `HITSTUN` and `HITSTUN END`** — the string's guarantee, observable; `HITSTUN` spans as above |
| `s4-block` | 0.1, **taps 3** | `HoldBlock` | `BLOCKED` staminaDamage `BAND_STAMDMG_LIGHT`; `BLOCKSTUN` spans `BAND_BLOCKSTUN_LIGHT` ±`BAND_BLOCKSTUN_TOL`; **one blocked `KNOCKBACK` per `BLOCKED`**, the ender included; knockback never inward |
| `s4-360` | 0.1, **taps 3**, `FacingMode` **Never**, **`bDebugSuppressLunge`** | `Off`, plus the player spawned at (200, 150) opposite the defender | **every string**, since 2026-08-24: attacks 1–2 damage **zero** distinct targets throughout; attack 3 damages **two** in string 1 and **exactly one** after; at least two normal-grade `KNOCKDOWN` lines. A single string **fails** — sampling past the first is the point |
| `s5-parry` | 0.1, **taps 3** | `PeriodicParry` | `PARRY WINDOW` span `BAND_PARRY_WINDOW` ±`BAND_PARRY_SPAN_TOL`; at least one `PARRY SUCCESS` (**n=0 fails**); credited reward inside [`BAND_PARRY_GAINED_MIN`, `BAND_PARRY_GAINED_MAX`]; **zero `STRING` advance marked after a parried swing**; **every `PARRY GESTURE` inside its own window (n=0 fails)** ; **`PARRY GRACE` span `BAND_PARRY_GRACE` ±`BAND_PARRY_SPAN_TOL`**; every `by=window` success starts exactly one tail; **Grace never re-arms** (no tail from a `by=grace` catch, none overlapping) |
| `s5-parry-reward` | 0.1, **taps 3**, interval **3.0** | `PeriodicParry`, **`DebugParryIntervalSeconds` 6.0**, **`DebugParryPreBlockSeconds` 3.935** | every `PARRY SUCCESS` following a *released* guard credits `gained` **`BAND_PARRY_GAINED_EXACT`**. **The period is locked to the attacker's and the phase is derived from human timing** — 6.0 is two attack cycles, which is what lets the pre-block be both correctly phased and long enough to drain. Measured **6 of 6 catches**, all crediting 25, bar 60.6–70.6 |
| `s5-parry-whiff` | 0.1, **taps 3** | `PeriodicParry`, `DebugParryIntervalSeconds` **0.5**, **and the defender also auto-attacks** (`bDebugAutoAttack`, interval **0.7**, `bDebugSuppressLunge`) | `PARRY RECOVERY` span `BAND_PARRY_RECOVERY` ±`BAND_PARRY_SPAN_TOL`; `REFUSED` naming **the lockout** (`parrying` or `parry recovery`) at least once; **nothing activates inside a recovery span (n=0 fails)**; **nothing activates inside a parry window either (n=0 fails)** |
| `s5-cancel` | 0.1, **`bDebugCancelAttackIntoBlock`** | `Off` | zero `RELEASE BEGIN`; zero `DAMAGED`; `BLOCK cost` at least once |
| `s5-waiver` | 0.1, **`bDebugDodgeAfterHit`** | `Off` | attacker `DODGE` within `BAND_WAIVER_DODGE_MAX_MS` of its own `DAMAGED`; `MOVE UNLOCK` present |
| `s6-knockdown` | 0.1, **taps 3** | `Off` | the ender's knockdown: every `KNOCKDOWN` reads `type=normal`; entry→rise **`BAND_KD_ENTRY_TO_RISE` ±`BAND_KD_SPAN_TOL`**; rise→stand **`BAND_KD_RISE` ±`BAND_KD_SPAN_TOL`**; **the fall landing inside its own lockout** — the *montage* span, `(played - from) / rate` off the `KNOCKDOWN MONTAGE` line against the `KNOCKDOWN` `lockout=`, because a fitted portion lets the montage outlast `want=`; authored values rather than measured, so it is frame-rate-proof; **zero `DAMAGED` between a knockdown and its rise** (floor invincibility, and it fails on n=0 knockdowns rather than passing vacuously); every rise `by=auto` |
| `s6-hard` | 0.22 | `Off` | the same spans from the other grade — `type=hard`, entry→rise `BAND_KD_ENTRY_TO_RISE`, rise→stand `BAND_KD_RISE`. **The total is type-invariant by design**, which is why this cannot see the 1.5/0.5 split and `s6-stand` exists |
| `s6-stand` | 0.1, **taps 3** | `Off`, plus **`bDebugPeriodicJump`** (interval **1.3**) | the lockout made observable: presses inside it are `REFUSED … knocked down (lockout)`, and the first press after it fires `RISE by=stand`. Chosen stands must land in **[jail, auto-rise)** — measured n=9 all within [1.000, 1.975] s |
| `s6-getup` | **0.22** (hard singles) | `Off`, plus **`DebugGetUpMode` `AttackGetUp`** | the fixture's press inside the **hard** input window; rise `by=attack`; press→`RELEASE BEGIN` `BAND_RELEASE_GETUP` ±`BAND_RELEASE_TOL`; the authored `BAND_ELAPSED_GETUP` total; the riser's hit landing; no `STRING` after. **Run unattended** — a human's floor presses rise `by=attack` on their own pawn and poison the counts |
| `s6-dodge` | 0.1, **taps 3**, **interval 6.0** | `Off`, plus **`DebugGetUpMode` `DodgeGetUp`** | **rise-to-stand equals `DodgeSeconds`, not the shared rise** — the i-frame assertion beside it stops at `DODGE END`, which is where the old gap opened; rise `by=dodge` inside the normal input window; **zero rises `by=kipup`**; `remaining=` **`BAND_GETUP_DODGE_COST`** on every get-up dodge; travel `BAND_DODGE_MIN`–`BAND_DODGE_MAX`; **zero `DAMAGED` on the riser between its rise and its `DODGE END`** — the i-frames, observable. **The 6.0 interval is load-bearing**: at the default 3.0 the attacker re-engages before regen tops the bar, and the cost assertion then reads a partly-regenerated bar instead of the authored cost |
| `s6-kipup` | **0.22**, **taps 1**, **interval 6.0** | `Off`, plus **`DebugGetUpMode` `DodgeGetUp`** | **rise-to-stand equals `DodgeSeconds`**, same gap, same reason; the same input on hard: rise `by=kipup` inside the hard input window; **zero rises `by=dodge`**, the directional form never yielded; travel **0–`BAND_KIPUP_TRAVEL_MAX`** — slack for capsule settle, not a travel budget; cost `BAND_GETUP_DODGE_COST` |
| `s6-block` | 0.1, **taps 3**, interval 6.0 | `Off`, plus **`DebugGetUpMode` `BlockGetUp`** | rise `by=block` inside the normal input window; **`BLOCK up` within `BAND_BLOCK_GUARD_GAP` of that rise** — the guard live from activation rather than from the top of the rise, which is the option's whole claim |
| `s6-hard-stand` | **0.22**, **taps 1** | `Off`, plus **`DebugGetUpMode` `StandGetUp`** | hard removes the free stand: `REFUSED … no stand from a hard knockdown` at least once, **zero rises `by=stand`**, and the auto-rise still arriving on the full `BAND_KD_ENTRY_TO_RISE` clock — a removed option rather than a broken one. The refusal and the absent rise are asserted separately, so a silent no-op fails |
| `s6-exhaust-regen` | 0.1, **taps 3** | `PeriodicParry`, `DebugParryPreBlockSeconds` **12.0**, `DebugParryIntervalSeconds` **13.0** — the pre-block drains to a break, and knockdowns land inside the exhaustion that follows | **the exhaustion exception**, asserted as time that fails to appear: every `EXHAUSTED` → `EXHAUSTION END` span containing a knockdown matches `pause + max÷exhausted-regen` (+ the break stun) within **`BAND_EXHAUST_SPAN_TOL`**, so the knockdown cost no recovery. **n=0 fails.** Needs no ledger trace — suppression would add the whole 2.5 s down-span, an order above the tolerance. Measured 6 spans, worst 7 ms |
| `s6-airborne` | 0.1, **taps 3** | `Off`, plus **`bDebugPeriodicJump`** (interval **1.3**) | the airborne carry: at least one knockdown entering `airborne=1` (**n=0 fails**), and of the samples clearing **`BAND_AIRBORNE_MIN_HEIGHT`** above the floor, every one falling back to its own stand — *equal heights across a carry mean the body hung*. **The floor is the lowest grounded stand in the same run**, never the highest: the level has raised geometry and a stand occasionally happens on it. **Rare by nature** — measured 1 airborne in 20 knockdowns, so budget minutes |
| `s7-death` | 0.1, **taps 3** | `Off`, `DebugAutoReviveSeconds` at its default **3.0** | death's own bookkeeping: `DEATH` fires (**n=0 fails**); every death lands at **exactly 0.0** health; every death has a `REVIVE`; `DEATH`→`REVIVE` is **`BAND_REVIVE_DELAY` ±`BAND_REVIVE_TOL`**, tracking the debug affordance rather than a design value; and **zero `DAMAGED` while dead**, plus the impulse read off `DEATH SETTLE` at ragdoll teardown and banded **`BAND_DEATH_SETTLE_LO`–`BAND_DEATH_SETTLE_HI`** against a measured 396-449 — the killing blow shares the death's timestamp and is excluded by a strict comparison, so only a *later* hit counts |
| `s7-death-grade` | **0.22**, **taps 1** | `Off` | death supersedes knockdown on one contact: **zero deaths also produced a `KNOCKDOWN`**, with `KNOCKDOWN` count as the control that grading is live in the run. **The fixture is what makes it rigorous, not the assertion** — on heavies every swing is graded, so any death is necessarily a graded kill. On a light string the lethal blow is hit 7 while enders are hits 3 and 6, so it lands on a swing that would not have floored anyway and the check passes without exercising anything. Both assertions proven by injecting a `KNOCKDOWN` at a death's timestamp |
| `s8-chain-early` | **scripted, not the dummy** — `Tools/RegressionCheck/ue_s8_driver.py` with `{"scen":"chain-early"}`; **silence the dummies before StartPIE** -- the driver cannot, the flag being read at BeginPlay | n/a | a press in the buffered slice (tap at 0.35) chains: at least one chain-out, **exactly one `STRING advance marked` per chain-out**, and one swing 0 per first-press. The advance count is the assertion that matters — a mark standing with no successor is the stale-window failure the link window had |
| `s8-chain-late` | as above, `{"scen":"chain-late"}` — tap at 0.60, inside the open span | n/a | same four assertions; the press fires on arrival rather than waiting for the opening |
| `s8-chain-closed` | as above, `{"scen":"chain-closed"}` — tap at 0.80, past the span's close | n/a | **zero chain-outs, zero advances marked, and every activation swing 0.** This is the row that would go red if the chain span lost its closing |
| `s8-discard` | as above, `{"scen":"discard"}` — tap at 0.70, 250 ms before actionable | n/a | **nothing comes out**: activations equal buffer expiries, no chain-out. The acceptance window's other edge, and the row that would go red if a discarded press started firing again |
| `s8-hold-tier` | as above, `{"scen":"hold-tier"}` — tap 0.00, hold 0.80–1.05 | n/a | the held press commits **heavy**, not light: lights must not outnumber heavies. Before the ladder counted accumulated hold, 9 of 12 such presses committed light against 227–345 ms holds |
| `s8-stale` | as above, `{"scen":"stale"}` — hold 0.00–0.40 for a heavy, then tap at 0.50 | n/a | the held swing activates; **zero chain-outs** (a heavy cannot be chained out of); at least one `BUFFER expired`; and **activations equal expiries**, so no attack fires after the swing that swallowed the press. Before the buffer extension was dropped this produced a stray light 1 up to 1.55 s late |
| `s6-exhausted` (`-kipup`, `-block`, `-attack`) | 0.1 taps 3; **0.22 taps 1** for `-kipup` | **`PeriodicParry`, `DebugParryPreBlockSeconds` 12.0, `DebugParryIntervalSeconds` 13.0** — `s5-parry-reward`'s pre-block trick used for its side effect, plus the matching `DebugGetUpMode` | of the presses landing **while `State.Exhausted` is up** (**n=0 fails**), the three defensive options produce **zero** rises and the get-up attack produces one every time. **The refusal is asserted as the absent rise, not as a `REFUSED` line** — see the note below |

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

**`s5-parry-reward`'s fixture is the worked example of building one from human timing** *(2026-08-24)*.
It was retuned blind three times and never got past a 1-in-20 catch rate. The reference run settled
it: across 15 successful human parries, **every one** had a hitbox aimed at the parrier inside the
300 ms window, at a median of **+206 ms** into it — which is where the light's release lands if you
press on the attacker's activation, since the hitbox opens at ACTIVATE **+207 ms**. Six of seven
human misses had **no hitbox at all**; only one was a timing loss, at the window's exact +300 ms edge.

**Locking the period made the error measurable instead of random.** At `DebugParryIntervalSeconds`
3.0 against a 3.0 s attack cycle the hitbox arrived a stable **−365 ms** before every window opened
— eight samples spanning 23 ms. A sweep gives you noise; a lock gives you an offset you can correct.

**The period is 6.0 rather than 3.0 because the pre-block does two jobs.** It sets the phase *and*
drains the bar, and those wanted 0.935 s and ≥1.5 s respectively. Two attack cycles keeps the lock
while giving the pre-block room for both: **3.935 s** put the hitbox at **+202 ms** on the first
attempt, 2 ms off the human median, and the catch rate went from 1-in-20 to 6-of-6.

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

**`s4-360`'s first-string exclusion lifted 2026-08-24, and the assertion got stronger rather than
merely wider.** It existed because the old ender knocked both bodies onto the attacker's facing
axis, where the 60° wedge reached them and the discrimination vanished. Knockdown replaced that
displacement with a **radial** carry — each victim leaves along its own bearing — so nobody parks in
front of the attacker and attacks 1–2 reach zero targets in *every* string. Measured across **23**:
`0` distinct targets for attacks 1–2 throughout, `2` for attack 3 in string 1, `1` in all 22 after.

**The step from two to one is the fixture, not the mechanic.** `bDebugHomeAtStand` returns a dummy
to its placed spacing at every stand and **never moves a player pawn**, so the player stays out at
the carry's 450 cm while the dummy comes back into reach. Asserted as *exactly* one: two would mean
the carry had stopped separating, zero that the re-home had stopped working. **The plan predicted
zero** — written before the re-home became unconditional, and corrected by measurement.

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

**The dodger's travel needs the lateral filter or a fifth of the samples are wrong.** The checker
keeps only `DODGE END` lines with `|right| ≤ 1.0`; 5 of 22 in the reference run read ~297 cm with
`right≈-67`, all of them the attacker colliding with a displaced dodger. **Never widen the distance
band to admit them** — that is fitting the band to contamination. **A duration gate rides beside
it** *(2026-08-15)*: the final dodge before `StopPIE` ends mid-travel with *zero* drift — measured
141 cm at 0.14 s — so only dodges running at least `BAND_DODGE_MIN_DURATION` (DodgeSeconds minus a
frame) count as travel samples at all.

**A refusal is asserted as the rise that did not happen, never as the `REFUSED` line** *(2026-08-24,
learned by watching one go missing)*. That line dedups per reason for half a second, so any defend
mode pressing the same ability the get-up mode presses swallows the get-up press's own refusal:
`HoldBlock` with `BlockGetUp` refused a press at 8.274 whose line did not print until 8.733, already
attributed to the guard spam. **The absent rise is the same fact and cannot be deduped**, so every
`s6-exhausted*` scenario asserts behaviour rather than logging.

**The exhausted fixtures cannot hold the tag up, so the assertion is scoped to the presses where it
was** *(fixture behaviour, measured 2026-08-24 — a property of the scenarios, not of any scripting
surface)*. A get-up that succeeds refills the bar, and the loop then settles with the defender never
re-exhausting -- `HoldBlock` with `BlockGetUp` exhausted once in sixty seconds and not again, while
`PeriodicDodge` never exhausted it at all. Each scenario therefore counts only presses made while
`State.Exhausted` was up and **fails on n=0** rather than passing on a run where the defender was
never exhausted. Measured on the pre-block drain: **2 of 6** presses for `-block`, **5 of 8** for
`-attack`. The `-block` sample is small and known to be; the n=0 gate is what stops it degrading
silently.

**`s6-airborne` is rare and that is the fixture being honest.** The jump and the attack cycle are
deliberately non-aliasing, so hits sweep the jump arc rather than meeting the same phase every time
— which is what produces a range of entry heights, and also why most hits land on a grounded body.
Measured **1 airborne in 20** knockdowns, the airborne one entering at `z=181.0` against a floor of
`z=98.2` and standing back at `98.2`: an 82.8 cm fall, with `IgnoreZAccumulate` letting gravity keep
the vertical.

**The height test runs before the hang test, and that ordering is the design.** A body floored 2 cm
off the deck is airborne by the flag and has nothing to fall, so it can neither hang nor be seen not
to — the three samples logged before this scenario existed were all of that kind. Only samples
clearing the height bar carry the hang assertion.

## Charting a bone through time, in world space

**The capsule is not what the player looks at, and until 2026-08-28 nothing here measured anything
else.** Four rounds of knockdown tuning each moved a capsule number and left the feel unfixed,
because the animation moves the body *inside* the capsule and the two were cancelling.

**`SkeletalMeshComponent::GetSocketLocation` resolves bone names and returns world space, live in
PIE** *(Python, confirmed 2026-08-28)*. Reach the component with
`actor.get_component_by_class(unreal.SkeletalMeshComponent)`, then `get_socket_location("pelvis")`.
Nothing needed building; it had simply never been asked for.

**Pair it with time dilation or it sees nothing.** A round trip through `run-in-editor.py` costs
**0.7 to 1.0 s of wall clock** on this machine — measured, 290 samples across 200 s — so at normal
speed a 0.5 s event yields **less than one sample**. At `set_global_time_dilation` **0.10** it yields
five to seven, and at **0.04** roughly fifteen. Restore dilation to 1.0 before reading any trace
timestamp. **The console's `slomo` is the same knob** — `UCheatManager::Slomo` calls
`WorldSettings->SetTimeDilation`, which is what `set_global_time_dilation` uses — so a human can
drive it from the PIE console while a script samples.

**Sample the same actor's own floor, never another's.** The first run of this measured lift as peak
minus floor across *both* dummies -- floor from the attacker at 96.0, peak from the victim standing
at 98.15 -- and invented a 41 cm ceiling that did not exist. `Docs/Working-In-Unreal.md` states that
trap; it was quoted the same day it was walked into.

**What it is good for beyond knockdowns**: any question of the form *"the mechanic is correct and it
looks wrong"*. It answers where the body actually is, which is the only thing a feel verdict is ever
about.

### Charting the tier hand-off, and shooting it

**`Tools/ClipScan/ue_chart_ab.py` measures what a blend does to the rendered hand** *(2026-09-02)*.
It injects a tap-then-hold plan through `UTDInputTools` on the PIE player pawn, samples `hand_r`
in component space every slate tick under 0.10 dilation alongside every candidate montage's
position, and optionally shoots stills through a window with the camera boom shortened.
`ue_ab_metrics.py` reduces a chart to the speed step across the swap tick, the largest one-tick
jump, and a **roughness** figure: the mean absolute change in hand speed per 10 ms bin over the
first 200 ms, with acceleration reversals and path length beside it. **Shots stall the tick**, so
roughness comes from runs made without them. **The pawn is teleported home before each run**;
every attack lunges it forward and twelve runs walked it off the floor.

**Read it against the shipping numbers.** On 2026-09-02, standard crossfade against inertial
blend on the shipped clips: L2→H2 79 against 27 cm/s per bin, L3→H3 417 against 43, C3 1443
against 62, with a 35 cm one-tick pop 60 ms into every crossfade out of light 3 and a 110 cm one
at the charged swap after it, none inertial. L1→H1 reads 117 against 156, C1 120 against 116,
C2 75 against 70: inertial wins where the outgoing clip is fast, and ties where both sides are
gathers. The blend-in sweep is the other reading, in the tuning map. **What it measures is
velocity continuity, not look**: V1 Attack2 had the smallest pose gaps of any charged candidate
and the largest inertial transient from H1, because its hand was moving 109° off the heavy's at
entry. Stills are for the eye; the chart is for the number.

## Build a scenario from a human demonstration, not from intuition about the fixture

**Amended 2026-08-24: the *driving* half of this is obsolete; the *measuring* half is not.**
`UTDInputTools` injects real Enhanced Input into PIE — taps and timed holds — so a fixture no longer
has to be a blind periodic timer, and a defender can be made to act at an exact game time by script.
**What a human demonstration is still for is learning what a human cadence *is***, which is a fact
about people and not about the fixture. The 500 ms cadence this project derives from remains the
one number measured off a person. Read the rest of this section as: measure off a human when the
question is *what should the timing be*, and script it when the question is *make this happen at
that time*.

**Two instruments arrived with it.** `GameplayStatics.set_global_time_dilation` works from Python
during PIE — 0.04 turns a 0.55 s window into nearly fourteen seconds of wall clock — and
`AutomationLibrary.take_high_res_screenshot` captures the **game** viewport with the debug HUD live,
writing to `Saved/Screenshots/WindowsEditor/`. `CaptureViewport` does **not**: it renders the editor
world. Together they make any moment in combat observable — slow the world, drive the input, poll
for the state, shoot. Restore dilation to 1.0 before reading any timing off the trace.

**Ruled 2026-08-24, by the designer, after watching the alternative fail three times in a row.**
When a new scenario needs a defender to *do* something at a *time* — parry, jump, get up — have a
human perform it first, measure their timing off the trace, and configure the fixture to reproduce
it. Do not guess at intervals.

**The reason is structural, not a matter of skill.** A fixture presses on a blind periodic timer
against a periodic world, and there are only two outcomes. Set the periods equal and it **aliases**
— every press lands at the same phase forever, so it either always hits or always misses and the
sample is one observation repeated. Set them unequal and it **sweeps** — phases drift, most presses
miss, and successes arrive at whatever rate the arithmetic allows. Neither resembles a human, who
presses *reactively* and therefore at a **consistent offset from the tell**.

**That offset is the thing worth having, because it transfers directly.** Once measured, set the
fixture's period equal to the attacker's — aliasing *deliberately*, which is the one case where it
is correct — and its phase to the measured offset. For the parry fixture the phase control is
`DebugParryPreBlockSeconds`, since the parry fires that far into each cycle; it doubles as the
stamina drain.

**Ask for failures as well as successes.** The gap between the offsets that land and the ones that
miss is the slack the model has, and it is what tells you whether a band is tight or generous.

**The method, in order:**

1. Configure the *attacker* exactly as the scenario will have it. Everything measured is relative
   to its cycle, so changing it afterwards invalidates the reference.
2. Make every other pawn inert, so nothing competes for aim assist or the trace.
3. The human plays the defender's part, deliberately mixing outcomes.
4. Measure the press against the attacker's `ACTIVATE` and `RELEASE BEGIN`, split by what followed.
5. Set the fixture's period and phase from that, then run it unattended and compare.

**A human-in-the-loop log is a reference measurement and never a scenario run.** Human presses
poison the counts — `s6-getup`'s matrix row already says so — so label the log as reference and
re-run unattended before trusting any assertion against it.

**What it cost not to do this**, kept because the shape recurs: `s5-parry-reward` was retuned blind
three times, each wrong for a different reason — a pre-block that monopolised the parry's own
window, a taps setting that removed the interference *and* the spacing maintenance together, and a
non-aliasing period that swept phases into a 1-in-20 success rate. All three were reasoned about
rather than measured.

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
  guard or dodges on a timer. **No scripting surface writes an attribute base directly**
  *(re-tested 2026-08-27, held at two surfaces of three)* — `SpawnedAttributes` is not
  reflection-readable, and `AbilitySystemLibrary`'s 132 members carry only getters
  (`get_float_attribute_base`). **C++ lifts it and the symbol is confirmed**:
  `UAbilitySystemComponent::SetNumericAttributeBase` is public `UE_API`
  (`AbilitySystemComponent.h:233`), so a debug setter is a build rather than an unknown — worth it
  only when a fixture needs an arbitrary starting bar. Until then, applying a GameplayEffect from
  Python is the route that already exists, and you drive a bar by spending, not by assignment
- **Attribute *base* values are clamped, not just current.** A base drifted above Max is invisible on
  the bar and makes every cost read wrong
- **Costs never gate.** Dodging below the cost must still work and empty the bar

Most of this is checkable without UI via `AbilitySystemInspectorToolset` against the `UEDPIE_0_`
actors while PIE runs. **Those calls are separate round-trips, so a snapshot can straddle a state
change** — an ability reading `bIsActive: false` beside a live `State.Attacking` is usually sampling
skew. Take several samples before believing one.

