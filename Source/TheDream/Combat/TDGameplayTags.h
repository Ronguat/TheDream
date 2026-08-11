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
}
