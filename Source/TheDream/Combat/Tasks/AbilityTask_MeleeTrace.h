// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Combat/TDAttackHitbox.h"
#include "AbilityTask_MeleeTrace.generated.h"

class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDMeleeTraceHitDelegate, const FHitResult&, Hit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTDMeleeTraceWindowClosedDelegate);

/**
 *  Resolves melee hits against authored volumes during the active frames of an attack.
 *
 *  The task lives for the whole ability but only tests between Event.Melee.WindowBegin and
 *  Event.Melee.WindowEnd. Each tick it evaluates every FTDAttackHitbox in the attacker's own
 *  frame -- see that struct for what a hitbox is and why it is authored rather than traced off
 *  the weapon. Every actor is reported at most once per window, so one swing cannot multi-hit.
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
	 *  Fires once when the window closes, whichever edge closed it -- the elapsed deadline or the
	 *  notify. Listeners must be idempotent: the notify's own end still arrives afterwards when
	 *  the deadline won, and is suppressed here rather than at the receiver.
	 */
	UPROPERTY(BlueprintAssignable)
	FTDMeleeTraceWindowClosedDelegate OnWindowClosed;

	/**
	 *  Resolves melee hits against authored volumes during the montage's active frames.
	 *  @param InHitboxes      Volumes in the attacker's frame. Empty means this attack cannot hit.
	 *  @param bDrawDebug      Draw them. OR'd with the TD.DebugMeleeTrace cvar.
	 *  @param ExpectedMontage Only windows sent by this montage open the test. Null accepts any.
	 *  @param InWindowSeconds Authored duration the window stays open for, measured from the
	 *                         opening notify. Zero leaves the notify's own end governing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "true"))
	static UAbilityTask_MeleeTrace* MeleeTrace(UGameplayAbility* OwningAbility, const TArray<FTDAttackHitbox>& InHitboxes, bool bDrawDebug, const UAnimMontage* ExpectedMontage, float InWindowSeconds);

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

	/** The volumes this attack strikes with, in the attacker's frame. */
	TArray<FTDAttackHitbox> Hitboxes;

	bool bDrawDebugTrace = false;

	/**
	 *  Only window events carrying this montage open the test. The events are broadcast to the whole
	 *  ASC and carry no ownership, so without this any montage carrying the notify opens every
	 *  listening trace. Null means accept any.
	 */
	TWeakObjectPtr<const UAnimMontage> ExpectedMontage;

	/** Closes the window if it is open, and broadcasts OnWindowClosed exactly once. */
	void CloseWindow();

	/**
	 *  Authored seconds the window stays open, from the opening notify. Zero disables the
	 *  deadline and leaves the closing notify governing.
	 */
	float WindowSeconds = 0.0f;

	/** World time the open window is due to close. Only meaningful while bWindowOpen. */
	float WindowEndsAt = 0.0f;

	/** True only between WindowBegin and whichever edge closes the window. */
	bool bWindowOpen = false;

	/** Actors already reported during the current window, so one swing hits once. */
	TSet<TWeakObjectPtr<AActor>> ActorsHitThisWindow;

	FDelegateHandle WindowBeginHandle;
	FDelegateHandle WindowEndHandle;
};
