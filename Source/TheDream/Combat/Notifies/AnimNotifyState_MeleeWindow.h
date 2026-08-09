// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_MeleeWindow.generated.h"

/**
 *  Marks the active frames of an attack on a montage timeline.
 *
 *  The notify deliberately does no tracing and applies no damage: it only sends
 *  Event.Melee.WindowBegin / WindowEnd to the owner's ASC. The ability owns the trace
 *  and the damage effect, because that is where the ability level, source tags and
 *  effect context live. That keeps this notify reusable by Heavy and Charged Heavy
 *  without modification.
 */
UCLASS(meta = (DisplayName = "Melee Window"))
class UAnimNotifyState_MeleeWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif
};
