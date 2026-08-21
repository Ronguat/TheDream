// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ParryGesture.generated.h"

/**
 *  Marks the boundary on the parry clip where the parry motion **ends** and the recovery begins.
 *
 *  **Place it at the end of the parry, not at the visual peak of the catch.** Clip start to the
 *  marker is stretched to fit ParryWindowSeconds and the marker to clip end to fit
 *  ParryWhiffRecoverySeconds, so one placed marker authors both halves. A marker at the peak --
 *  earlier than the motion resolves -- silently compresses the parry and stretches the recovery,
 *  and nothing about the result looks like a misplaced notify.
 *
 *  It declares the clip's geometry and never decides when the parry is live. The negation window is
 *  a timestamp checked in Tick, unreachable from this class, so retiming the clip, moving this
 *  marker or deleting the montage changes how the parry *looks* and cannot change what it *does*.
 *
 *  A UAnimNotify rather than a UAnimNotifyState because there is one boundary, not two; the spans
 *  either side are bounded by the clip's own ends.
 */
UCLASS(meta = (DisplayName = "Parry Gesture"))
class UAnimNotify_ParryGesture : public UAnimNotify
{
	GENERATED_BODY()

public:

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

#if WITH_EDITOR
	virtual FString GetNotifyName_Implementation() const override;
#endif
};
