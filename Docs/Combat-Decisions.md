# Combat decision log

What was decided, why, and what is still open. Newest entries at the top.

This file records **reasoning**, not implementation. Code and Blueprints are the
authority on how things work and what the numbers currently are; entries here explain
why the shape is what it is, and stay true even after the code moves on. An entry is
never rewritten to match new code — a reversal gets a new entry that supersedes it.

Append an entry whenever a gameplay or combat choice is made that a future reader could
reasonably second-guess.

**The bar, raised 2026-08-11: does this record something the code cannot?** In practice that
means **what was rejected, and the mechanism by which it failed** — because code contains only
the winner. "We built an authored `IFrameSeconds` inside a longer dodge and dropped it", "we
authored per-branch commit rates and the windup rate carried through the release and halved the
light's active window", "we raised letting stamina go negative and rejected it": none of that is
recoverable from a codebase, and every one of them is a proposal somebody will make again.

If an entry would be equally true as a header comment, **write the header comment instead** —
code cannot drift from itself. Audited on the day this bar was set: 9 of the first 27 entries
cleared it; the rest explained the current design, which the codebase does better.

**How to read this file.** The sections above the first dated entry — **known traps**, the
**tuning map**, **what has been superseded**, and **retired names** — are the working part, and
they are short on purpose. The dated entries below are an archive. Read the working sections
when starting a slice; grep the entries when you want to know *why* something is the shape it
is. Do not read it front to back.

**On the fact that it only grows.** Reviewed 2026-08-11 and kept deliberately. **The growth is
proportionate, not pathological** — a codebase accumulates decisions as it accumulates code, and
a file recording the ones worth keeping grows alongside it. A decision log that stopped growing
would mean the project had stopped being built. What has to stay bounded is the *working*
sections above, not the archive.

Compacting superseded entries was considered and rejected: all seven supersessions to date are
*partial* —
each kills one claim inside an entry whose other claims are still live — so stubbing them would
destroy current reasoning to save bytes nobody pays for. The archive is reached by search, not
by reading, so its length costs approximately nothing, and git already holds anything that did
get removed. Length is not the risk here; **a stale claim that does not announce itself is**,
which is what the table below exists for.

---

## What has been superseded

A new entry says what it supersedes. The old entry does not say it has been gutted — so reading
one directly can hand you a dead number with full confidence. Check here before trusting any
dated entry. Add a row whenever an entry supersedes part of an older one.

| This entry | …has a claim that is now wrong | Corrected by |
|---|---|---|
| 2026-08-09 — Ability input is routed by gameplay tag | block and parry will share one button | 2026-08-10 — The four questions gating defense |
| 2026-08-09 — One ability with three branches | `GA_LightAttack` is kept on disk as a fallback | 2026-08-10 — The `GA_LightAttack` fallback is removed |
| 2026-08-10 — The four questions gating defense | only *block* can cancel an attack's startup | 2026-08-10 — Costs are paid, not required |
| 2026-08-10 — Costs are paid, not required | exhaustion lasts a flat 4 s (`ExhaustionSeconds`) | 2026-08-10 — Exhaustion ends at full |
| 2026-08-10 — Jumping is taxed in recovery | the jump's tail is half the defensive pause | 2026-08-10 — The defensive regen pause is 0.5 s |
| 2026-08-10 — Sword and shield, rolls for every evade | every evade is a roll | 2026-08-10 — The evade is a dash, not a roll |
| 2026-08-10 — No dodging in the air | once buffering exists the refusal should become a defer | 2026-08-11 — The input buffer remembers a press |
| 2026-08-10 — Facing is camera-relative | the stock ABP plays a forward run while strafing | corrected **inline** in that same entry — nothing was decided on it, so it was a factual error rather than a reversal |
| 2026-08-11 — PvP is the destination | `CLAUDE.md` still lists netcode as out of scope "and that stands" | corrected **inline** in that same entry, within the hour — the user withdrew the scope call once it was restated back to them |
| 2026-08-11 — PvP is the destination | 14 network-unaware `SetTimer` sites | corrected **inline** in that same entry — the real figure is **2**; the count swept in Epic template code, debug timers, and one that must stay local |

---

## Known traps, indexed by what sets them off

Latent defects and unverified assumptions in code that **already exists**, each filed against
the slice that makes it bite. Re-read this when starting that slice, not at session start — a
flat list read once is forgotten by the time it matters.

These are not design questions. Nothing here needs play to settle; they need checking.

**Discharge a trap in the same commit that fixes it.** This section is the most load-bearing part
of the file and the only one with no natural expiry: a trap fixed by someone not reading this file
stays here and misdirects the next person, which is worse than never having filed it. Say what
discharged it and keep anything from it that is still true — the Slice B entry became a note about
the client path being unexercised rather than simply disappearing. Removing a trap silently is the
one edit here that cannot be reviewed, because nothing is left to review.

**Mid-item-6 — *reach has changed and the placed spacing has not.*** The trace moved from
`hand_r` to a 100 cm blade on the `Sword` socket, so the hitbox is somewhere else. **Both
characters still damage and kill each other** — verified in play 2026-08-11 — but a target
standing where it used to be struck can now sit outside the swing. Re-judge `TraceRadius`
(45 / 55 / 65, tuned against a fist standing in for the whole hitbox) and the training dummy's
placed distance during the content pass. `bDrawDebugTrace` draws the blade in yellow; nothing
else reports whether the length and axis are right.

*This replaces a trap filed hours earlier claiming the trace connected with nothing. It was
wrong — see the diagnostic note below, which is the part worth keeping.*

**Before block (item 7)** — *exhaustion can become permanent.* `ActivationBlockedTags` gates
activation, not continuation, so a block held through zero keeps draining and keeps
`State.StaminaRegenPaused` applied. Regen is now the **only** thing that ends exhaustion, so
stalling it stalls the exit condition forever. Related: the stamina delegate only fires on a
*change*, so a cost applied at exactly 0 changes nothing and cannot retrigger exhaustion.
Unreachable today — every defensive action is locked out until full — and block is what makes
it reachable.

**Before the first real multiplayer test — the ASC's client path is written and unexercised.**
Slice B shipped 2026-08-11 and its three filed traps are discharged: the PlayerState's 1 Hz net
update frequency is raised in `ATDPlayerState`'s constructor, the character re-resolves its ASC
in `OnRep_PlayerState` as well as `PossessedBy`, and `ATheDreamGameMode` now sets
`PlayerStateClass`. What is *verified* is the server path only — single-player PIE has no client,
so `OnRep_PlayerState` never fired in any test that passed. It is the half that is hardest to
reason about and the only half nothing has run.

A fourth trap was **not** filed in advance and is the one worth carrying forward as a pattern
rather than a fact: *the ordering hazard was invisible from the design and obvious from the
engine's callback order.* Seeding was guarded by one bool on the character, and a player pawn's
`BeginPlay` runs **before** it is possessed — so the flag would have been spent on the fallback
ASC and the PlayerState's real one never seeded. The player would have had no attributes and no
abilities **while the never-possessed training dummy worked perfectly**, which is what would have
made it hard to find. The guard now lives with the ASC (`ATDPlayerState::HasSeededDefaults`).
The general form: **when one code path serves two lifecycles, the one that works is not evidence
about the one that does not** — and here the working one was the simpler, so it would have been
tested first.

**Whenever new state is added, from 2026-08-11 on** — *a loose gameplay tag does not replicate,
and this is now a PvP project.* `AddLooseGameplayTag` is local to the machine that calls it. If
the caller is authority-only — anything driven by an attribute delegate, which are all bound
behind the authority gate — clients never see the state at all, and their own
`CanActivateAbility` will happily pass a check the server has already failed. Use a replicated
property whose `OnRep` applies the tag locally, following `bDead` / `bExhausted`. The rule is
**decide on the server, apply everywhere.**

**Before any multiplayer slice** — *i-frames have no lag compensation, and the dodge is 400 ms of
invulnerability.* The immunity check reads the target's ASC on the server. A client dodges at T
and the server learns at T+RTT/2; an attack resolving inside that gap ignores a dodge the player
has already watched begin. This is the "I dodged that!" complaint and this design can least
afford it — i-frames last exactly as long as the dodge, with no vulnerable tail to absorb the
disagreement. Also unsolved: no prediction windows despite every ability being `LocalPredicted`,
and **2** network-unaware `SetTimer` sites — `TDChargedAttackAbility`'s checkpoint and
`TDDodgeAbility`'s duration.

*Recounted 2026-08-11; the original figure of 14 came from a module-wide grep.* There are 13, and
**six are in `Variant_Combat/`** — Epic template code this project explicitly does not derive
from. Of our seven, four are debug-only (auto-attack press/release/reset, the debug revive) and
one is the buffered-release replay, which is *local input* and by this project's own rule
deliberately must **not** be networked. Note the correction does not shrink the work: the dodge
timer is the i-frame problem in this same paragraph, which is the hardest item on the list. **A
count taken across a whole module measures the module, not the debt** — the same filtered-view
error the absence rule exists for, in its counting form.

**Before the sword-and-shield attack swap (item 6)** — *the melee trace follows `hand_r`, not the
weapon, and reach is therefore unrelated to the sword.* `UTDMeleeAttackAbility::TraceSocket`
defaults to `hand_r` and sweeps a sphere of the branch's `TraceRadius` (45 / 55 / 65) along that
socket's path. Legacy from unarmed prototyping, and **already wrong** — the character has held a
sword since item 3b, and its blade contributes nothing to what it hits. Confirmed with the user
2026-08-11: item 6 moves the trace onto the weapon.

Two things that will move with it. Reach grows, so the `TraceRadius` values were tuned against a
fist and will need re-judging against a blade.

**The shape is settled: a blade is a line.** Decided by the user 2026-08-11, so item 6 sweeps
blade-base to blade-tip rather than sweeping a sphere along one socket. Recorded here because the
trap previously posed it as an open question, and the cheap moment to take it is while the trace
is being moved anyway — afterwards it is a second pass over the same code. The `TraceRadius`
values (45 / 55 / 65) become the blade's *thickness* rather than the whole hitbox, which is a
second reason they need re-judging and not merely rescaling.

**Before a second `Release Window` notify exists (item 6, or item 9)** — *the melee trace opens
on any `Event.Melee.WindowBegin` reaching that ASC.* `UAbilityTask_MeleeTrace` subscribes by tag
and never checks which montage sent the event. Correct while exactly one montage carries the
notify; silently wrong the moment a second does. Add the montage check **before** authoring the
second notify, not after.

