# Closing down a session

**Read this when the user says to wind down, and not before.** It is a procedure with one trigger,
which is why it lives here rather than in `CLAUDE.md` — that file is loaded in full every session
and carries rules and current facts, while this fires perhaps once in a session and never
unprompted. Moved out 2026-08-14, on the same reasoning that keeps the known-traps list in
`Docs/Combat-Decisions.md`: **content with a single, explicit trigger does not belong in the file
that is always read.**


**Run this only when the user says so.** Whether a session continues or concludes is theirs to decide
and never yours to infer — the user took that responsibility explicitly on 2026-08-14, when this read
*"or when a session ends on a finished item"* and thereby handed the judgement over. **A session
ending on a finished item is not a session the assistant may end.**

It exists because the 2026-08-12 audit found eight wrong claims across the docs, and **every one of
them was cheap to catch at a boundary and expensive to trip over later**. Steps 4 and 5 are the
audit in miniature; the rest is making sure nothing is left on the floor.

1. **Make the editor state safe.** `AssetTools.save_assets` **naming what you touched**, then
   **`git status` — and read it.** Calling save is not the check; *seeing the files listed* is. A
   write that was never saved and a write that is saved but not yet live look identical from inside
   the editor.

   **The empty list saves the level too.** After a CDO write that has not been through a restart,
   that serialises the stale value into placed actors as per-instance overrides, permanently — see
   `Working-In-Unreal.md`. Name paths instead; `git checkout` on the `.umap` is the way back, but
   the binary diff cannot tell a baked override from a real level edit.

   **If the editor is reopened and offers to restore auto-saved packages, decline.** The only thing
   ever stranded is fixture state — `DebugAutoAttackStringTaps`, a defend mode — which is
   deliberately never persisted. **Accepting bakes it into `L_CombatTest` permanently**, and a
   scenario then runs a configuration nobody chose. Declining loses nothing.

   **Closing the editor is not part of this** (2026-08-12). Winding down a session does not mean
   ending the user's, and leaving it open costs nothing now that the save is verified against
   `git status` rather than against the act of closing. Anything mid-session that *does* need the
   editor closed — a header change, a full rebuild — still announces first; that rule lives in
   `Docs/Working-In-Unreal.md`, where every other reason to close it lives.
2. **Run the loop, and read what it says rather than its exit code.**
   `./Tools/RegressionCheck/regression-run.sh --all`, then the real-time canary
   `--family tier --realtime` and `string-cadence --realtime`. It takes about 32 minutes at the
   fixed clock for 88 rows *(1918 s, 2026-09-03)*, so background it and do step 4 while it runs.

   **A red row is a correctness item and blocks the push**, whether or not this session touched
   what it covers. **A CHANGED row is not a failure**: the golden skeleton reports what moved
   whether or not anything asserts it, so explain it in the handoff and accept its skeleton
   (`--accept-golden`) in the same commit as the change that caused it — never in a commit of its
   own, which is how an unexplained behaviour change gets a signature.

   **An UNPROVEN mutation is worse than a red**: it says the row could not have failed, so its
   green means nothing. Fix the mutation before trusting anything that row reports.

   **The canary's own CHANGED rows mean nothing** *(measured 2026-09-03)*: skeletons carry frame
   numbers, and frame numbers are a property of the clock, so every real-time row differs from a
   fixed-step baseline by construction. **Read the canary's assertions, never its skeleton** — its
   job is that the bands hold on both clocks, and all three `tier` rows passed 5 of 5 on each.
   `--realtime` skips scripted rows, whose plans are authored in frames; and its hygiene readout can
   catch a legacy row's dummy dead or mid-swing where the fixed clock did not, which is the clock
   moving the teardown, not a leak.

3. **Leave nothing verified uncommitted, and *propose* the push.** Commit anything finished; the
   push itself waits on the completion gate in Working Rules, so this step ends by naming what is
   ready to go rather than by sending it. For anything deliberately left out, say so and why —
   pending *tuning* does not block a push, pending *correctness* does.

   **Run step 4 before committing.** These two are numbered in the order you *think* about them and
   executed in the reverse, because step 4 audits the very files step 3 would commit — commit first
   and every fix it finds becomes a second commit for no reason. Noted 2026-08-14, after doing it
   this way by instinct and being asked why the procedure appeared to skip a step.
