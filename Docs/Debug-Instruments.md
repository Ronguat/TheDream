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

**Two warnings are deliberately ungated**, because both describe an attack that silently stops
dealing damage: a skipped coil, and a `ReleaseStartSeconds` drifted from its notify.


**Not every state is traced.** There is **nothing for exhaustion** — absence from the log is evidence
nobody logs it. Confirm with `GetActiveTags`, or infer from a `BUFFER ...Dodge: expired`. The list is
greppable: `grep -rn "TD_TIMING_LOG" Source/`.

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
