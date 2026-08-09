// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDMeleeAttackAbility.generated.h"

class UAnimMontage;
class UGameplayEffect;

/**
 *  A single melee swing: play a montage, trace during its active frames, apply damage.
 *
 *  Light, Heavy and Charged Heavy are all expected to be Blueprint subclasses of this
 *  that differ only in their tuning values and montage, so the swing logic itself lives
 *  in one place. Everything a designer needs to change is exposed below.
 */
UCLASS(abstract)
class UTDMeleeAttackAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:

	/** Montage to play. Its Melee Window notify state defines the active frames. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Animation")
	TObjectPtr<UAnimMontage> AttackMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Animation", meta=(ClampMin="0.1"))
	float MontagePlayRate = 1.0f;

	/** Applied to each actor hit. Expects a Data.Damage SetByCaller magnitude on Health. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** Health removed per hit. Positive here; applied as a negative magnitude. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Damage", meta=(ClampMin="0.0"))
	float Damage = 15.0f;

	/** Socket swept for hits. hand_r suits unarmed; a weapon socket replaces it later. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace")
	FName TraceSocket = FName("hand_r");

	/** Sphere radius in cm. This is the attack's effective thickness, so it drives spacing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace", meta=(ClampMin="1.0"))
	float TraceRadius = 45.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Trace")
	bool bDrawDebugTrace = false;

private:

	UFUNCTION()
	void HandleTraceHit(const FHitResult& Hit);

	UFUNCTION()
	void HandleMontageFinished();

	UFUNCTION()
	void HandleMontageInterrupted();
};
