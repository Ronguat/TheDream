// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "TDGameplayAbility.generated.h"

/**
 *  Shared base for every combat ability in the project.
 */
UCLASS(abstract)
class UTDGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:

	UTDGameplayAbility();

	/**
	 *  Input this ability answers to, e.g. InputTag.Attack.
	 *
	 *  Deliberately a separate namespace from the ability's own tags: the input is not
	 *  the move. One press of InputTag.Attack resolves to Light, Heavy or Charged
	 *  depending on how long it is held.
	 *
	 *  Block and Parry deliberately do *not* share a button, unlike the attack ladder:
	 *  both have to be active on the frame they are pressed, so neither can afford to
	 *  wait and see which one was meant. See Docs/Combat-Decisions.md.
	 *
	 *  Input is routed by tag rather than by an integer ID so that adding an ability is
	 *  a content change. The character calls AbilitySpecInputPressed/Released, which sets
	 *  Spec.InputPressed and forwards both edges to live instances -- that is what
	 *  WaitInputRelease observes, and what the hold-to-Heavy conversion needs.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input")
	FGameplayTag InputTag;
};
