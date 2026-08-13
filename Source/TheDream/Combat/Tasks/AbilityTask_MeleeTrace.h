// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Combat/TDAttackHitbox.h"
#include "AbilityTask_MeleeTrace.generated.h"

class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDMeleeTraceHitDelegate, const FHitResult&, Hit);

/**
 *  Resolves melee hits against authored volumes during the active frames of an attack.
 *
 *  The task lives for the whole ability but only tests between Event.Melee.WindowBegin and
 *  Event.Melee.WindowEnd. Each tick it evaluates every FTDAttackHitbox in the attacker's own
 *  frame -- see that struct for what a hitbox is and why it is authored rather than traced off
 *  the weapon. Every actor is reported at most once per window, so one swing cannot multi-hit.
 *
 *  **This replaced a swept capsule chain along the sword, and the sweeping went with it.** A
 *  blade is thin, so a point-in-time test could tunnel past a target between frames and the old
 *  trace swept previous-to-current to close that gap. An authored wedge is tens of cm deep and a
 *  target moves at most about 8 cm per frame at MaxWalkSpeed 500, so the gap it was closing no
 *  longer exists. What is left is one overlap and an exact filter per hitbox per tick.
 *
 *  **Facing must be stable while this runs.** The volume is defined in the attacker's yaw frame,
 *  so a character free to snap toward the camera mid-window would drag its own hitbox around
 *  with it. UTDChargedAttackAbility locks facing for the release window; without that lock this
 *  system is not merely imprecise, it is arbitrary.
 */
UCLASS()
class UAbilityTask_MeleeTrace : public UAbilityTask
{
	GENERATED_BODY()

public:

	UAbilityTask_MeleeTrace();

	/** Fires once per newly hit actor while the window is open. */
	UPROPERTY(BlueprintAssignable)
	FTDMeleeTraceHitDelegate OnHit;

	/**
	 *  Resolves melee hits against authored volumes during the montage's active frames.
	 *  @param InHitboxes      Volumes in the attacker's frame. Empty means this attack cannot hit.
	 *  @param bDrawDebug      Draw them. OR'd with the TD.DebugMeleeTrace cvar.
	 *  @param ExpectedMontage Only windows sent by this montage open the test. Null accepts any.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "true"))
	static UAbilityTask_MeleeTrace* MeleeTrace(UGameplayAbility* OwningAbility, const TArray<FTDAttackHitbox>& InHitboxes, bool bDrawDebug, const UAnimMontage* ExpectedMontage);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:

	void HandleWindowBegin(FGameplayTag Tag, const FGameplayEventData* Payload);
	void HandleWindowEnd(FGameplayTag Tag, const FGameplayEventData* Payload);

	/** Whether a window event came from the montage this attack is playing. */
	bool IsWindowForThisAttack(const FGameplayEventData* Payload) const;

	/** Tests every hitbox and broadcasts newly struck actors. Authority only. */
	void ResolveHits(UWorld* World, AActor* Avatar);

#if ENABLE_DRAW_DEBUG
	/** Outlines one hitbox: arcs at both radii across both ends of the vertical band. */
#endif

	/** The volumes this attack strikes with, in the attacker's frame. */
	TArray<FTDAttackHitbox> Hitboxes;

	bool bDrawDebugTrace = false;

	/**
	 *  Only window events carrying this montage open the test.
	 *
	 *  The events are broadcast to the whole ASC and carry no ownership of their own, so without
	 *  this any montage carrying the notify opens every listening trace. Null means accept any,
	 *  which is the pre-item-6 behaviour and is kept only so an ability may opt out deliberately.
	 */
	TWeakObjectPtr<const UAnimMontage> ExpectedMontage;

	/** True only between WindowBegin and WindowEnd. */
	bool bWindowOpen = false;

	/** Actors already reported during the current window, so one swing hits once. */
	TSet<TWeakObjectPtr<AActor>> ActorsHitThisWindow;

	FDelegateHandle WindowBeginHandle;
	FDelegateHandle WindowEndHandle;
};
