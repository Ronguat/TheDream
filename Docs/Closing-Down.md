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
them was cheap to catch at a boundary and expensive to trip over later**. Steps 3 and 4 are the
audit in miniature; the rest is making sure nothing is left on the floor.

1. **Make the editor state safe.** `AssetTools.save_assets` with an empty list, then **`git status`
   — and read it.** Calling save is not the check; *seeing the files listed* is. A write that was
   never saved and a write that is saved but not yet live look identical from inside the editor.

   **Closing the editor is not part of this** (2026-08-12). Winding down a session does not mean
   ending the user's, and leaving it open costs nothing now that the save is verified against
   `git status` rather than against the act of closing. Anything mid-session that *does* need the
   editor closed — a header change, a full rebuild — still announces first; that rule lives in
   `Docs/Working-In-Unreal.md`, where every other reason to close it lives.
2. **Leave nothing verified uncommitted, and *propose* the push.** Commit anything finished; the
   push itself waits on the completion gate in Working Rules, so this step ends by naming what is
   ready to go rather than by sending it. For anything deliberately left out, say so and why —
   pending *tuning* does not block a push, pending *correctness* does.
3. **Audit the two files that are read every session, for bloat as well as truth.** `CLAUDE.md` and
   `Docs/Working-In-Unreal.md` are both loaded or read in full at every startup, so **length is a
   correctness problem for them and not only a tidiness one** — a file nobody finishes reading
   protects nobody, and both grew past that point once already. Re-read what you added today and ask
   of each line: *is this a rule, or is it the story of how the rule was learned?* Stories go to
   `Docs/Combat-Decisions.md` or stay in git; the rule stays here. Added 2026-08-13, when
   `Working-In-Unreal.md` was cut in half without losing a single rule.

   Then **check the docs you touched, with three greps.** `grep -n "supersede" Docs/Combat-Decisions.md` —
   every hit needs a row in the supersession table, and two were missing on 2026-08-12. Then confirm
   any cross-reference you wrote resolves to a section that exists; three pointed at a section that
   had been deleted. **Then, if you edited the known-traps section, confirm every trap still has its
   bolded header** — an `Edit` that replaces a header instead of inserting before it leaves the body
   orphaned, reading as prose belonging to whatever precedes it. That happened on 2026-08-12 and was
   caught by luck rather than by a step; a trap that no longer announces itself is the one edit in
   that file which cannot be reviewed.
4. **Discharge what you fixed.** Did this session fix anything filed as a trap? Clear it *and say
   what discharged it*, in the same commit. Did anything supersede an entry, or make an absence
   claim? Rows and dates, per the rules above.
5. **Update the focus.** If the next item changed, `Current Focus` is the only place that says so —
   and completed items keep one line plus whatever they left behind that can still bite.
6. **Check memory is still pointing, not restating.** Only `combat-prototype-state` normally needs
   touching, and only if the state actually moved. Anything a future contributor would need belongs
   in the repo instead.
7. **Hand off explicitly.** Where to pick up, what is verified versus merely written, and what is
   open. **Name anything claimed but not verified** — that is the item most likely to be believed
   next session and least likely to be re-checked.

   **And state what was done beyond what was agreed, or that nothing was.** Added 2026-08-14, and it
   is what makes unattended execution reviewable: the drift that gets caught is the drift that looks
   wrong, so the additions a reasonable person would have made anyway are exactly the ones that go
   unmentioned. Saying "nothing" is a real answer and should be said out loud rather than left to
   silence, which is indistinguishable from not having checked.
8. **Title the session, for the archive. Five words maximum.** What the session *did*, not what it
   touched — "Ship Lunge, lock attack movement" over "worked on combat". Verbs over nouns; no date,
   no item numbers, nothing the archive already knows.

   **The cap is the point, not a nicety.** Five words does not fit a summary, so it forces the
   session's dominant thing to the front and leaves everything else to step 7 — which is where a
   reader who needs detail is going anyway. **A session that did two unrelated things names the
   bigger one**; the title is an index entry, not a record, and the handoff directly above it is
   what stops the smaller one from being lost.

   **Deliberately last, because it is the most compressed artefact and compression is easiest after
   the long form exists.** By this point the handoff has already forced you to say what was done and
   what is open; the title is that, squeezed. Written any earlier it is a guess about a session that
   has not finished being accounted for. Added 2026-08-12 at the user's request, capped the same day.

