// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "TDAttributeSet.generated.h"

/** Generates the getter, setter, initter and FGameplayAttribute accessor for one attribute. */
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 *  Core combat attributes shared by every combatant.
 *
 *  Health and Stamina are always clamped to [0, Max]. Raising or lowering a Max
 *  attribute scales its current value to preserve the same ratio.
 */
UCLASS()
class UTDAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:

	UTDAttributeSet();

	//~ Begin UAttributeSet interface
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UAttributeSet interface

	/** Current health. Reaching 0 means dead. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Attributes", ReplicatedUsing=OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UTDAttributeSet, Health)

	/** Upper bound for Health. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Attributes", ReplicatedUsing=OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UTDAttributeSet, MaxHealth)

	/** Current stamina. Spent by defensive actions; reaching 0 causes Exhaustion. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Attributes", ReplicatedUsing=OnRep_Stamina)
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS(UTDAttributeSet, Stamina)

	/** Upper bound for Stamina. */
	UPROPERTY(BlueprintReadOnly, Category="Combat|Attributes", ReplicatedUsing=OnRep_MaxStamina)
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS(UTDAttributeSet, MaxStamina)

protected:

	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_Stamina(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	void OnRep_MaxStamina(const FGameplayAttributeData& OldValue);

private:

	/** Scales a current value when its Max counterpart changes, so the ratio is preserved. */
	void AdjustForMaxChange(const FGameplayAttributeData& AffectedAttribute, float OldMaxValue, float NewMaxValue, const FGameplayAttribute& AffectedAttributeProperty);
};
