// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_MeleeWindow.generated.h"

/**
 *  Marks the release phase of an attack on a montage timeline -- the frames during which it can
 *  deal damage.
 *
 *  It does no tracing and applies no damage: it only sends Event.Melee.WindowBegin / WindowEnd to
 *  the owner's ASC. The ability owns the trace and the damage effect, because that is where the
 *  ability level, source tags and effect context live, which keeps this notify reusable by every
 *  branch without modification.
 *
 *  **Do not rename the class.** Placed notifies serialise against the class path, so a rename needs
 *  an ActiveClassRedirects entry, breaks every montage already using it, and notify placement
 *  cannot be repaired programmatically.
 */
UCLASS(meta = (DisplayName = "Release Window"))
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
