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
 *  The ASC lives here rather than on the pawn because a PlayerState outlives its pawn. That is
 *  the conventional shape for PvP, and it is what survives if respawn ever means a fresh pawn
 *  rather than the debug revive it is today. Keeping it on the character was defensible on
 *  current behaviour and was rejected because the destination is known.
 *
 *  **Only players have one.** The training dummy is AI-possessed under a stock AAIController
 *  and has no PlayerState at all, so ATDCombatCharacter *resolves* which ASC it uses rather
 *  than assuming this one exists. Reasoning in Docs/Combat-Decisions.md, 2026-08-11.
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
	 *  **The flag lives with the ASC, not with the character, and that is load-bearing.** A
	 *  single bool on the character cannot express this correctly: a player pawn's BeginPlay
	 *  runs *before* it is possessed, so it would seed the character's fallback ASC, set the
	 *  flag, and then swap to this one -- leaving the player with no attributes and no
	 *  abilities. The never-possessed training dummy would work perfectly throughout, which is
	 *  what would have made it hard to find.
	 *
	 *  It is also what stops a respawned pawn re-granting every ability and re-stacking every
	 *  default effect onto a PlayerState that already carries them.
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
