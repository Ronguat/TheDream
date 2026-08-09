// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDGameplayTags.h"

namespace TDTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Melee_WindowBegin, "Event.Melee.WindowBegin", "Active frames start; begin hit tracing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Melee_WindowEnd, "Event.Melee.WindowEnd", "Active frames end; stop hit tracing.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Damage, "Data.Damage", "SetByCaller key for health damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_StaminaDamage, "Data.StaminaDamage", "SetByCaller key for stamina drain on a blocking target.");
}