**Before recovery and punish windows (item 12)** — *recovery is shorter than it looks, by exactly
the montage's blend-out.* `UTDMeleeAttackAbility::StartAttackMontage` binds `HandleMontageFinished`
to both `OnCompleted` and `OnBlendOut`, and `OnBlendOut` fires when blending *starts* — so the
ability ends and `State.Attacking` / `State.Attacking.Committed` come off `BlendOut.blendTime`
before the swing visually finishes. That is **0.25 s** on `AM_LightAttack_01`, large beside a
250 ms light. Mechanical and visible recovery differ, and recovery *is* the punish window. Settle
it deliberately: either recovery ends at blend-out and the animation is authored to agree, or the
ability waits for `OnCompleted` and blend-out becomes dead time. **Do not discover this by tuning
around it** — it is also the true cause of the debug attacker resetting before its swing looked
done, which was patched with a delay rather than diagnosed.

**When item 12 authors a real recovery** — *re-check `InputBufferSeconds`, which was sized
against a recovery nobody chose.* The window was tuned to its current value by play on
2026-08-11 and works well, but what it is bridging is an attack's *ability* lifetime, and that
still ends at montage blend-out rather than at an authored recovery — the item 12 trap below.
Change what an attack's tail is and the window is measuring a different thing, without anything
announcing it. Its ceiling is set by the longest lockout the design refuses to shorten, which is
exhaustion; see the 200 ms entry.

Note this replaces the trap that stood here until 2026-08-11 — that every timing verdict was
confounded by inputs which never registered. That was item 8's whole justification and it is
discharged.

**Whenever `MaxWalkSpeed` changes** — *it is coupled to the blendspace's top row and nothing
enforces the link.* `BS_SwordShield_Locomotion` places its run samples at Speed 500 because
`MaxWalkSpeed` is 500. Change one and the character tops out partway up the blend, playing a
permanent half-walk at full speed, with no error anywhere. Change both together.

Note 500 was inherited from Epic's template and **still has never been measured** against what
the `Run` clips are authored for — 3c shipped without visible foot sliding, which makes it
acceptable rather than correct. The `_RM` variants encode the authored displacement, so this
stays measurable rather than a matter of taste. See the tuning map's foot-sliding row.

---

## Tuning map — a verdict comes back, which knob moves

**The right-hand column is the point.** Each row is a place where the obvious-looking fix is
the wrong one, usually because it would quietly make the animation the balance authority or
silently shrink a window under a later retune. Read the row before reaching for a number.

The *questions* — does the ladder feel right, is the dodge too safe — are the user's and are
kept in their own notes. What belongs here is only what to move once a verdict arrives.

| Feels wrong | Move this | **Not** this |
|---|---|---|
| Dodge reads fast-forwarded | `DodgeSeconds` | The clip. Trimming sections to drop the play rate makes the animator's midpoint the design, and does it before the baseline has been felt. |
| Dodge travels too far or short | `AnimRootMotionTranslationScale` on the montage task | The play rate. Rate changes *duration*, never *distance* — a faster dash covers the same ground in less time. |
| Dodge is too safe | A recovery window in **absolute** time, i-frames derived as `DodgeSeconds - RecoverySeconds` | A *fraction* of the dodge. What makes recovery punishable is how it compares to an attack's startup, and a fraction shrinks the punish window below usable whenever the dodge is retuned faster. |
| An attack is too reactable, or not enough | `CoilEndSeconds`, or moving where the coil starts | The windup length. Reactability is measured from the **tell**, not the press — a longer windup with the same coil changes nothing. |
| The snap-to-camera pop reads badly | `StationaryTurnRateDegrees`, or blending the snap over a frame or two | Reverting to always-smooth. That reintroduces stale facing on the first frame of input and sends dodges sideways. |
| Feet slide during locomotion | `MaxWalkSpeed`, set from the `_RM` clips' measured displacement | The animation's rate. 500 came from Epic's template and was never measured; derive the speed from the clip rather than scaling the clip to an unchosen number. |
| An action feels unresponsive at low stamina | Nothing — find what is gating it | Adding or restoring a cost gate. Costs are paid, never required; if an input silently does nothing, `CostGameplayEffectClass` or `CommitAbility` has crept back in. |
| Exhaustion feels too long or short | `StaminaRegenPerSecond`, since recovery *is* the duration | A duration knob. There isn't one — `ExhaustionSeconds` was deleted deliberately so no second number can disagree with the bar. |
| An input still feels dropped, with buffering on | `InputBufferSeconds` — but read the `BUFFER` trace first and find out whether it was stored, fired or expired | The attack's own timings. A press that expired unfired is a question about the window; moving `ReleaseAtSeconds` to compensate tunes the ladder around an input problem and hides it. |

Add a row whenever an entry below establishes that a fix belongs in one place rather than
another. That is the reusable part of an entry; the argument around it is not.

---

## Retired names

Dated entries are never rewritten, so they still name things the code has since dropped.
This table is the bridge; without it a reader greps for a name, finds nothing, and
concludes the log is wrong rather than merely old. Add a row whenever a name changes.

| Entries say | Code now |
|---|---|
| `CoilPlayRate` | Derived at runtime from distance and time remaining. The authored knob is `CoilEndSeconds`. |
| `CoilCeilingSeconds` | `CoilEndSeconds` |
| `CoilStartSeconds` | `Branches[0].HoldUntilSeconds` — the coil begins exactly where the light stops being available, so it is one number, not two. |
| `WindupSeconds`, `MinHoldSeconds`, `MaxHoldSeconds` | Per-branch `HoldUntilSeconds` (the input boundary) and `ReleaseAtSeconds` (when the hitbox goes live). |
| `LogTDCoil` | `LogTDCombatTiming`, behind the `TD.DebugCombatTiming` cvar. |

---

## 2026-08-11 — Item 6's clip candidates, and the play-rate figure nobody has measured

Measured 2026-08-10, moved here from `CLAUDE.md` on 2026-08-11 because it is working data and an
unverified assumption, not a rule. The assumption is the reason it earns an entry.

**The pack splits every attack into a short mid-chain strike and a long terminal one.**
`Attack3_Stage1_RM` **0.73 s** and `Attack7_Stage2_RM` **0.70 s** are strike-only and chain;
terminal stages (`Attack3_Stage2` 2.20 s, `Attack7_Stage3` 2.27 s) carry a 2 s+ recovery to idle.
The standalone `AttackN_RM` clips are those same strikes wrapped in idle at both ends and run
**1.50 s** (`Attack9`, the shortest) to **6.60 s** (`Attack8`).

**The ~0.7 s chain stages are the candidates.** Windup rate scales with where the impact frame
sits, so against our current 1.0 s montage with its impact 36% in, a 0.7 s clip lands near
**1.0×** — better than today's 1.44× — while `Attack9` implies ~2.2× and `Attack1` ~7.4×.

**Those rate figures assume the pack shares our clip's impact proportion, and that is not
measured.** Impact position is unreadable through the MCP toolset, which is the same gap that
forces `ReleaseStartSeconds` to duplicate the notify by hand in the first place. So the numbers
above are an *argument for previewing*, not a result — and the failure mode if the assumption is
wrong is not a bad rate, it is a `ReleaseStartSeconds` that has drifted from the notify it was
copied from, which makes an attack silently stop dealing damage. Confirm on a preview before
committing to a family.

**`Attack7` is favoured for a reason that is not about rate:** the pack ships native combo
families, and `Attack7` has three authored stages (`Attack3`/`4`/`6`/`10` have two). That is
exactly what item 9's 2–4 hit light string wants, and choosing a family without the string in
mind means placing the `Release Window` notifies twice.

> **Corrected the same day, by measuring both packs. Two errors above.**
>
> **1. The durations are V1's, and the entry never said so.** `Attack7_Stage2_RM` exists in all
> three `SwordShield` packs *and* in `Spear`; the clip name alone does not identify a pack.
> Confirmed by measurement: V1's is 0.700 s, V3's is **0.467 s**.
>
> **2. "Three authored stages" does not mean three usable strikes**, and this is the error that
> would have cost a re-do. Nearly every family in **both** packs is *long opener → one short
> strike → long terminal*: V1 `Attack7` is 1.667 / 0.700 / 2.267; V3 `Attack7` is 1.500 / 0.467 /
> 2.333; V3 `Attack6` is 1.500 / 0.433 / 2.200. One chainable strike per family, not three. Item
> 9's string must therefore be assembled from short stages **across** families, or accept uneven
> lengths. The single exception found is **V3 `Attack4`**, the only four-stage family, at
> 0.600 / 1.167 / **0.667** / 2.333 — two short stages inside one authored chain.
>
> **And a correction to the reasoning, not just the data: shorter is not better.** The target is
> a clip whose impact frame lands at 250 ms at a play rate near **1.0**, which at an assumed 36%
> impact position means **L ≈ 0.70 s**. A 0.433 s clip implies a rate of **0.62** — slow motion,
> which is as much an artifact as the 1.44× it would replace. This kills the tempting reading
> that V3's snappier clips are automatically the better source.
>
> **Both packs have 0.700 s candidates, so length does not discriminate between them** — V1
> `Attack4_Stage1` and `Attack7_Stage2`, V3 `Attack1_Stage2` and `Attack8_Stage2`. The pack
> choice is therefore a *look* question, settled by preview, not by this table.

**Measured 2026-08-11, both packs, every non-`React`/`Complete`/`Back` attack clip.** Short
chainable strikes only; full data in git history of this entry.

| V1 | s | V3 | s |
|---|---|---|---|
| `Attack10_Stage1` | 0.633 | `Attack6_Stage2` | 0.433 |
| `Attack4_Stage1` | **0.700** | `Attack7_Stage2` | 0.467 |
| `Attack7_Stage2` | **0.700** | `Attack4_Stage1` | 0.600 |
| `Attack3_Stage1` | 0.733 | `Attack4_Stage3` | 0.667 |
| `Attack6_Stage1` | 1.067 | `Attack1_Stage2`, `Attack8_Stage2` | **0.700** |

**V1's two-stage families open short** (0.633–0.733) where V3's mostly do not, so for item 6's
*single* light — all the current scope needs — V1 gives a usable strike with no long opener in
front of it. **V3's combo families are deeper** (`Attack4` has four stages; `Attack1`/`6`/`7`/`8`
have three, against V1's mostly two), which is what item 9 will want. That tension is real and is
not resolved here.

## 2026-08-11 — The training dummy gets the sword too, and the blade is authored rather than measured

Raised by the user while setting item 6's scope: should the dummy get the new attacks, or keep
punching with stock assets? Settled as **parity in what the player is tested against** — the
user's phrase was "the correct amount of parity", and the qualifier is the load-bearing part.

