// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 *  Tags that C++ plumbing depends on by name.
 *
 *  These are declared natively rather than in DefaultGameplayTags.ini so a typo is a
 *  compile error instead of a silently empty tag at runtime. Design-facing tags
 *  (Ability.*, State.*) stay in the ini where they can be edited without a rebuild.
 */
namespace TDTags
{
	/** Sent by UAnimNotifyState_MeleeWindow when the active frames start. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Melee_WindowBegin);

	/** Sent by UAnimNotifyState_MeleeWindow when the active frames end. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Melee_WindowEnd);

	/** SetByCaller key for health damage passed from an ability to its damage effect. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Damage);

	/** SetByCaller key for stamina drain inflicted on a blocking target. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_StaminaDamage);

	/**
	 *  Health reached zero. Refuses every ability, from the shared base rather than per asset.
	 *
	 *  Deliberately native rather than an EditDefaultsOnly UPROPERTY like ExhaustedTag, which
	 *  is otherwise the pattern this copies. A placed actor can serialize stale
	 *  EditDefaultsOnly values that silently override its Blueprint, and the training dummy
	 *  shipped for days with ExhaustedTag unset for exactly that reason -- unreachable from
	 *  the details panel, so nothing showed it. A native tag has no per-instance value to be
	 *  stale. See Docs/Working-In-Unreal.md.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);

	/**
	 *  A guard was broken. Refuses every ability for GuardBreakStunSeconds.
	 *
	 *  Native for the same reason State_Dead is: it is plumbing the C++ depends on by name, and an
	 *  EditDefaultsOnly equivalent can be silently stale on a placed actor -- which is exactly how
	 *  the training dummy shipped for days with ExhaustedTag unset. A native tag has no
	 *  per-instance value to go stale, and this one refuses *every* action, so a defender whose tag
	 *  never applied would look invulnerable to guard breaks rather than merely mis-tuned.
	 *
	 *  Deliberately not State.Blockstun, which was built 2026-08-14 and stayed separate as predicted:
	 *  blockstun is the lockout a *successful* block imposes, this is the penalty for one that
	 *  failed. They differ in cause, duration and what they forbid. See State_Blockstun below.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_GuardBroken);

	/**
	 *  A block succeeded. Refuses offense -- and *only* offense -- for a duration the *attack* chose.
	 *
	 *  **The counterpart to State_GuardBroken, and the pair only makes sense together.** A guard that
	 *  fails costs you everything for a fixed stun; a guard that works costs you initiative for as
	 *  long as what you blocked deserves. So this refuses attacking while leaving movement, dodging,
	 *  parrying and the guard itself alone -- the defender never released the button, and
	 *  taking their guard away for blocking correctly would invert the mechanic.
	 *
	 *  **Parry is deliberately not refused, and this comment said the opposite until 2026-08-18.**
	 *  The "offense + parry" wording was wrong from the day it was written and no implementation
	 *  ever matched it; blockstun and parry never know about each other (the designer, 2026-08-15).
	 *  It was the last surviving copy of a claim Docs/Combat-Spec.md had already corrected, and it
	 *  survived precisely because nothing could contradict it while parry did not exist.
	 *
	 *  Duration is authored per attack branch rather than here, because a heavy should pin a guard
	 *  longer than a light and only the attacker knows which was thrown.
	 *
	 *  Native, and moved out of DefaultGameplayTags.ini to become so, for the reason the two above
	 *  give: C++ applies and reads it by name, and there is no per-instance value to go stale.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Blockstun);

	/**
	 *  Cleanly hit. Refuses **every** ability -- defense included -- for a duration the attack
	 *  authored, and cancels whatever the victim was doing when it landed.
	 *
	 *  The deliberate contrast is with State_Blockstun, which refuses offense only: blockstun is
	 *  the price of a *successful* defence, so the guard and the dodge stay available, while
	 *  hitstun is the price of failing to defend at all -- and refusing defense during it is the
	 *  entire mechanism behind "any hit in the string guarantees the rest". A defender who could
	 *  dodge between string hits would make that rule a lie.
	 *
	 *  Native for the reason its three siblings are: C++ applies it in EnterHitstun, refuses on it
	 *  in CanActivateAbility, and hand-checks it in Jump(); an EditDefaultsOnly equivalent can be
	 *  silently stale on a placed actor.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Hitstun);

	/**
	 *  A guard is inside its minimum duration and cannot be acted out of.
	 *
	 *  **Deliberately named to parallel State.Attacking.Committed**, because it is the same idea
	 *  applied to defence: an action with a window in which the decision has already been made.
	 *  Attacks commit at a checkpoint partway through; a guard commits the moment it goes up.
	 *
	 *  It exists because a guard with no floor could be feathered at input speed --
	 *  attack, block, attack, block -- which read as unfinished in play. Everything is refused
	 *  during it *except movement*: no attacking, dodging or jumping, but WASD stays free, since
	 *  the guard is a stance you carry rather than a place you are pinned.
	 *
	 *  Native for the reason State_GuardBroken is: C++ applies it, C++ reads it in Jump(), and an
	 *  EditDefaultsOnly equivalent can go stale on a placed actor with nothing to show for it.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Blocking_Committed);

	/**
	 *  A parry window is live. Refuses **every** ability for as long as it is up.
	 *
	 *  **The jail's first half** (the designer, 2026-08-19): *"once a parry has been initiated, you
	 *  are jailed and unable to do anything until parry recovery ends, or an attacker overrides it
	 *  via inflicting punishment, OR you parry something successfully during the window."* So the
	 *  commitment starts at **activation**, not at window close, and this tag plus
	 *  State_ParryRecovery span it between them.
	 *
	 *  Until then this was an ini tag that nothing refused on, so a parry could be attacked,
	 *  blocked or dodged out of mid-window -- which made the read free, since the tell arrives in
	 *  time to cancel.
	 *
	 *  ***It marks the window, not the ability, and the difference is not cosmetic.*** It rode in
	 *  GA_Parry's ActivationOwnedTags until 2026-08-19, which was exact while the ability's
	 *  lifetime *was* the window's. The whiff commitment broke that -- a whiffed parry keeps
	 *  GA_Parry alive across its recovery to hold the movement lock -- and **the tag's span
	 *  silently followed the ability, staying up for the whole 900 ms jail** and shadowing
	 *  State_ParryRecovery so completely that the recovery's own refusal became unreachable.
	 *  Applied and cleared against bParryWindowOpen now, so the two tags mean what they are named.
	 *  The general form is worth keeping: **a tag borrowed from an ability's lifetime silently
	 *  re-scopes itself whenever that lifetime changes.**
	 *
	 *  **Declared natively *and* left in DefaultGameplayTags.ini**, following State_Hitstun, which
	 *  has been in both since it shipped. The ini entry is now redundant rather than wrong, and
	 *  removing it would be a project-settings edit for no behavioural gain.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Parrying);

	/**
	 *  Your own whiffed parry. Refuses **every** ability for ParryWhiffRecoverySeconds.
	 *
	 *  **A recovery, not a lockout, and the distinction is the schema** (the designer, 2026-08-19):
	 *  *lockouts are externally inflicted, recoveries are self-inflicted.* Nobody did this to you --
	 *  you threw a read and missed -- so it is a recovery, the same category as an attack's.
	 *
	 *  **It refuses everything, which is what separates it from the gap below.** Until 2026-08-19
	 *  this refused only *defensive* activations and shared one tag with the post-dodge gap, on the
	 *  argument that both say "you may not put a parry window here". The designer's ruling that a
	 *  whiffed parry must prevent acting broke that equivalence: this one now commits you, and the
	 *  gap deliberately still does not. Splitting them is what stopped the ruling from silently
	 *  making every dodge commit you for 0.15 s as well.
	 *
	 *  The refusal itself lives in UTDGameplayAbility::CanActivateAbility rather than in each
	 *  ability's ActivationBlockedTags -- one place, and a future ability cannot be granted without
	 *  it. Same argument State_Dead makes.
	 *
	 *  Native for the reason the others are: C++ applies it, C++ refuses on it, and an
	 *  EditDefaultsOnly equivalent can go stale on a placed actor with nothing to show for it.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ParryRecovery);

	/**
	 *  Your own dodge, just ended. Refuses **parry only**, for DodgeRecoverySeconds.
	 *
	 *  Exists so a predictive dodge cannot chain into a parry that covers the charged, which would
	 *  unscrew the vise's late jaw. Self-inflicted, so a recovery by the schema above -- but a far
	 *  narrower one than the parry's: it takes nothing away except the parry it exists to forbid.
	 *  Movement, offense and block are all untouched, which is the behaviour it had before the
	 *  2026-08-19 split and deliberately keeps.
	 *
	 *  Refused through GA_Parry's ActivationBlockedTags rather than the C++ base, precisely because
	 *  it is one ability's business rather than a global state.
	 *
	 *  **The max-extension in ApplyDodgeRecovery still matters** now that the two are separate
	 *  tags: two dodges landing close together must not shorten the gap below either alone.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_DodgeRecovery);

	/**
	 *  On the floor. Refuses **every** ability for the jail, then only some of them for the choice
	 *  window; supersedes hitstun outright and takes movement for the whole span.
	 *
	 *  **The ninth member of the state family**, and it follows the same contract as the eight
	 *  before it: the server decides, a replicated bool carries the decision, and OnRep applies
	 *  this tag locally. Native for the reason State_Hitstun is -- C++ applies it, refuses on it,
	 *  and reads it in the regen tick -- and an EditDefaultsOnly equivalent can go silently stale
	 *  on a placed actor.
	 *
	 *  **It marks the whole down-state, not the jail.** The jail and the choice window are
	 *  timestamps underneath it, exactly as the parry window and its recovery sit under separate
	 *  tags -- but here one tag spans both, because what the tag *means* to everyone outside the
	 *  character is "this body is on the floor", and that is true for all of it. What differs
	 *  between the phases is which activations the character itself refuses, which is a question
	 *  the timestamps answer and a tag cannot.
	 *
	 *  It clears at the **stand boundary**, not when a rise begins: the rise is committed,
	 *  vulnerable and unactionable, so it is still the floor for every purpose except invincibility.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_KnockedDown);

	/**
	 *  You were parried. Refuses **every** ability and takes the full movement lock.
	 *
	 *  **The tenth member of the state family, and the name's reservation resolves here**
	 *  (2026-08-20). It was held free rather than recycled because the designer had used it before,
	 *  in an earlier project, for exactly this: the state inflicted on an attacker who *has been*
	 *  parried, carrying its own authored properties. That is externally inflicted and so a lockout
	 *  under the recovery/lockout schema; the attacker/defender asymmetry is immaterial.
	 *
	 *  **Its duration is derived, not authored**: the swing's planned authored total minus the time
	 *  already elapsed at the catch. That is pure arithmetic over values the designer already set,
	 *  and it preserves every per-tier punish window for free -- a parried charged pays more than a
	 *  parried light because it had more attack left to lose. ParryLockoutFloorSeconds is the
	 *  authored half, reserved at 0 and spent only if play asks for one.
	 *
	 *  **What it replaced**: the attacker used to ride their own recovery to the end, because a
	 *  parry ended nothing. Now the catch ends the swing through the ordinary funnel -- facing,
	 *  lunge, homing and tags all restored, and the hitbox dead for *everyone*, which matters in a
	 *  crowd -- and this holds them for what remained. Same total, honest cause.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ParryLockout);

	/*
	 *  **There is deliberately no Event.Parry.Gesture tag.** One existed briefly on 2026-08-19,
	 *  when the marker's rate switch was handled inside GA_Parry and needed an event to reach it.
	 *  Moving the switch into the notify itself -- so it survives the ability ending, which a catch
	 *  does immediately -- removed the only reason for the tag, and an unused tag in the registry
	 *  is something a later reader has to disprove. See UAnimNotify_ParryGesture.
	 */

	/*
	 *  **State.ParryLockout's reservation resolved on 2026-08-20** -- it is declared above now, for
	 *  exactly the job it was being held for. The reservation stood from 2026-08-19, when the
	 *  recovery/lockout schema arrived and the name turned out to be wrong for this file's
	 *  parry-whiff tail (self-inflicted, so a recovery). It was kept free rather than recycled
	 *  because the designer had used it before for the state inflicted on an attacker who *has
	 *  been* parried, and that is what it now names.
	 *
	 *  The prediction it was filed on is worth keeping: the lean, fully-derived reward "has a
	 *  significant chance of proving under-authored". ParryLockoutFloorSeconds is the authored half
	 *  held in reserve at 0 against exactly that. See Docs/Combat-Decisions.md, 2026-08-19.
	 */
}
