// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/TheDreamCharacter.h"
#include "AbilitySystemInterface.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDCombatCharacter.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UInputAction;
class UTDAttributeSet;

/**
 *  Base class for anything that can fight: the player and the training dummy alike.
 *
 *  Inherits locomotion and the third person camera from ATheDreamCharacter and adds
 *  the Ability System Component plus the core combat attributes on top. Abilities are
 *  granted from DefaultAbilities, so a Blueprint subclass decides what it can do
 *  without any graph wiring. Leaving that list empty produces a valid damage target
 *  that cannot act, which is exactly what the training dummy needs.
 */
UCLASS(abstract)
class ATDCombatCharacter : public ATheDreamCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:

	ATDCombatCharacter();

	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetMaxStamina() const;

	/** Health as a 0-1 fraction, for health bars and debug readouts. */
	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetHealthPercent() const;

	/** Stamina as a 0-1 fraction, for stamina bars and debug readouts. */
	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetStaminaPercent() const;

protected:

	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Granted on spawn. Empty means this character cannot act (training dummy). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/**
	 *  Which input action activates which ability slot.
	 *
	 *  An ability is reached through its InputID rather than by tag lookup, so the ASC
	 *  sees both the press and the release. Abilities can then wait on input release,
	 *  which is what the hold that converts a Light into a Heavy needs.
	 *
	 *  Mapped actions must use a Down trigger (or no trigger at all), never Pressed --
	 *  a Pressed trigger completes on the frame after the press, while the button is
	 *  still held, so the release edge would be lost.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input")
	TMap<TObjectPtr<UInputAction>, ETDAbilityInputID> AbilityInputActions;

	/** Starting and maximum health. Characters spawn at full. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Attributes", meta=(ClampMin="1.0"))
	float StartingMaxHealth = 100.0f;

	/** Starting and maximum stamina. Per the design this is 100 for everyone, for now. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Attributes", meta=(ClampMin="1.0"))
	float StartingMaxStamina = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UTDAttributeSet> AttributeSet;

private:

	/** Binds the actor info, seeds the attributes and grants DefaultAbilities. Safe to call twice. */
	void InitialiseAbilitySystem();

	void OnAbilityInputPressed(ETDAbilityInputID InputID);
	void OnAbilityInputReleased(ETDAbilityInputID InputID);

	bool bAbilitySystemInitialised = false;
};
