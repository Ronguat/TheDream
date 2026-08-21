// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/TDPlayerState.h"
#include "Combat/Attributes/TDAttributeSet.h"
#include "AbilitySystemComponent.h"

ATDPlayerState::ATDPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed: full effect replication to the owning client, minimal to everyone else.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UTDAttributeSet>(TEXT("AttributeSet"));

	// APlayerState's own constructor sets this to 1 Hz, which suits a scoreboard and is catastrophic
	// for an ASC. The attack ladder resolves in 250 ms, so at 1 Hz attributes, tags and ability
	// state would arrive up to a second late -- and it would read as a gameplay bug (attacks that
	// do nothing, stamina that jumps) rather than as a net setting. 100 Hz is AActor's own default,
	// so this restores the normal rate rather than choosing a special one; the actual send rate is
	// still bounded by the connection's tick.
	SetNetUpdateFrequency(100.0f);
}

UAbilitySystemComponent* ATDPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
