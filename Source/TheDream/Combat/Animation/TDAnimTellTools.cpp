// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Animation/TDAnimTellTools.h"
#include "Combat/TDCombatCharacter.h"
#include "Animation/AnimInstance.h"
#include "AnimExecutionContextLibrary.h"
#include "SequencePlayerLibrary.h"

void UTDAnimTellTools::DriveHitstunTell(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	DriveTell(Context, Node, /*bHitstun=*/true);
}

void UTDAnimTellTools::DriveBlockstunTell(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	DriveTell(Context, Node, /*bHitstun=*/false);
}

void UTDAnimTellTools::DriveTell(const FAnimUpdateContext& Context, const FAnimNodeReference& Node, bool bHitstun)
{
	EAnimNodeReferenceConversionResult Result = EAnimNodeReferenceConversionResult::Failed;
	const FSequencePlayerReference Player = USequencePlayerLibrary::ConvertToSequencePlayer(Node, Result);
	if (Result != EAnimNodeReferenceConversionResult::Succeeded)
	{
		return;
	}

	const UAnimInstance* AnimInstance = UAnimExecutionContextLibrary::GetAnimInstance(Context);
	const ATDCombatCharacter* Character =
		AnimInstance ? Cast<ATDCombatCharacter>(AnimInstance->GetOwningActor()) : nullptr;
	if (!Character)
	{
		return;
	}

	// Rate zero before the position, and not optional: FAnimNode_SequencePlayerBase hands the
	// accumulator to the tick record, which advances it by delta * rate after this runs. Any
	// non-zero rate would drift the position set below by one frame's worth every frame.
	USequencePlayerLibrary::SetPlayRate(Player, 0.0f);
	USequencePlayerLibrary::SetAccumulatedTime(Player,
		bHitstun ? Character->GetHitstunTellTime() : Character->GetBlockstunTellTime());
}