4. **Audit the two always-read files. `CLAUDE.md` in full, every time.** It is short enough that a
   full pass is cheap, and it is the pinnacle of what this project is — it earns one. For
   `Docs/Working-In-Unreal.md`, re-read what you added today.

   **Two questions of every line, because the two files fail differently:**
   - *Is this a rule, or the story of how the rule was learned?* Stories go to
     `Docs/Combat-Decisions.md` or stay in git. **Dates survive on capability claims** — a limit is
     a measurement with a shelf life — and die on incidents.
   - *Does this need re-reading **every** session, by someone who may not touch this system at
     all?* A true rule that only binds one system is triggered content and belongs behind a
     trigger. This clause is the one that was missing: a spec section passes the first question
     cleanly and can still not belong.

   **A file cannot trigger its own audit** — that is why this lives here and not in either file.
   The budgets live in `docs-check.sh` (C8), both **backstops rather
   than gates**, and they police different things. `CLAUDE.md` is read in full every session, so
   its number is a real per-session cost and stays tight. **Working-In-Unreal is triggered and is
   *expected* to balloon** — every engine limit re-measured lands there — so crossing its line is
   a prompt to **subdivide along its section seams**, never a request to cut prose that earned its
   place *(reframed 2026-08-24, at the designer's call: the old 534 warned on any session that
   added a measurement, which is the number crowding out the criterion in the other direction)*.
   The questions above are the gate. A line count is checkable in a second and
   fitness is not, so the number will crowd out the criterion unless it is explicitly demoted —
   which is exactly how `CLAUDE.md` passed six length audits while most of it was unfit.

   Then **run `Tools/CommentCheck/comment-check.sh` as well: clear every FAIL, read every WARN.**
   It keeps WHY out of code comments — a date or an attribution in one fails outright, and the
   block-size and narrative WARNs are shortlists for the same eye the trap shortlist needs. It
   cannot catch a comment that describes the wrong declaration; only reading does.

   **A C7 failure is not cleared by `--baseline`.** It means a file outgrew its recorded volume:
   either cut it back, or raise that one line in `Tools/CommentCheck/baseline.txt` and say why in
   the commit. Regenerating wholesale erases the memory the check exists to keep. C7 also names any
   **unbaselined** file added this session and any **stale** entry left by one deleted — neither
   fails, and both are cleared by `--baseline` once the session's file set is settled.

   Then **run `Tools/DocsCheck/docs-check.sh`: clear every FAIL, read every WARN.** It mechanizes
   what used to be manual greps, each invariant commented with the incident that earned it. Two judgments stay yours: `grep -n "supersede" Docs/Combat-Decisions.md`
   — every hit in a dated entry needs a row in the supersession table, and two were missing on
   2026-08-12 — and the trap-shortlist WARN needs an eye, because an orphaned trap body reads as
   prose belonging to whatever precedes it and no grep can tell those apart. That happened on 2026-08-12 and was
   caught by luck rather than by a step; a trap that no longer announces itself is the one edit in
   that file which cannot be reviewed.

   **And if the session moved text between files, re-read both seams — each file's final paragraph
   especially.** One edit in `ef62b17` mangled the tails of both files it touched, and two
   closedown audits then passed them (found 2026-08-18): a content re-read checks fitness, not
   integrity, and the line-count backstop stayed green throughout. `docs-check` now fails on both
   damage shapes; the re-read is for what it cannot see.
5. **Discharge what you fixed.** Did this session fix anything filed as a trap? Clear it *and say
   what discharged it*, in the same commit. Did anything supersede an entry, or make an absence
   claim? Rows and dates, per the rules above.

   **Did a value, symbol or mechanism change? Grep the symbol *and the outgoing value* across
   `Docs/` and `CLAUDE.md`, reconcile every hit — update, demote or supersede — and quote the
   greps in the handoff.** Cost is proportional to the session's delta; skipping it is how one
   session's change left five stale readings for an audit to find. **Combat values moved?
   Regenerate `Docs/Combat-Values.tsv`** — the file's header carries the command — so the mirror
   stays dated to the change.
6. **Update the focus, and route a shipped item's consequences.** If the next item changed,
   `Current Focus` is the only place that says so.

   **This is where the shipped-item routing fires, and nowhere else.** A shipped item keeps only
   its strikethrough in the roster; everything else it carried routes out by the destination table
   in `CLAUDE.md`'s "When a slice ships", and the routing is not finished until every consequence
   is at its destination and you have said where each one went. **Verify each landed before
   removing its old home**, in that order, because the reverse loses content silently.

   Attached here deliberately: the rule was written 2026-08-14 with no trigger, which is how the
   traps re-read failed until it became a step. An unenforced instruction fails.
7. **Check memory is still pointing, not restating.** Only `combat-prototype-state` normally needs
   touching, and only if the state actually moved. Anything a future contributor would need belongs
   in the repo instead.
8. **Hand off explicitly.** Where to pick up, what is verified versus merely written, and what is
   open. **Name anything claimed but not verified** — that is the item most likely to be believed
   next session and least likely to be re-checked.

   **And state what was done beyond what was agreed, or that nothing was.** Added 2026-08-14, and it
   is what makes unattended execution reviewable: the drift that gets caught is the drift that looks
   wrong, so the additions a reasonable person would have made anyway are exactly the ones that go
   unmentioned. Saying "nothing" is a real answer and should be said out loud rather than left to
   silence, which is indistinguishable from not having checked.
9. **Title the session, for the archive. Five words maximum.** What the session *did*, not what it
   touched — "Ship Lunge, lock attack movement" over "worked on combat". Verbs over nouns; no date,
   no item numbers, nothing the archive already knows.

   **The cap is the point, not a nicety.** Five words does not fit a summary, so it forces the
   session's dominant thing to the front and leaves everything else to step 8 — which is where a
   reader who needs detail is going anyway. **A session that did two unrelated things names the
   bigger one**; the title is an index entry, not a record, and the handoff directly above it is
   what stops the smaller one from being lost.

   **Deliberately last, because it is the most compressed artefact and compression is easiest after
   the long form exists.** By this point the handoff has already forced you to say what was done and
   what is open; the title is that, squeezed. Written any earlier it is a guess about a session that
   has not finished being accounted for. Added 2026-08-12 at the user's request, capped the same day.

