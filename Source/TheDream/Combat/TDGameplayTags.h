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
	 *  A block succeeded. Refuses offense and parry for a duration the *attack* chose.
	 *
	 *  **The counterpart to State_GuardBroken, and the pair only makes sense together.** A guard that
	 *  fails costs you everything for a fixed stun; a guard that works costs you initiative for as
	 *  long as what you blocked deserves. So this refuses attacking and parrying while leaving
	 *  movement, dodging and the guard itself alone -- the defender never released the button, and
	 *  taking their guard away for blocking correctly would invert the mechanic.
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
}