**Keeping the dummy unarmed as a stable control was the real alternative, and it is better than it
sounds.** A test fixture that changes whenever the player changes stops being a control: you
cannot ask "does blocking feel different than it did before item 6" if both sides moved. That is
a genuine methodological argument and it is being rejected rather than refuted.

What defeats it: **item 6 changes reach**, and reach is what spacing is measured against. Every
defensive verdict from here — block coverage, blockstun duration, the parry window, whiff-punish
distance — is measured against whatever the dummy throws. A dummy that keeps fist-reach is a
*stable* reference to the wrong number, and item 7's blockstun is explicitly "a duration based on
the attack blocked". The failure is silent: the tuning all looks principled, and is calibrated
against an opponent nobody will ever fight. A wrong control answers a question the prototype is
not asking, and PvP is the destination.

**Parity is deliberately partial.** The dummy gets *offense* parity because offense is what the
player is measured against. It does not get `GA_Dodge` and should not — nothing about a defensive
verdict depends on the dummy being able to evade, and a dodging dummy would make every spacing
measurement noisier for no gain. The rule is not "the dummy is a second player"; it is "the dummy
is accurate in the dimension being measured."

Worth noting the decision was nearly made already and neither of us had noticed: **both characters
are granted the same `GA_Attack`**, so montages, branch timings, `TraceSocket` and `TraceRadius`
are one shared set. Divergence was the option that cost work — a second ability asset with a
second set of timings to keep in sync. Checking that first turned a design debate into a much
smaller one, which is the general lesson: *establish whether the alternatives actually differ in
cost before arguing their merits.*

**The blade's length is authored, not derived from the weapon mesh.** This is the design decision
the dummy question forced out early, and it is the more valuable half of the entry. A
blade-base-to-tip sweep needs a length, and taking it from the attached `WeaponMesh` bounds is the
obvious implementation. It fails on exactly one case and fails silently: the `Sword` socket lives
on the *skeleton*, so it exists whether or not a prop hangs off it, and a character with no weapon
mesh would produce a well-formed trace of **zero length** — a hitbox that misses everything, with
nothing logged and nothing null. Authoring the length as a Blueprint-exposed number instead means
the trace is correct for anyone holding anything, including nobody.

Had the dummy stayed unarmed, a mesh-derived blade would have worked perfectly in every test —
because the player, the only armed character, would have been the only one traced.

The consequence for item 6 is small and should not be mistaken for the decision: the dummy needs
its `WeaponMesh` and `ShieldMesh` set, which is two properties on `BP_TrainingDummy`, not a class
change. If player and dummy become hard to tell apart, a material tint is the answer — it does
not touch mechanics.

## 2026-08-11 — The ASC moves to the PlayerState, and the character resolves which one it uses

**Decided, not yet built.** Recorded now because the alternatives were argued and discarded, and
the code will only contain the winner.

**The ASC moves to a new `ATDPlayerState`** for player characters — the conventional PvP shape,
and the one that survives if respawn ever means a fresh pawn rather than the revive it is today.
Keeping it on the character was defensible on current behaviour and was rejected on the grounds
that the destination is known.

**The training dummy has no PlayerState**, being an unpossessed placed pawn, so the character
resolves its ASC: the PlayerState's when there is one, an owned component when there is not.
One class, one `InitialiseAbilitySystem`, no Blueprint changes.

Two alternatives were rejected:

**Splitting into `ATDPlayerCharacter` / `ATDAICharacter`** was chosen first and then withdrawn.
It is the cleanest conceptually — each class honest about what it owns — but it means
**reparenting `BP_PlayerCharacter` and `BP_TrainingDummy`**, and a Blueprint stores its parent
class path. That is the operation this project's first implementation convention exists to warn
about, and it would be performed on the two Blueprints everything else depends on, to buy a
distinction a header comment already makes. What decided it: *the training dummy will only ever
be a training dummy.* There is no future AI opponent for the split to serve, so it would be
structure built for a case that is not coming.

**Giving the dummy an AI controller with `bWantsPlayerState`** would make the rule uniform with
no fallback branch, and was rejected for the same reason — a controller the dummy does not
otherwise need, to remove a branch that is two lines.

The accepted cost is a small one, stated plainly: a player character carries an owned ASC
subobject it never uses. It seeds no attributes and grants no abilities, so it costs registration
rather than bandwidth.

## 2026-08-11 — PvP is the destination, so state must replicate; the model is server-auth + prediction

Raised by the user, who noted this prototype is ultimately PvP and that netcode should be treated
as an inevitability rather than a later phase. An audit went looking for how large the gap
already was.

> **Corrected within the hour, by the user.** This entry originally said `CLAUDE.md` still listed
> netcode as out of scope "and that stands". It does not stand — the user withdrew the scope call
> as soon as it was restated back to them: *"A PvP prototype doesn't do all that much, ultimately,
> if it's incapable of PvP."* `CLAUDE.md` now carries **Building for the network** instead, and
> only *live multiplayer sessions* remain out of scope. Corrected inline rather than superseded
> because the reasoning below never depended on the deferral — it argued the opposite — and a
> stale claim about what another document says is exactly the kind that gets read as current.
>
> Worth keeping as an instance of a pattern this log already knows: **a claim moved between
> documents arrives with the authority of its new home and none of the scrutiny it never had.**
> The same thing happened on 2026-08-10 with the stock-ABP claim. Restating an inherited
> assumption out loud is the cheapest moment to test it, and both times that is what killed it.

