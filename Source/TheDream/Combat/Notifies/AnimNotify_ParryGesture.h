// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ParryGesture.generated.h"

/**
 *  Marks the instant on the parry clip where the gesture has read -- where the character has
 *  visibly caught the blow. Everything before it is the parry; everything after it is the
 *  recovery settling back to neutral.
 *
 *  **An instant treated as a span** (the designer, 2026-08-19). The marker names one frame, and
 *  the two spans either side of it are what the ability actually uses: clip start to here is
 *  fitted to ParryWindowSeconds, here to clip end is fitted to ParryWhiffRecoverySeconds. So a
 *  single placed marker authors both halves of the fit.
 *
 *  **It declares the clip's geometry. It never decides when the parry is live.** That direction is
 *  the whole point and it is the opposite of what a notify usually does here: the animation
 *  conforms to the authored values, never the reverse (the designer, 2026-08-19). The negation
 *  window remains a timestamp checked in Tick, unreachable from this class -- so retiming the
 *  clip, moving this marker, or deleting the montage outright changes how the parry *looks* and
 *  cannot change what it *does*. Making the window a notify would hand a hit-negation window to
 *  the animation, which is the mistake Docs/Combat-Spec.md spent the ladder learning not to make.
 *
 *  The relationship it does have is the one Release Window has with ReleaseSeconds: the notify
 *  says where the clip's own boundary sits, the code says how long the mechanic lasts, and the
 *  play rate is *derived* to reconcile them. Nobody maintains a second copy of either number.
 *
 *  A UAnimNotify rather than a UAnimNotifyState because there is one boundary, not two. The span
 *  it implies is bounded by the clip's own ends, which need no marking.
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
