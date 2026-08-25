// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDMeleeAttackAbility.h"
#include "TDGetUpAttackAbility.generated.h"

/**
 *  The attack thrown from the floor: knockdown's offensive exit. Legal only in the input window
 *  (bAllowedFromKnockdown), its activation is the rise (BringsOwnRiseMontage), and it plays one
 *  fixed swing -- no hold conversion, no chain, no string membership -- committed from the first
 *  frame and waived on a clean hit like every attack. The montage's Release Window drives the
 *  hitbox; the three authored phase durations drive the play rates, derived from where the window
 *  sits in the clip. Knockback is radial: each victim leaves along its own bearing from the riser.
 *
 *  GA_GetUpAttack's CDO is authoritative for every value here; the mechanics are in
 *  Docs/Combat-Spec.md under Stun & Knockdown, and the reasoning in Docs/Combat-Decisions.md.
 */
UCLASS()
class UTDGetUpAttackAbility : public UTDMeleeAttackAbility
{
	GENERATED_BODY()

public:
	UTDGetUpAttackAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual const TCHAR* GetKnockdownRiseLabel(const class ATDCombatCharacter* Character) const override { return TEXT("attack"); }
	virtual bool BringsOwnRiseMontage() const override { return true; }

protected:
	/** Seconds from the press to the release window opening. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Phases", meta=(ClampMin="0.01"))
	float WindupSeconds = 0.3f;

	/** Seconds the release window stays open. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Phases", meta=(ClampMin="0.01"))
	float ReleaseSeconds = 0.35f;

	/** Seconds from the window closing to the ability's end. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Phases", meta=(ClampMin="0.01"))
	float RecoverySeconds = 0.6f;

	/**
	 *  Montage position where the Release Window opens. Hand-copied from the notify; the drift
	 *  warning on RELEASE checks it, and the windup rate derives from it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Attack|Phases", meta=(ClampMin="0.0"))
	float ReleaseStartSeconds = 0.6333f;

	virtual bool UsesRadialKnockback() const override { return true; }

	/** The planned total minus what has elapsed: a catch pays for the rest of the commitment. */
	virtual float GetAttackParryLockoutSeconds() const override;

	virtual void ReleaseCommitmentTag() override;

private:
	/** Stretches the release window to ReleaseSeconds, after the drift check. */
	UFUNCTION()
	void HandleReleaseWindowBegan(FGameplayEventData Payload);

	/** Takes the release rate off and carries the tail through in RecoverySeconds. */
	UFUNCTION()
	void HandleReleaseWindowEnded(FGameplayEventData Payload);

	/** The authored ReleaseSeconds, so the trace task can close the window on time. */
	virtual float GetTraceWindowSeconds() const override { return ReleaseSeconds; }

	/** The trace task's closing edge. Routes to the same place the closing notify does. */
	virtual void HandleTraceWindowClosed() override;

	/** Takes the release rate off and starts recovery. Runs once per activation. */
	void CloseReleaseWindow();

	/** Adds or removes State.Attacking.Committed on the owner's ASC, once each way. */
	void SetCommitted(bool bCommitted);

	float ActivationWorldTime = 0.0f;
	bool bCommittedTagApplied = false;

	/** True once this activation's release window has closed, so the second edge is inert. */
	bool bReleaseWindowClosed = false;
};
