// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDCombatCharacter.h"
#include "Combat/Attributes/TDAttributeSet.h"
#include "AbilitySystemComponent.h"

ATDCombatCharacter::ATDCombatCharacter()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed: full effect replication to the owning client, minimal to everyone else.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UTDAttributeSet>(TEXT("AttributeSet"));
}

UAbilitySystemComponent* ATDCombatCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ATDCombatCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Covers characters that are never possessed, such as a placed training dummy.
	InitialiseAbilitySystem();
}

void ATDCombatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// A possessed pawn needs the actor info rebound to its new owner.
	bAbilitySystemInitialised = false;
	InitialiseAbilitySystem();
}

void ATDCombatCharacter::InitialiseAbilitySystem()
{
	if (bAbilitySystemInitialised || !AbilitySystemComponent)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	bAbilitySystemInitialised = true;

	// Attributes and abilities are authority-only state; clients receive them by replication.
	if (!HasAuthority())
	{
		return;
	}

	AbilitySystemComponent->SetNumericAttributeBase(UTDAttributeSet::GetMaxHealthAttribute(), StartingMaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), StartingMaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(UTDAttributeSet::GetMaxStaminaAttribute(), StartingMaxStamina);
	AbilitySystemComponent->SetNumericAttributeBase(UTDAttributeSet::GetStaminaAttribute(), StartingMaxStamina);

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
	{
		if (AbilityClass)
		{
			AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	}
}

float ATDCombatCharacter::GetHealth() const
{
	return AttributeSet ? AttributeSet->GetHealth() : 0.0f;
}

float ATDCombatCharacter::GetMaxHealth() const
{
	return AttributeSet ? AttributeSet->GetMaxHealth() : 0.0f;
}

float ATDCombatCharacter::GetStamina() const
{
	return AttributeSet ? AttributeSet->GetStamina() : 0.0f;
}

float ATDCombatCharacter::GetMaxStamina() const
{
	return AttributeSet ? AttributeSet->GetMaxStamina() : 0.0f;
}

float ATDCombatCharacter::GetHealthPercent() const
{
	const float Max = GetMaxHealth();
	return (Max > 0.0f) ? GetHealth() / Max : 0.0f;
}

float ATDCombatCharacter::GetStaminaPercent() const
{
	const float Max = GetMaxStamina();
	return (Max > 0.0f) ? GetStamina() / Max : 0.0f;
}
