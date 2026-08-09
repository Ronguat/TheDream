// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "TDGameplayAbility.generated.h"

/**
 *  Identifies which input an ability activates from.
 *
 *  Granted specs carry this ID so input is routed through the ASC
 *  (AbilityLocalInputPressed / Released) rather than by tag lookup. That is what makes
 *  WaitInputRelease work, which the Heavy and Charged Heavy holds depend on.
 */
UENUM(BlueprintType)
enum class ETDAbilityInputID : uint8
{
	None			UMETA(DisplayName = "None"),
	LightAttack		UMETA(DisplayName = "Light Attack"),
	Block			UMETA(DisplayName = "Block"),
	Dodge			UMETA(DisplayName = "Dodge"),
	Parry			UMETA(DisplayName = "Parry")
};

/**
 *  Shared base for every combat ability in the project.
 */
UCLASS(abstract)
class UTDGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:

	UTDGameplayAbility();

	/** Input this ability activates from. Read by ATDCombatCharacter when granting the spec. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input")
	ETDAbilityInputID InputID = ETDAbilityInputID::None;
};
