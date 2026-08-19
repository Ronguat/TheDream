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
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Blockstun, "State.Blockstun", "A block succeeded. Refuses offense only -- never defense -- for a duration the attack chose.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Hitstun, "State.Hitstun", "Cleanly hit. Refuses every ability, defense included, for a duration the attack authored.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_ParryRecovery, "State.ParryRecovery", "Your own parry whiffed. Refuses every ability for its duration.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_DodgeRecovery, "State.DodgeRecovery", "Your own dodge just ended. Refuses parry only.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Parry_Gesture, "Event.Parry.Gesture", "The parry clip's gesture reached its read moment; switch to the recovery play rate.");
}
