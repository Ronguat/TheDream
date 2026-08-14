// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDGameplayTags.h"

namespace TDTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Melee_WindowBegin, "Event.Melee.WindowBegin", "Active frames start; begin hit tracing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Melee_WindowEnd, "Event.Melee.WindowEnd", "Active frames end; stop hit tracing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Damage, "Data.Damage", "SetByCaller key for health damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_StaminaDamage, "Data.StaminaDamage", "SetByCaller key for stamina damage dealt to a blocking target.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead", "Health reached zero. Refuses every ability until revived.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_GuardBroken, "State.GuardBroken", "A blocked hit emptied the bar. Refuses every ability for the stun's duration.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Blocking_Committed, "State.Blocking.Committed", "A guard is inside its minimum duration. Refuses everything but movement.");
}
