// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDMeleeAttackAbility.generated.h"

class UAnimMontage;
class UAbilityTask_MeleeTrace;
class UGameplayEffect;
class USkeletalMeshComponent;

/**
 *  A single melee swing: play a montage, trace during its active frames, apply damage.
 *
 *  The damage and trace values are virtual so a subclass can vary them per swing --
 *  UTDChargedAttackAbility picks them from whichever branch the player's hold selected.
 */
UCLASS(abstract)
class UTDMeleeAttackAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

protected:

	/** Montage to play. Its Melee Window notify states define the active frames. */
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

	/** Damage for the swing currently being thrown. Overridden when a swing has variants. */
	virtual float GetAttackDamage() const { return Damage; }

	/** Trace radius for the swing currently being thrown. Longer attacks reach further. */
	virtual float GetAttackTraceRadius() const { return TraceRadius; }

	/** The mesh carrying TraceSocket, or null if the avatar has none. */
	USkeletalMeshComponent* FindAvatarMesh() const;

	/** Starts tracing. The task idles until a Melee Window notify opens on the montage. */
	UAbilityTask_MeleeTrace* StartMeleeTrace(float Radius);

	/** Plays AttackMontage, optionally from a named section, and ends the ability when it finishes. */
	bool StartAttackMontage(FName StartSection);

	UFUNCTION()
	void HandleTraceHit(const FHitResult& Hit);

	UFUNCTION()
	void HandleMontageFinished();

	UFUNCTION()
	void HandleMontageInterrupted();
};