**The verdict: caught early, and the expensive-to-retrofit decisions had already gone the right
way.** Attributes replicate correctly (`Mixed` mode, `REPNOTIFY_Always`, three-layer clamping),
damage and cost application are authority-gated, and **displacement is root motion rather than
code-driven** — that last one was chosen on 2026-08-10 for a completely unrelated reason ("the
cheaper experiment runs first") and is the single most valuable accident in the codebase, because
CMC replicates root motion and a hand-rolled displacement system would have had to be rewritten.

What was wrong was wrong *mechanically* rather than structurally: ten call sites of one mistake,
one unset property, one missing guard.

**The latency model is server-authoritative with client prediction.** Chosen deliberately over
rollback. Rollback is arguably the better fit for the design — a 250 ms light and frame-precise
punishes are fighting-game shapes — but GAS is not built for it, and choosing it would mean
fighting the framework or moving combat out of it entirely. This is the model GAS is designed
around, and the project has shipped PvP on it before.

The consequence to hold onto: **latency is now a design constraint, not an implementation
detail.** Reactability is measured from the tell, and the heavy's coil→damaging window is already
"right at the edge of human reaction" at ~240 ms. Whatever a network adds comes out of that
budget. Items 6–12 should be designed knowing it.

**Loose gameplay tags do not replicate, and every state tag in the project was one.** For tags
applied by an ability this survived by accident — the ability runs on both machines under
`LocalPredicted`, so both ASCs get the tag independently. For tags applied by the *character* it
was simply broken: `State.Dead` and `State.Exhausted` are driven by attribute delegates bound
behind the authority gate, so a client's ASC never received them, its own `CanActivateAbility`
would pass, and it would predict actions the server had already refused.

Fixed by making `bDead` / `bExhausted` replicated properties whose `OnRep` applies the same tag
locally that the server applied on itself. **A GameplayEffect carrying the tag is the more usual
answer and was rejected on tooling grounds**: UE 5.8 expresses effect-granted tags through
`gEComponents`, which cannot be scripted, so it would need a human in the details panel for a
mechanism the economy already owns in C++.

The shape this establishes, and the reason it is worth an entry: **decide on the server, apply
everywhere.** `EnterDeath`/`ExitExhaustion` decide whether the state changed and are
authority-only; `ApplyDeathState`/`ClearExhaustionState` make it true on the local machine and run
on the server directly and on clients from `OnRep`. Two things stayed deliberately on the server
side of that line — `CancelAllAbilities`, because it replicates through GAS by itself and running
it again from a client would cancel that client's predicted copies, and attribute restoration,
because a client rewriting its own health is the shape of an exploit.

**`NetExecutionPolicy` was `LocalPredicted` on every ability by accident.** `UGameplayAbility`'s
constructor never assigns it and `LocalPredicted` is enum index 0, so the most demanding policy
was in force without anyone choosing it. It is now set explicitly — to the same value, which is
correct for the agreed model. What is still owed is prediction windows, so a mispredicted
activation is rolled back rather than left standing. Declaring the intent while the
implementation is incomplete is deliberate and better than leaving it undeclared.

**The melee trace is now authority-guarded.** It ran on client and server both, from socket
positions a round trip apart. Damage was already server-gated so this was wasted work rather than
double damage, but it was also a second opinion about what was hit that nothing reconciled.
Prediction does not change this: a client predicts its own action, never whether that action
connected with someone else.

**Standing rule from here: new state is a replicated property or an attribute, never a loose tag;
new authority-sensitive work is explicitly gated.** That is what stops the gap growing while the
actual networking waits.

Known to remain, and deliberately deferred: prediction windows and keys, lag compensation for
i-frames and hit resolution (the dodge is 400 ms of invulnerability whose start instant two
machines will disagree about), client-side stamina prediction, and 14 `SetTimer` sites that are
not network-aware.

> **The figure of 14 is wrong, recounted 2026-08-11.** It is **2** — `TDChargedAttackAbility`'s
> checkpoint and `TDDodgeAbility`'s duration. The original came from a module-wide grep that
> counted six sites in `Variant_Combat/` (Epic template code we do not derive from), four
> debug-only timers, and the buffered-release replay, which is local input and correctly stays
> local. Corrected **inline** rather than superseded because nothing was decided on it — it was a
> miscount, not a position that changed, the same treatment the stock-ABP claim got on
> 2026-08-10. See the known-traps section for the full breakdown. **The correction makes the list
> shorter, not the work smaller:** the dodge timer is the i-frame lag-compensation problem named
> two sentences earlier, and that is the hardest item here.

## 2026-08-11 — Dodge travel ships at the clips' authored distance, measured rather than assumed

Item 5. `DodgeRootMotionScale` is now wired through to the montage task, and stays at **1.0** —
the value play already had. The slice's product is the knob and the measurement, not a change.

**The eight directions were suspected of disagreeing and do not.** `AM_Dodge` is built from eight
separate `Dash` clips, and nothing had ever checked they travel the same distance; if they had
disagreed, a single uniform scale would multiply the gap rather than close it, and the fix would
have been per-direction data. Measured over one dodge each on flat ground:

| Direction | BR | FR | FL | R | Fw | BL | Bw | L |
|---|---|---|---|---|---|---|---|---|
| Travelled (uu) | 418.5 | 417.3 | 408.8 | 403.6 | 403.4 | 402.2 | 398.6 | 387.0 |

Mean **404.9 uu**, total spread 31.5 uu — under one capsule radius, and below what a player can
perceive as directional bias. A single scale is the right shape. Durations were 0.400–0.408 s
against `DodgeSeconds` 0.400, matching the project's usual within-a-frame-biased-late pattern.

**Measured at the actor, deliberately, not read off the clip.** What the player feels is where
the character ended up, which collision, slopes and the movement component all get a say in. The
authored displacement would describe the animator's intent instead. The trace reports the actual
delta, horizontal only — vertical travel is gravity, and counting it would make every dodge down
a slope read as longer.

**The number worth carrying forward: a dodge covers roughly three times an attack's static
reach**, and averages about twice `MaxWalkSpeed`. So it does not merely evade, it disengages, and
punishing afterwards costs the time to close again. That is not a complaint — play reports the
dodge feels good — but it means the dodge has *two* safety margins, exposure and distance, where
the tuning map's "dodge is too safe" row only names the first. Whichever gets moved, the other is
still there.

Reach is estimated rather than measured: the trace follows `hand_r`, and where that socket sits at
the impact frame is unknown. If spacing becomes contentious, that is the missing number.

## 2026-08-11 — Death cancels what exhaustion lets finish, and inert is not the same as dead

Item 4, the minimum death: health reaches zero, `State.Dead` goes on, every ability is refused,
the character stops. Respawn rules, whether death routes through knockdown, and whether the dummy
should die at all remain item 11's.

**Death cancels running abilities; exhaustion deliberately does not.** This is the one place the
pattern being copied was wrong to copy. Exhaustion gates *activation* and lets a running ability
play out, which is right for a stamina lockout — and is the reason a block held through zero is a
filed trap. For death it is visibly wrong: a killing blow landing mid-swing would leave the corpse
completing its attack, hitbox included. `CancelAllAbilities` also clears `State.Attacking` and
`State.Attacking.Committed` through the normal end path, so death cannot leak the tags that would
forbid every future defensive action after a revive.

**The refusal lives on the shared ability base, not in each ability's `ActivationBlockedTags`.**
An ability can be authored without a tag; it cannot be authored without its base class. The dead
are not a special case any one ability should have to remember.

**`State.Dead` is native, not an `EditDefaultsOnly` property like `ExhaustedTag`.** A placed actor
can serialize stale `EditDefaultsOnly` values that silently override its Blueprint, and the
training dummy shipped for days with `ExhaustedTag` unset for exactly that reason — unreachable
from the details panel, so nothing showed it. A native tag has no per-instance value to go stale.

**A ragdoll, because "inert" and "dead" look identical.** Play's first report was that death was
unreadable: the character stopped and nothing announced it. The ragdoll needs no animation
content, which matters because the directional `Death_<DIR>` clips belong to item 11 — and it is
behind `bRagdollOnDeath` so that slice can switch it off rather than unpick it. The mesh's rest
offset is captured in `BeginPlay`, before physics can touch it: restoring from a value read after
death would bake the ragdoll's final pose in as the new rest offset and the character would stand
up crooked permanently.

**Disabling movement does not disable facing, and that cost a bug.** The dead character still
turned to track the camera, because `UpdateCameraRelativeFacing` is a separate system that knew
nothing about death. Worth stating generally: *stopping a character* is not one switch, and the
list of what "stopped" means is not discoverable from the thing you just turned off.

The user proposed **depossessing the pawn** instead, which is the stronger version of this
argument and was rejected only on scope: it would kill the whole class of problem at once, but
pulls in where the controller goes meanwhile and what the camera does, both of which are item 11's
questions. Revisit if death ever needs its own camera. Recorded because the reasoning is better
than the alternative it lost to, and someone should propose it again.

**Two defects found by review rather than by play**, both silent:

- *Dying airborne stranded `bJumpRegenPauseActive` set forever.* `DisableMovement` stops the fall,
  so `Landed()` never fires to clear it, and stamina regen stays suppressed for the rest of that
  character's life — past the revive, with nothing to indicate it.
- *`ReturnToDebugAutoAttackHome` guarded on the wrong flag.* It checked
  `bDebugAutoAttackResetPosition` (default true) but not `bDebugAutoAttack`, while the home
  transform is only ever captured for auto-attackers. Unreachable until the revive started calling
  it — at which point the *player* would have teleported to the world origin on every death.

**And one found by play that was a wrong comment rather than wrong code.** The reset is correctly
suppressed while dead, so a corpse is not teleported mid-ragdoll; the comment then claimed "the
revive restores the transform anyway", which was false — the revive restores the *mesh's* offset
from the capsule, never the *actor's* world transform. So a dummy killed mid-lunge revived
displaced and only drifted home on its next attack cycle. The behaviour was changed to match the
comment rather than the comment softened to match the behaviour, because the comment described
what the system should have done.

## 2026-08-11 — The buffer window doubles to 200 ms, and what the tuning sessions ruled out

`InputBufferSeconds` 100 → 200 ms, settled by play the same day it was built. Measured, across
three sessions on the same build:

| Window | Presses buffered | Expired unfired | Fired |
|---|---|---|---|
| 100 ms | 40 | **17 (43%)** | 23 |
| 200 ms | 35 | 2 (5.7%) | 33 |
| 200 ms, ladder test | 70 | **2 (2.9%)** | 68 (97%) |

**Going higher was rejected, and exhaustion is the reason it stays rejected.** The user's read
was that a wider window buys only false positives; the log gives that a mechanism. Exhaustion
lasts until stamina refills — about four seconds — and dodge presses made while exhausted
correctly expire rather than firing. A window wide enough to bridge that would hand back a dodge
seconds after it was asked for. So the ceiling is not "how long until an input feels stale" in
the abstract; it is set by the longest lockout the design deliberately refuses to shorten.

**A verdict that reads as a dodge failure and is not.** Zero of seven buffered dodges fired in
one session, which looked alarming and was not: all seven were pressed either during a charged's
commitment or while exhausted, and 14 further dodge presses that session activated immediately.
Both blocking states are visible to the player — a swing in progress, an empty greyed bar — so
none of these was an input silently doing nothing. **The lesson is the denominator**: judging a
buffer by the subset that needed buffering measures the hardest cases only, and reports a healthy
system as broken.

**Holding an input to have it fire on the first legal frame is accepted as a technique.** It
falls out of the no-expiry-while-held rule rather than being designed, and it was left in
deliberately after being checked for the failure that would have killed it: it does not loop.
Buffering happens only on a press edge (`ETriggerEvent::Started`), and the slot is cleared when
it fires, so a held button yields exactly one action. Confirmed in play — the tightest interval
between consecutive dodges was 834 ms against a 0.4 s dodge; a loop would show a run of ~400 ms
repeats until stamina ran out.

**Accepted, with eyes open: a buffered attack's tier depends on how long the buffer waited, and
the player cannot see that.** The hold is measured from activation, so the wait is subtracted
from the front. Measured in one session: a **452 ms** physical hold produced a **Heavy** while a
**484 ms** hold produced a **Light**, because the buffer had sat for 0 ms and 282 ms
respectively. This is not a defect — it is "windup length is preset" doing its job, and
crediting the pre-activation hold is the fix that was rejected when the buffer was built. It is
recorded because someone will propose that fix again on seeing exactly this. What makes it
liveable is that the attack is *visually* cued: hold until the coil appears and the latency
stops mattering. The user's verdict after extended play was that the misses "felt like a skill I
could grind improving, rather than the game's inputs betraying me", which is the distinction
that matters and the one only play can supply.

**A note on what "verified" required, because the bar was briefly set wrong.** The
tier-preserving replay was called untested for two sessions on the grounds that no logged case
showed a hold past 200 ms producing a heavy. That standard was wrong: there is no branch on
200 ms. The path schedules the replay at `HoldSeconds` whatever its value, it executed 24 times
correctly, and what follows the replayed release is the ordinary escalation logic that 39 heavy
and 38 charged commits already exercise. **Treating a parameter value as a code branch produced
a test that could only be passed by hand-timing a float**, and cost two play sessions chasing it.
Ask what the code actually branches on before asking play to cover a case.

## 2026-08-11 — The input buffer remembers a press, and one rejection that play reversed

A press nothing could answer is kept for `InputBufferSeconds` (100 ms) and retried each frame.
Item 8, verified in play. The code says what it does; below is what was rejected, and the one
rejection that turned out to be wrong.

**Crediting the hold already elapsed was rejected outright.** It is the tempting way to make a
buffered charge arrive "on time", and it breaks the rule that windup length is preset — the
attack would land sooner than its tier is authored to take. The hold is measured from
activation, and time spent pressing before that is spent.

**Replaying the release as a *duration* was built, dropped, then reinstated by play. That
sequence is the entry.** The simplification was to record the release as a yes/no and replay it
at once, justified like this: the buffer outlives a release by only `InputBufferSeconds`, so
every recordable hold must be under 100 ms — comfortably inside the light's 200 ms boundary —
and therefore replaying the true offset and releasing immediately select the same branch, so the
offset machinery buys nothing.

The premise is false. **The buffer survives indefinitely while the button is held**, so a
recorded hold is bounded by how long the *block* lasted, not by the window. Release while still
locked out and any hold length can be recorded. Play produced a **236 ms hold — a heavy by every
rule the ladder has — flattened to a light**, visible only because the trace prints the duration
rather than a boolean.

Two things worth carrying out of that:

- The instruction it overrode — record both edges, *because you need to be able to buffer any
  attack type* — was the user's, and the argument that talked us out of it was mine. **A
  simplification justified by an invariant is worth exactly as much as the invariant**, and this
  one was never checked against a rule written into the same struct three edits earlier.
- **Where a value drives behaviour, print the value.** A boolean trace would have shown this
  system working perfectly. That is the second time this project has learned it; the first was
  `want=0.500s` on eleven consecutive dodges.

**One slot, not a queue.** A queue replays stale intent as a burst — press dodge then attack into
a lockout and both arrive, in an order you had already stopped asking for.

**Polled, not event-driven.** Waking on "the reason this was refused" means enumerating every
blocking tag, every ability end and the airborne check; missing one fails silently. Polling
cannot be incomplete, and there is at most one entry to retry.

**The window is grace on taps, not on intent.** A held button never expires, so the 100 ms
measures from the release. Without that a heavy could not be buffered at all — its 200 ms
boundary is beyond any window this size, so every tier above light necessarily comes from a hold
that outlives the window.

**The airborne dodge refusal is deliberately not buffered**, superseding the prediction in
"2026-08-10 — No dodging in the air" that buffering would turn it into a defer. Landing is not a
moment you are free to act through: a landing recovery would lock the dodge out anyway, so
deferring to the touchdown frame hands back a dodge a finished character will later have to take
away, and tunes the timing of a window that does not exist yet. `ShouldBufferFailedInput` is
where an ability declares that its refusal was not the temporary kind.

**A held buffer does not expire, but it does not wait either** — and confusing the two cost two
play sessions. I proposed testing the supersede path by holding dodge through a committed attack
and then pressing attack, expecting the held buffer still to be there. It was not: holding
prevents *expiry*, not *firing*, and the buffer fires at the first opportunity. Measured, the
dodge escaped in 202, 227 and 335 ms across three attempts. The general form is more useful than
the bug — **a superseding press only matters when something blocks an ability for longer than a
person can react**, and today exhaustion is the only such thing in the game. Items 7 and 11
(block, blockstun, knockdown) make long lockouts routine, at which point this stops being exotic.

**Feel is deliberately untuned.** 43% of buffered presses expired unfired in the first session.
Whether the window should be wider is open and is the user's call; see the trap above about
sizing it against a recovery length item 12 has not settled.

## 2026-08-11 — The character wears the bundle's mesh, and `ABP_Combat` is Epic's ABP with two nodes swapped

Three choices from items 3b and 3c that a future reader would reasonably question.

**The character mesh is `GDHBundle`'s `SKM_Manny`, not Epic's `SKM_Manny_Simple`.** Not a
preference — the pack's `Sword` and `Shield` sockets exist only on its own mesh, and those
sockets carry the grip rotation and the non-uniform scale that corrects an oversized shield.
`_Simple` also lacks the whole twist/corrective/weapon bone family. The cost is that the mesh
now sits on the bundle's skeleton while `AM_LightAttack_01` is still bound to Epic's, which
works **only** because GDH's `SK_Mannequin` was given a reverse `CompatibleSkeletons` entry
pointing back at Epic's. That entry is load-bearing: remove it and the light attack silently
stops playing. The original migration added the forward entry; this session added the return.

**`ABP_Combat` is a duplicate of Epic's `ABP_Manny_Combat` with two nodes repointed**, rather
than an AnimBlueprint authored from scratch. Duplicating buys a working state machine, jump and
fall states, and a foot-IK control rig for free, and putting the copy under `/Game/TheDream/`
satisfies the ownership rule. Authoring one fresh would have cost a day to arrive somewhere
worse. The accepted consequence is that we now maintain Epic's structure without having chosen
it, and that its jump, fall and land states still play Epic's generic `MM_` clips — visible in
play, deliberately out of item 3c's scope.

**The blendspace has a walk row even though WASD cannot ask for a walk.** Raised by the user,
who expected it to be dead content. It is not: the Speed axis is fed by actual movement speed,
not by an input mode, and the character ramps through walk speeds on every start and stop. With
no walk row that ramp blends idle straight into a full run and reads floaty. The row that would
genuinely be dead content is a *sprint* row, since nothing can request one.

**Known and not fixed: adjacent directions disagree about the guard pose.** `BR` holds the
shield front-left while backpedalling, `R` holds it front-centre, so it snaps ~135° blending
between them. That is in the source clips, not in our setup. The standard fix is an upper-body
layered blend over one consistent guard pose, above `spine_01` or `spine_03` — which is very
likely what block needs anyway, so building it now would mean building it twice or building it
before knowing what item 7 wants from it. Deferred deliberately, not overlooked.

## 2026-08-10 — Facing is camera-relative always, and snaps on *input* rather than on movement

Four decisions taken together, before any of item 3 was built. Moved here from `CLAUDE.md`,
which was carrying the argument as well as the ruling.

**Why facing, locomotion and the props are one item.** `ResolveDodgeDirection` measures input
against actor facing, and `bOrientRotationToMovement` meant the actor already faced its input —
so the angle was always ~0 and it resolved to `Fw` in steady state, `Bw` only via the stationary
fallback. Across every dodge logged 2026-08-10, no other direction ever fired. **The function
needs no code change; it was correct and merely degenerate.** Camera-relative facing lights up
all eight as they are.

**Locomotion still ships with facing, but not for the reason `CLAUDE.md` gave.**

> **Corrected the same day it was written.** This entry originally claimed locomotion could not
> be deferred because "the stock ABP plays a forward run while moving sideways." **That is
> false** — 3a went into play and `ABP_Manny_Combat` produced correct strafe and backpedal
> animations immediately. The claim came from `CLAUDE.md`, was moved here unexamined, and was
> falsified within hours by the very slice it was justifying. Neither of us had checked it. It
> is corrected inline rather than superseded because nothing was *decided* on it — it was a
> factual error about existing content, not a position that changed.

The real reasons are ownership and stance, and both are weaker urgency than the false one:
`ABP_Manny_Combat` lives at `/Game/` root, is Epic template content authored for the
`Variant_Combat` hierarchy we deliberately do not derive from, and animates a generic Manny
rather than a sword-and-shield stance. So it gets replaced because it is not ours and not the
right character, not because it is broken.

**The transferable lesson is about inherited claims.** A statement moved between documents
arrives with the authority of the file it lands in and none of the scrutiny it never had.
Deduplicating is still right — the error was in exactly one place when it came time to fix it,
which is the whole argument for pointing rather than copying — but *moving* a claim is the
cheapest moment to test it, and this one would have cost one PIE session to check.

**Facing is camera-relative permanently, not by combat stance.** A stance toggle is the common
action-game answer and was rejected for now on state cost: dodge, block and parry would each
have to agree with it, and there is no traversal content that would benefit. Revisit if one
ever exists.

**But the character does not snap to the camera at all times.** While there is no movement
input it turns smoothly toward camera yaw; the moment there is any movement input it snaps.
The reason snap is wrong while stationary is that a static idle has nothing to mask an
instant rotation pop — and the standard fix is unavailable to us:

> **The bundle contains no turn-in-place animation whatsoever.** Zero matches for `Turn`,
> `Pivot`, `Rotate` or `Spin` across all 6,576 rows of the unfiltered index, and no `Turn`
> token in the exhaustive vocabulary. `SwordShield` ships two idles per pack and nothing else.

So the hybrid is not a preference over the animated solution; it is the only solution we can
build without new content.

**The snap keys on input, never on velocity, and that is the load-bearing part.** Keyed on
*movement* the hybrid would break the dodge at exactly the moment dodges get pressed. Standing
still, camera swung north, character mid-turn still facing east, press forward+dodge on one
frame: input is north, facing is east, the signed angle is −90° and the dodge resolves to `L`.
You pressed forward and went left.

Keying on input closes it, because `ResolveDodgeDirection` already returns `Bw` unconditionally
on near-zero input and never consults facing while stationary. The two rules then key off the
**same** condition and cannot disagree: facing is allowed to lag only while nothing reads it,
and is exact the instant anything does.

This is the third application of a rule the codebase keeps arriving at independently — **key
off input or action, not off the state it produces.** `ResolveDodgeDirection` reads
`GetLastInputVector()` rather than velocity because velocity still carries the old direction
after the stick is released; the jump regen pause hooks `OnJumped` rather than `IsFalling()`
so a ledge-walk is not billed. Worth stating generally now that it has come up three times.

**Accepted cost, stated plainly: the pop is relocated, not removed.** Swing the camera 180°
while stationary and move before the turn completes, and the remainder snaps. It lands on the
idle→locomotion transition, which is changing the animation anyway, and at 500°/s a 180° turn
takes 0.36 s so it will usually have finished. The turn rate gets its own tunable rather than
borrowing `RotationRate`, because the two are now different things.

**Locomotion is run-only, eight directions, `Loopable` clips, from `SwordAndShieldAnimV1`.**
One 1D directional blendspace, eight samples, idle as a separate state. Walk is not authored
because there is no walk/run input to select it — a speed axis would be content for a control
that does not exist. Start/End transition clips are likewise deferred: they are polish on a
baseline nobody has felt, and this project has now twice been right to judge the baseline
first. V1 is chosen to match `AM_Dodge`, which is built from V1 `Dash` clips; mixing packs
risks two stances reading as two characters. If V3 previews better, `AM_Dodge` should move too
rather than the two diverging.

## 2026-08-10 — The defensive regen pause is 0.5 s; felt feel beat the argued distinction

`StaminaRegenPauseSeconds` drops 1.0 → 0.5, which makes it equal to `JumpRegenPauseSeconds`.

This supersedes the reasoning in "Jumping is taxed in recovery" below, which set the jump's tail
at half the defensive one *because* "a jump is a weaker commitment than a dodge and should not be
taxed as heavily." That argument is sound and it lost to play: with the montage finally attached
and the dodge legible, 0.5 s simply felt right and 1.0 s did not. The distinction was reasoned
into existence before anyone had felt either number.

**The two values stay separate even though they are now identical.** Not an oversight — the
separation is capacity, not an active claim. Collapsing them into one number would make
re-splitting them a code change rather than a tuning pass, and there is no cost to holding two
fields that happen to agree.

The transferable rule, now also in `CLAUDE.md`: **when play and rationale disagree, play wins.**
This log exists to make choices legible, not to defend them against evidence. A carefully argued
entry is not a commitment — it is a record of what was known at the time, and superseding one is
the system working rather than failing. That matters here specifically because this log is long
and persuasive, and a persuasive argument is exactly the kind of thing that can outlive its
usefulness quietly.

## 2026-08-10 — The evade is a dash, not a roll; the roll choice came from an incomplete search

`AM_Dodge` is built from the eight `AS_SwordAndShieldAnimV1_Dash_*_RM` clips. This supersedes
"Sword and shield, rolls for every evade" below.

**The original entry was wrong by omission, not by reasoning.** It argued that the library has no
forward `Dodge` in any archetype while `Roll` covers all eight, so committing to rolls avoided
mixing two move types under one button. Every word of that is true. It simply never checked
`Dash`, which also covers all eight directions in the same pack, and was sitting migrated in the
project the whole time. The user found it. The constraint that supposedly forced rolls never
actually discriminated between the candidates.

The full set, all eight-directional and all already migrated:

| Clips | Length | Play rate at `DodgeSeconds` 0.4 |
|---|---|---|
| `SwordAndShieldAnimV1_Dash` | 0.733 s | **1.83×** |
| `SwordSwordAnimV3_Dash` | 0.833 s | 1.92× |
| `SwordAndShieldAnimV1_Roll` | 0.900 s | 2.25× |
| `SwordSwordAnimV3_Flip` | 0.967 s | 2.42× |

Length is the smaller argument. The larger one is that **the move type should match the duration
that play actually settled on.** 0.4 s was reached by feel, twice, before any animation existed —
and a full body roll compressed into 0.4 s is a complete rotation in under half a second, which
would read as frantic at any multiplier. A dash is natively a quick reactive evade, so at 0.4 s it
is being asked to do roughly what it already does.

The counter-argument is real and is being set aside rather than refuted: the earlier entry held
that a roll's commitment suits a move costing half the stamina bar, and that rolls are the genre's
readable idiom for i-frames. If the dash proves too weightless for a 50-stamina cost, that is a
finding, and the rolls are one montage away.

Root motion was enabled on all eight dashes, since like every `_RM` clip in the bundle they ship
with it off. Verified on disk rather than by read-back.

**The transferable lesson is about when this was catchable.** It was raised at the last cheap
moment — `AM_Dodge` had exactly one segment in it. The same question a day later costs rebuilding
eight sections. A decision recorded with confident reasoning is not the same as a decision whose
alternatives were actually enumerated, and this log cannot tell those apart on its own.

## 2026-08-10 — Animations play whole; visual consistency beats hypothetical feel

`AM_Dodge` is built from **entire, untrimmed roll clips**, so the animation and `DodgeSeconds`
line up exactly. At 0.4 s against 0.9 s clips that means a 2.25× play rate, and 2.25× is accepted
rather than engineered away.

This supersedes advice I gave twice earlier the same day, that the sections should be trimmed to
their "usable evasive portion" to keep the multiplier down. That was wrong in a specific and
instructive way: it proposed cutting the animation to hit a play rate nobody had looked at yet,
on a guess about how 2.25× would read. Trimming a clip to a hand-picked endpoint is not neutral —
it silently makes the *animator's* midpoint the design, and it does so before anyone has seen the
baseline it is supposedly fixing.

**The rule, stated generally: an animation plays in full across the mechanical duration it
belongs to.** It is fitted to that duration, never cut down to hit a number that has not been
felt. If the result feels wrong, change the duration — that is a single authored number with a
derived rate behind it — rather than hiding the problem inside the asset.

This does not weaken "the animation is not the balance authority", which still holds and is why
`DodgeSeconds` is authored and the rate derived. The two are complements: **mechanics decide how
long, the animation gets all of that time.** What is forbidden is the third option, where a clip
is quietly reshaped so a number stops being questioned.

The practical consequence: judge the baseline first, then deviate only if play demands it. Travel
distance is deliberately left alone for the same reason — it is unjudgeable until there is
something on screen to judge.

## 2026-08-10 — The dodge is 0.4 s, judged before it had an animation

`DodgeSeconds` went 0.5 → 0.3 → 0.4 in one sitting, settling at 0.4. 0.5 was a guess made before
any clip existed; 0.3 overshot.

**What makes this verdict unusually trustworthy is when it was taken.** There is still no
`AM_Dodge`, so the judgement was made against the pure mechanical duration — the lockout and the
i-frame window — with no animation to blame or to hide behind. Once a montage exists that signal
is gone for good, because a dodge that feels wrong can then always be read as the animation's
fault. Retuning before authoring was deliberate for exactly this reason, and it is worth
repeating for block and parry: **judge the timing naked, then build the animation to fit it.**

The order also protects the model. The play rate derives from `DodgeSeconds`, so had the montage
been authored first, the tempting fix for "this feels too long" would have been to speed the clip
up and leave the duration alone — which is the animation quietly becoming the balance authority,
the thing this project has now rejected three times.

The consequence to carry into authoring: 0.4 s against 0.9 s clips is a **2.25× play rate**. That
is inside the range the earlier entry warned about, and the lever if it reads fast-forwarded is
trimming the sections, not raising the duration back up. See the open question above.

## 2026-08-10 — No dodging in the air, and why it keys off state where the jump keys off action

Found in play: you could dodge while airborne. Not intended, so `GA_Dodge` now refuses to
activate while falling.

**This keys on the airborne *state*, not on having jumped** — so it also stops a dodge after
walking off a ledge. That is deliberately the opposite of the jump's regen pause immediately
below, which keys on the *action* and lets a ledge-walk cost nothing. The two look inconsistent
side by side and are not, because they answer different questions. The regen pause is a **cost**,
so it should only attach to something you chose to do; falling because you walked off a ledge is
not a choice and should not be billed. The dodge block is about what is **physically coherent** —
a ground roll in mid-air is nonsense however you got up there.

Implemented as `bBlockedWhileAirborne` on the shared ability base rather than a check inside the
dodge, so block and parry can adopt it with a checkbox when those exist. Left **off** by default:
an air attack is a legitimate thing to want later and this should not quietly forbid one.

Worth flagging against the "costs are paid, not required" rule, which this does *not* violate:
that rule is about stamina never refusing an action, not about state never refusing one. Dodging
is already blocked by `State.Attacking.Committed`, `State.Dodging` and `State.Exhausted`, and
being airborne is another such state. It also does not fall foul of "an input that silently does
nothing is the worst feedback available", because unlike an empty stamina bar, being in the air
is unmistakably legible to the player.

**Once input buffering exists this should probably become a defer rather than a refusal**, so a
dodge pressed just before landing fires on landing instead of vanishing. That is the shape that
makes the rule feel like timing rather than like a wall, and it is noted on the property.

## 2026-08-10 — Jumping is taxed in recovery, not in bar

Jumping costs no stamina and never will, but it suppresses regen from the launch until 0.5 s
after landing.

This is a third category the economy did not previously have. Defensive actions **cost**
stamina and pause regen; jumping only pauses. So a jump is never refused, never contributes to
exhaustion, and cannot be the thing that empties you — but spamming it holds the bar flat, which
matters precisely because the bar is what unlocks the defensive options. The cost of jumping is
paid in *when you get your stamina back*, not in how much you have.

Its tail is 0.5 s against a defensive action's 1 s, deliberately. A jump is a weaker commitment
than a dodge and should not be taxed as heavily; giving it its own number rather than sharing
`StaminaRegenPauseSeconds` is what keeps that adjustable.

**The pause is keyed to the jump action, not to being airborne**, and the distinction is the
whole rule. Walking off a ledge is not something you did, so it costs nothing — driving this
off `IsFalling()` would have taxed falling itself, which is both unintuitive and exploitable in
the wrong direction (it would discourage using terrain). For the same reason it hooks
`OnJumped`, the actual launch, rather than `Jump()`, which only records a button press: a press
that never becomes a jump — held against a ceiling, or pressed while already falling — must not
charge for something that did not happen.

Note this makes jumping while exhausted doubly locked: it is already refused by
`State.Exhausted`, so the pause cannot extend an exhaustion it is not allowed to start.

## 2026-08-10 — Exhaustion ends at full, and the bar was lying about the pool

Two changes from the first play session, one design and one bug they exposed together.

**Exhaustion now lasts until stamina reaches Max, not four seconds.** This supersedes the flat
`ExhaustionSeconds` in "Costs are paid, not required" below.

**Every exhaustion is identical, and that is the intended shape.** Stamina floors at 0, so
overspending does not exist as a concept: dodging at 50 and dodging at 3 both land on exactly 0
and recover at the same rate. An earlier draft of this entry claimed the change made the
punishment scale with how empty you ran yourself — it does not, and could not without letting
stamina go negative. That alternative was raised and rejected: a cost that does not appear on
the bar is invisible state, which is exactly how the base-value bug below stayed hidden.

What ending on recovery buys is the **exit condition, not the duration — and not protection of
any kind**. The old timer released you at roughly 62 stamina, the new rule releases you at 100,
and under either one you are free to spend straight back down to 0 and exhaust yourself again.
That is the system saying yes and stamina deciding; a rule that prevented it would contradict
"costs are paid, not required" directly. An earlier draft of this entry sold the change as
stopping exactly that, which was both factually wrong — nothing blocks a dodge the instant
`State.Exhausted` clears — and the wrong shape for this design.

The actual gain is that the exit state is *uniform and knowable* rather than an accident of
arithmetic. 62 was never a designed number; it was whatever `ExhaustionSeconds` and
`StaminaRegenPerSecond` happened to produce together. Retune regen to 10/s and that same four
second timer returns you at about 25 — exhaustion would hand back a bar nearly as empty as it
found you, and nothing would announce that the mechanic had stopped working. Ending at Max is
invariant to both numbers. It costs about 5.5 s rather than 4 s, and it removes a second number
that can disagree with the bar. `ExhaustionSeconds` is deleted rather than kept as a floor: nothing
refills stamina instantly, so a floor would be an unreachable branch, and this morning's cleanup
is the argument against leaving those lying around.

The consequence to hold onto: **regen continuing during exhaustion is now load-bearing, not
merely humane.** It was previously a kindness that stopped exhaustion being inescapable; it is
now the only mechanism that can end it. Anything that suppresses regen while exhausted makes
exhaustion permanent. Block is the obvious future hazard, since a held block keeps
`State.StaminaRegenPaused` applied and `ActivationBlockedTags` gates activation rather than
continuation.

**The bug: attribute *base* values were never clamped.** Regen ticks through
`ApplyModToAttribute`, which writes the base value, and only `PreAttributeChange` was
overridden — which guards the current value. So the base climbed past Max unbounded while the
bar read a correctly clamped 100. Measured live at base **105.33** against a displayed 100.

This is worth recording because of how it presented rather than what it was. The reported
symptom was "two dodges reach zero but do not exhaust, and a third dodge is wrongly allowed",
which reads as a bug in the exhaustion rule. It was not: the pool genuinely held more than the
bar showed, two dodges left a fraction rather than zero, and the HUD's `%.0f` rendered that
fraction as "0". Every layer was behaving correctly on the value it was given. It also means
the standing check "a dodge from full reads exactly 50" would have failed — it would have read
55.33 — so the check was right and had simply never been run.

`PreAttributeBaseChange` now clamps alongside the other two. All three are needed and they
catch different writes: current values, direct base writes, and instant effects.

**The debug auto-attacker resets to its spawn transform before each swing.** Attack montages
carry root motion, so a dummy attacking on a loop walked itself to the edge of the map. Zeroing
`AnimRootMotionTranslationScale` was rejected: suppressing the lunge shortens the dummy's
effective reach, and reach is the thing spacing tests measure — it would have quietly made the
open question about the dummy's mobility worse. Resetting *before* the swing rather than after
also means every attack starts from an identical transform, so repeated measurements compare.

**`TD.DebugCombatTiming` now defaults to on.** Every real bug in the timing model was found by
measuring, and this session added one more found by reading `baseValue`. It costs nothing when
nothing is attacking, and the standing advice to reach for it early is better served by not
having to reach at all. Turn it off when combat stops being the thing under test.

## 2026-08-10 — `CommitAbility` is gone from every ability

Removing `CostGameplayEffectClass` left the call that *checks* it in place. All three combat
abilities still opened with `if (!CommitAbility(...)) { EndAbility(...); return; }`, and the
dodge's carried a comment describing the gate as live: *"Failing here is the 'not enough
stamina to dodge' case."* That is precisely the behaviour the entry below reversed.

The calls were provably inert — `CommitCheck` tests cost and cooldown, and every ability has
both set to `None`, verified on the CDOs before removing anything. So this changes nothing at
runtime. It is worth doing anyway because the residue was a loaded trap: `Docs/Working-In-Unreal.md`
lists "an action that silently does nothing at low stamina means `CostGameplayEffectClass` has
crept back in" as a standing regression check, and a `CommitAbility` call sitting in
`ActivateAbility` under a comment endorsing the gate is the most likely way for it to creep.
Deleting the call makes the gate unrepresentable rather than merely unused — the same move as
collapsing three hold thresholds into one per-branch number so a threshold could not sit inside
a band.

**`EffectOnEnd` is now an unused hook.** It was added to carry the second half of the regen
pause; that half now falls out of `RegenSuppressedUntil` on the character being pushed forward
while the tag is present, so nothing needs applying when an ability ends. The property is kept
rather than deleted — it is a reasonable general hook and removing a `UPROPERTY` is a reflection
change — but its comment now says plainly that nothing sets it, because a doc comment claiming a
property carries a mechanism it no longer carries is exactly the drift this log exists to catch.

Two smaller corrections in the same pass. `DefaultEffects` was documented as "where stamina
regen lives" months after `GE_StaminaRegen` was deleted and regen moved into C++ forty lines
below it. And `GA_Dodge` had an empty `AbilityTags` while `GA_Attack` carries `Ability.Attack`;
the dodge now carries `Ability.Defend.Dodge` there too. `AbilityTags` is what
`CancelAbilitiesWithTag` and `BlockAbilitiesWithTag` match on, so an ability that cannot be
named cannot be cancelled — which block and parry will almost certainly want to do.

## 2026-08-10 — Costs are paid, not required; the system says yes and stamina decides

**No action is ever refused for want of stamina.** Dodging at 30 stamina works, empties the
bar and exhausts you. It does not silently fail.

This replaces GAS's `CostGameplayEffectClass`, which is a *gate* — checked in
`CanActivateAbility`, refusing the activation if the cost cannot be met — with an
`EffectOnStart` on the shared ability base that simply applies. The distinction is the whole
design: this combat is meant to be free-flowing and responsive, with **stamina management as
what unlocks the system's potential rather than what prevents you using it**. An input that
does nothing is the worst possible feedback, because it is indistinguishable from a dropped
input.

That only works if overspending is punished, which is why exhaustion could not be deferred
to last as originally planned. Until it existed there was no downside to anything.

**Exhaustion is a lockout on acting, not on recovering.** At zero stamina `State.Exhausted`
is applied for 4 s, blocking defensive actions and jump — but regen keeps running throughout.
Stopping regen too would make exhaustion inescapable rather than punishing.

**Any defensive action cancels an attack, not just block.** This supersedes the reading in
"2026-08-10 — The four questions gating defense, settled", which took `CLAUDE.md`'s "can
cancel attack startup into block" literally and wired dodge to be *blocked* by
`State.Attacking`. The boundary is unchanged — cancel until the attack commits, never after
— but it is now stated once, by a `State.Attacking.Committed` tag applied at the commit
checkpoint, which defensive abilities block on instead of `State.Attacking`. Adding block and
parry needs no new cancel logic.

## 2026-08-10 — The stamina economy is orchestrated in C++, not by GameplayEffects

Regen, its pause, and exhaustion live on `ATDCombatCharacter` rather than in a periodic
GameplayEffect with tag requirements.

**This is not a move away from GAS.** Attributes, tags and the ASC are unchanged, and the
AttributeSet was always C++ — GAS *is* a C++ framework. What moved is only the orchestration.
The framing of "GAS or C++" as alternatives was a mistake in the discussion that led here;
the real axis is what is *authored in a details panel* versus what is *code*.

The reason is concrete: UE 5.8 expresses a GameplayEffect's tag behaviour through
`gEComponents`, which cannot be scripted, and the inline containers accept writes while
reading back empty. So every tag-gated effect needs a human in the editor. The economy is
also a small state machine — a pause that outlives the action causing it, a timed lockout —
which is clearer as fifty lines of C++ than as three effects and their components.

Consequences accepted:

- The regen rate, pause and exhaustion duration are now `UPROPERTY`s on the character rather
  than effect assets. Still designer-tunable per Blueprint, but no longer swappable per
  effect, which would matter if different characters ever needed different economies.
- Regen watches for the pause tag rather than abilities telling it to stop. That is
  deliberate: an ability that is cancelled or interrupted cannot leave regen suppressed
  forever, because nothing is holding a flag it might fail to clear.
- `GE_StaminaRegen` and `GE_StaminaRegenPause` were deleted rather than left unused, since an
  effect that looks live and is not is worse than no effect at all.

## 2026-08-10 — I-frames last exactly as long as the dodge

`CLAUDE.md` says the dodge "grants i-frames for the duration", and it is now built that way:
one number, `DodgeSeconds`, with no separate i-frame window.

The rejected alternative was mine, and it had been built before anyone questioned it — an
authored `IFrameSeconds` of 0.3 inside a 0.5 s dodge, leaving a vulnerable recovery tail. It
was defensible on feel-goal grounds ("clear punish opportunities") but it was not in the
spec, and more damningly the two numbers had no stated relationship. 0.3 and 0.5 were both
guesses, and nothing said what made either right or what should happen to one if the other
moved.

So a whiffed dodge is **not directly punishable**. Its cost is 50 stamina and being unable
to act for the duration, and spam is bounded by the stamina economy rather than by
vulnerability. This is a real design position, not a simplification: it says the risk of
dodging is resource commitment, not exposure.

There is precedent for this working. Divine Knockout and New World both shipped dodges whose
cost was resource and commitment rather than exposure. New World's problem was not the model
but its erosion — accumulating enough ways to reduce or refund a dodge's cost that players
became uncatchable. That is a failure of *degree*, so it is recorded as something to watch as
the economy grows, **not** as a rule against ever discounting a dodge. Divine Knockout avoided
the question entirely by putting dodge on a flat 3-second cooldown.

If dodge proves too safe in play, the fix is a recovery window authored in **absolute time**
and i-frames derived as `DodgeSeconds - RecoverySeconds` — never a fraction. What makes
recovery punishable is how it compares to an attack's startup, and a fraction silently
shrinks the punish window below anything usable whenever the dodge is retuned faster. That
is the same reasoning that keeps the attack ladder in absolute milliseconds.

## 2026-08-10 — The dodge authors its duration and derives the play rate

The roll clips are 0.9 s; `DodgeSeconds` was guessed at 0.5 s before any clip existed. Rather
than accept the clip's length or re-cut the animation, the montage is played at whatever rate
makes the authored duration true — `MontageLength / DodgeSeconds`.

This is deliberately the same shape as the attack ladder, where a branch authors *when it
hits* and every play rate is derived at runtime. The alternative — treating the clip's length
as authoritative and tuning the design around it — inverts that, and would make the animation
the balance authority for the second time in this project. It was rejected for i-frames
earlier today for the same reason.

Two consequences worth stating plainly:

- **Rate changes duration, not distance.** A roll played at 1.8× covers the same ground in
  half the time. If the *travel* is wrong for our spacing, the knob is
  `AnimRootMotionTranslationScale`, which is a separate decision and still open.
- A large enough rate multiplier will read as fast-forwarded rather than snappy. If the
  number needed turns out to be extreme, that is evidence the roll is the wrong clip for a
  reactive defensive option, not evidence that the rate needs raising further.

## 2026-08-10 — Sword and shield, rolls for every evade, root motion first

**The melee archetype is sword and shield.** It is the largest set in the library (1,046
assets) and the only stance with a shield, which is what makes block's 180° forward coverage
read as a thing the character is doing rather than a rule the game is enforcing. Reach also
grows, which serves spacing — the top feel goal — and incidentally makes i-frames less
fiddly to test.

Chosen over staying unarmed, which was free but leaves block unmotivated and reach short,
and over one-handed sword, which ships **no evasive animations at all** — a mismatch on
precisely the move being built next.

The cost is real and lands on offense, which is currently verified and working: new attack
clips mean re-authoring the montage, re-placing the `Release Window` notify by hand (notifies
cannot be placed programmatically) and updating `ReleaseStartSeconds` to match. This is why
the swap is its own slice rather than something folded into the dodge work — the timing model
is the most expensive thing in the project to re-verify, and it should not be disturbed
halfway through building something else.

Note also that the library has **no shield mesh at all**, so the shield is a placeholder
primitive until one is sourced. That is a prop gap and does not affect the animations.

**Every evade is a roll, in all eight directions.** The library has no forward dodge in any
archetype, while rolls cover all eight — so the choice was between mixing two move types
under one button or committing to one. One move type wins: a dodge that is a sidestep in
four directions and a roll in the other four would need different i-frame windows, different
durations and different punish profiles depending on which way you pressed, which is a lot of
hidden complexity for a defensive option that has to be read instantly.

The accepted cost is that a roll is longer and more committal than a sidestep. That is a
tuning problem — `IFrameSeconds` and the recovery tail after it are exactly the knobs for it
— and it is arguably the right shape anyway, since dodge costs half the stamina bar and
ought to feel like a commitment.

**The mechanic keeps the name Dodge.** The tags, input and ability are all `Dodge`
(`InputTag.Dodge`, `State.Dodging`, `UTDDodgeAbility`); "roll" is what the animation is, not
what the move is. Renaming would churn the spec, the ini and the code to describe a clip.

**Displacement stays authored, via root motion.** Every dodge and roll in the library is
root motion and none is in place, so this is the default rather than a preference. Root
motion can be switched off on the montage and displacement driven in code without new
assets, so the cheaper experiment runs first; revisit if the distance proves untunable
against spacing.

## 2026-08-10 — The focus-return frame hitch reproduces; it is a trigger, not an anomaly

The windup-skipping hitch described under "2026-08-09 — Branches are described by when they
hit" happened again, on the same trigger: the first attack thrown after clicking into the
PIE window to return focus. It presents as a visibly stuttery light that deals no damage,
and the damage total confirms the mechanism rather than merely resembling it — three
subsequent attacks left the dummy at exactly 20 health, so the phantom attack contributed
zero, meaning its release window opened before any trace existed.

That entry called it "observed once", and its "not worth chasing yet" rested partly on
being unable to reproduce it. It is now reproducible on demand, which changes the
assessment even though the conclusion holds for now:

- It is still an **editor-only** trigger. Nothing has shown a hitch large enough to do this
  arising from gameplay, and a packaged build has no equivalent of returning window focus.
- It remains worth fixing eventually, because the failure is silent. An attack that arrives
  with no windup is unfair; one that arrives with no windup *and* no hitbox is merely
  confusing, which is why this has never cost anything yet.
- The fix, when it is worth doing, is to clamp the frame delta the ability reasons about,
  not to special-case focus. Nothing currently clamps it.

Do not treat a stuttery, damage-less first attack after clicking into PIE as a regression.
Anything else of that shape is not this.

## 2026-08-10 — The four questions gating defense, settled

**Block and parry keep separate buttons.** Block is hold-RMB, parry is MB4 or LAlt+RMB, and
each is active on the frame it is pressed. This supersedes the closing line of "2026-08-09 —
Ability input is routed by gameplay tag", which anticipated that they would share one;
`InputTag.Block` and `InputTag.Parry` were already authored separately in
`DefaultGameplayTags.ini`, so the shared-button claim was the outlier rather than the plan.

The tap-versus-hold resolution that works for the attack ladder does not transfer, and why
is worth keeping. An attack can defer its identity because a windup is playing either way,
so nothing is lost while the branch is undecided. Defense has no such runway: block must be
guarding on the frame it is pressed, and parry must open its window on the frame it is
pressed. Any scheme that waits to see which one you meant has already spent the time that
made pressing it worthwhile. Light/heavy/charged are a ladder of commitment; block and parry
are two different reactions to the same instant.

**An attack can be cancelled into block until it commits, not until it coils.** Punishment
is meant to attach to committing to a move, not to having wound one up. The commit
checkpoint is already the instant the attack resolves, so ending the cancel window there
reuses a boundary the model has rather than inventing a second one.

The alternative considered was ending it when the coil starts, on the grounds that a 700 ms
charge abortable on reaction makes the most committal attack the safest. What defuses that:
the cancel is into **block only** — never into another attack, a dodge, or a free action —
so an aborted charge has spent up to 700 ms achieving nothing and ends up holding a guard
that drains stamina. That is a real cost; it is simply not a punish.

**The minimum stamina economy ships with the first defensive feature**, rather than after
all three. Dodge costs 50 of a 100 pool, so without costs, regen and the regen pause there
is nothing to judge — the cost *is* the design. Exhaustion is deferred until a second
defensive action exists to be locked out of: with only dodge built, "exhausted" and "not
enough stamina to dodge" are one state wearing two names.

**Dodge is the first defensive feature**, being the most self-contained of the three, and
building it is what forces the stamina economy into existence.

## 2026-08-10 — The GA_LightAttack fallback is removed

`GA_LightAttack` was left on disk unreferenced when `GA_Attack` replaced it, as insurance
in case the branch model did not work out. It did work out and has been playtested, so the
fallback now insures against a system that is verified — which git history already does,
and better. Keeping a second ability answering `InputTag.Attack` also keeps the original
bug within reach: granting both fires both on one press.

`UTDMeleeAttackAbility` stays. It is the abstract base `UTDChargedAttackAbility` derives
from and carries the montage, trace and damage plumbing; only the Blueprint went.

Supersedes the closing note of "One ability with three branches, not three abilities".

## 2026-08-09 — Branches are described by when they hit; every play rate is derived

A branch authors `ReleaseAtSeconds` (when its hitbox goes live), `HoldUntilSeconds` (the
input boundary) and `ReleaseSeconds` (how long the hitbox lasts). Nothing authors a play
rate. Windup, coil, commit and release rates are all computed at runtime from the
montage's **measured** position.

The key inversion: **the shared windup runs at whatever rate the *fastest* branch needs**
— `ReleaseStartSeconds / Branches[0].ReleaseAtSeconds`, currently 1.44 — and slower
branches are produced by the coil holding them back, never by a later branch accelerating
to catch up. Authoring a per-branch commit rate was tried first and worked, but it made
the light visibly snap mid-swing and silently halved its active window, because a rate
chosen for the windup carried on through the release.

Deriving from measured position rather than assumed position is not a detail. Two separate
bugs came from assuming: a coil rate computed from where the coil was *meant* to start
overran the release window, so the notify fired before any trace existed and the charged
attack dealt no damage at all while still applying its tag.

Costs accepted, in rough order of how much they matter:

- **Timing lands within about one frame, biased late** — measured 253 / 509 / 769 against
  targets of 250 / 500 / 750. The release notify is detected on a frame boundary, so it
  can only ever be late.
- **A true freeze is now impossible.** Zero is floored out of every rate. If a design ever
  wants a genuinely held pose it needs a different mechanism, not a play rate.
- **`ReleaseStartSeconds` duplicates the notify's placement**, because notifies cannot be
  read off a montage and the windup rate is needed before any notify fires. The ability
  compares itself to reality when the window opens and warns on drift — mitigated, not
  prevented.
- **`CoilEndSeconds` must stay below `ReleaseStartSeconds`** and nothing enforces it at
  author time. Violating it reproduces the silent no-damage bug exactly.
- **Play rates are no longer readable from the asset.** What the montage actually does is
  a computation, which costs debuggability; the temporary `LogTDCoil` output is the
  substitute.
- **A long enough frame hitch skips an attack's windup entirely.** Observed once when
  clicking into the PIE window returned focus: a single frame advanced 0.333s, carrying
  the montage to 0.48 and firing the release window's begin and end 9ms apart, before the
  attack had even committed. Harmless in the editor, but an attack that arrives with no
  windup is a fairness problem if it can ever happen in play. Not worth chasing yet;
  worth remembering that nothing currently clamps a frame delta.

## 2026-08-09 — Reactability is measured from the tell, not from the press

A 500 ms heavy windup looks reactable if you read the number on its own. It is not,
because reacting to a heavy first requires knowing it is *not* a light, and nothing
distinguishes them until the coil appears. The window a defender actually gets is coil →
damaging: roughly 240 ms for the heavy, which sits right at the edge of human reaction.
The charged attack is genuinely reactable because its coil holds long past the point a
heavy would have committed, which is itself the tell.

Consequences worth holding onto:

- Lengthening a windup does **not** by itself make an attack more reactable. Moving
  `CoilStartSeconds` earlier does, and so does giving a branch its own animation.
- The shared windup is not only an authoring convenience — it is the mechanism that keeps
  the reaction window short. Giving a branch a distinct `MontageSection` later buys
  readability at the direct cost of that branch's ambiguity.
- The light is unreactable because it never coils at all, not because it is fast.

This corrects a wrong edit to CLAUDE.md made the same day, which downgraded the heavy to
"reactable" purely on the size of the number.

## 2026-08-09 — Documentation: a decision log, not per-system design docs

Per-system design docs were considered and rejected. Header comments in this codebase
already carry local rationale well, and a doc that describes a system drifts out of sync
with it — at which point it is worse than nothing, because it gets trusted over the
code. The concrete evidence: `WindupSeconds` sat in `TDChargedAttackAbility.h` documented
as "the most important number in the system" while nothing in the `.cpp` ever read it. A
system doc written alongside it would have repeated that error confidently.

A decision log degrades honestly instead: a dated entry about what was chosen stays true
even once the code changes. `Docs/Working-In-Unreal.md` was split out for the same
reason — it was previously held only in per-machine assistant memory, invisible to the
repo and lost on a machine change.

## 2026-08-09 — Attack phases are windup / release / recovery; coil sits inside windup

**Compacted 2026-08-11 — this entry recorded nothing the other docs did not.** Audited against
the new bar and found to be the only one of 27 that was fully duplicated: the vocabulary and the
coil-inside-windup ruling are in `CLAUDE.md`'s **Combat Vocabulary**, and the reason
`UAnimNotifyState_MeleeWindow` cannot be renamed — placed notifies serialize against the class
path — is in `Docs/Working-In-Unreal.md` under **Not scriptable at all**. Both are better homes:
one is loaded every session, the other is read before touching notifies.

Kept as a stub rather than deleted so the date survives and a grep for the vocabulary lands
somewhere that points onward. Full text in git before `2026-08-11`.

## 2026-08-09 — Windups are preset, not resolved at the moment of release

Previously the attack resolved the instant the button came up, so a 251 ms hold produced
a heavy that came out at 251 ms. Light was strictly dominated: there was no reason ever
to tap. Now the windup runs to a preset length, and holding through a checkpoint
escalates to the next tier and its longer windup. Releasing early inside a band changes
nothing.

That delay *is* the heavy's cost, and it is what buys light a reason to exist. The
consequence to accept is dead time: release at 260 ms and nothing visibly happens until
the attack commits.

Because a band's edge and the previous tier's windup length are the same instant, each
branch needs only one number. `MinHoldSeconds`, the ability-level `WindupSeconds` and
`MaxHoldSeconds` collapsed into a single per-branch `WindupSeconds`, which makes the old
bug — a threshold sitting inside a band — unrepresentable.

## 2026-08-09 — The coil creeps toward a ceiling rather than freezing

A hard freeze at the coil point would guarantee every branch identical impact frames, but
reads as a statue over a long charge. A slow creep reads as tension instead. Left
open-ended the creep is a bug: at `CoilPlayRate` 0.1 a charged hold advances the montage
past the first impact frame, so the attack would fire with part of its active window
already spent.

`CoilCeilingSeconds` caps how far the coil may advance, just short of the first active
frame. Every branch keeps its full release window; only long holds ever reach the cap.

## 2026-08-09 — One ability with three branches, not three abilities

`GA_Attack` (a Blueprint of `UTDChargedAttackAbility`) replaced `GA_LightAttack` rather
than joining it — both answer `InputTag.Attack`, and granting both fires both on one
press. `GA_LightAttack` is left on disk unreferenced as a fallback.

The ability's own asset tag is the generic `Ability.Attack`. Which of the three it turns
out to be is decided at commit time, so it cannot be an authored tag; C++ applies
`Ability.Attack.{Light,Heavy,Charged}` as a loose tag while the swing runs. That loose
tag is also how the debug HUD reveals which attack was actually thrown — the intended
verification method, rather than inferring it from the animation.

## 2026-08-09 — One shared animation for all three attacks

An attack's identity is a *consequence* of how long its windup lasted, so it cannot be
known up front to pick a clip. Branches carry an optional `MontageSection` for when one
earns its own animation later; until then all three share the strike.

## 2026-08-09 — Ability input is routed by gameplay tag

`UTDGameplayAbility::InputTag` is matched against a `UInputAction → FGameplayTag` map on
the character, so adding an ability is a content change rather than a code change.
`InputTag.*` is deliberately a separate namespace from `Ability.*`: the input is not the
move. One press of `InputTag.Attack` resolves to light, heavy or charged depending on how
long it is held, and block and parry will share a button.
