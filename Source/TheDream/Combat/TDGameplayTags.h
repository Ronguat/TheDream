// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 *  Tags that C++ plumbing depends on by name.
 *
 *  Declared natively rather than in DefaultGameplayTags.ini so a typo is a compile
 *  error instead of a silently empty tag at runtime. Design-facing tags (Ability.*,
 *  State.*) stay in the ini where they can be edited without a rebuild.
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

	/** Health reached zero. Refuses every ability, from the shared base rather than per asset. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);

	/** A guard was broken. Refuses every ability for GuardBreakStunSeconds. */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_GuardBroken);

	/**
	 *  A block succeeded. Refuses offense only, for a duration the attack authored
	 *  (per-branch BlockstunSeconds). Movement, dodge, parry and the guard stay live.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Blockstun);

	/**
	 *  Cleanly hit. Refuses every ability, defense included, for a duration the attack
	 *  authored, and cancels whatever the victim was doing when it landed.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Hitstun);

	/**
	 *  A guard is inside its minimum duration and cannot be acted out of. Refuses
	 *  attacking, dodging and jumping; movement stays free.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Blocking_Committed);

	/**
	 *  A parry window is live. Refuses every ability for as long as it is up. Applied and
	 *  cleared against bParryWindowOpen rather than GA_Parry's lifetime, which outlives
	 *  the window on a whiff.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Parrying);

	/**
	 *  Your own whiffed parry. Refuses every ability for ParryWhiffRecoverySeconds.
	 *  Enforced in UTDGameplayAbility::CanActivateAbility rather than per-ability
	 *  ActivationBlockedTags, so no ability can be granted without it.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ParryRecovery);

	/**
	 *  Your own dodge, just ended. Refuses parry only, for DodgeRecoverySeconds; movement,
	 *  offense and block are untouched. Refused through GA_Parry's ActivationBlockedTags
	 *  rather than the C++ base.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_DodgeRecovery);

	/**
	 *  On the floor. Refuses every ability for the jail, then only some of them for the
	 *  choice window; supersedes hitstun and takes movement for the whole span. One tag
	 *  spans both phases -- the jail and the choice window are timestamps beneath it.
	 *  Clears at the stand boundary, not when a rise begins.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_KnockedDown);

	/**
	 *  You were parried. Refuses every ability and takes the full movement lock, for
	 *  ParryLockoutSeconds, authored per branch and per swing.
	 */
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_ParryLockout);
}
