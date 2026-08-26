// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimNodeReference.h"
#include "TDAnimTellTools.generated.h"

/**
 *  Anim-graph entry points that position a stun tell's playhead from the stun that owns it.
 *
 *  Bound as a sequence player's **On Update**. Each holds its node at play rate zero and sets the
 *  accumulated time explicitly, so the clip's position is a function of stun progress rather than
 *  of elapsed frames. The character supplies the position; see
 *  ATDCombatCharacter::GetHitstunTellTime.
 *
 *  A function library is legal for this binding -- `UAnimGraphNode_Base`'s function properties
 *  carry `AllowFunctionLibraries` -- so no custom `UAnimInstance` is involved and `ABP_Combat`
 *  keeps its parent.
 */
UCLASS()
class UTDAnimTellTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Positions the hitstun flinch. Bind as the flinch sequence player's On Update. */
	UFUNCTION(BlueprintCallable, Category="TheDream|Anim", meta=(BlueprintThreadSafe))
	static void DriveHitstunTell(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** Positions the blockstun reaction. Bind as the blockstun sequence player's On Update. */
	UFUNCTION(BlueprintCallable, Category="TheDream|Anim", meta=(BlueprintThreadSafe))
	static void DriveBlockstunTell(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

private:
	/** Resolves the player and the owning character, then parks the playhead at the tell time. */
	static void DriveTell(const FAnimUpdateContext& Context, const FAnimNodeReference& Node, bool bHitstun);
};
