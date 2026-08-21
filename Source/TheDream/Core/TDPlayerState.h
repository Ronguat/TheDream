// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "TDPlayerState.generated.h"

class UAbilitySystemComponent;
class UTDAttributeSet;

/**
 *  Carries the Ability System Component and the combat attributes for a *player*.
 *
 *  **Only players have one.** The training dummy is AI-possessed under a stock AAIController and
 *  has no PlayerState at all, so ATDCombatCharacter *resolves* which ASC it uses rather than
 *  assuming this one exists.
 */
UCLASS()
class ATDPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:

	ATDPlayerState();

	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	UTDAttributeSet* GetAttributeSet() const { return AttributeSet; }

	/**
	 *  Whether attributes and abilities have already been seeded onto this ASC.
	 *
	 *  **The flag lives with the ASC, not with the character.** A player pawn's BeginPlay runs
	 *  *before* it is possessed, so a bool on the character would seed the character's fallback ASC,
	 *  set the flag, then swap to this one -- leaving the player with no attributes and no
	 *  abilities, while the never-possessed training dummy worked perfectly throughout. It also
	 *  stops a respawned pawn re-granting every ability and re-stacking every default effect onto a
	 *  PlayerState that already carries them.
	 */
	bool HasSeededDefaults() const { return bDefaultsSeeded; }
	void MarkDefaultsSeeded() { bDefaultsSeeded = true; }

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UTDAttributeSet> AttributeSet;

private:

	/** Authority-only bookkeeping: a client never seeds, so this deliberately does not replicate. */
	bool bDefaultsSeeded = false;
};
