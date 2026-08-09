// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AbilityTask_MeleeTrace.generated.h"

class USkeletalMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDMeleeTraceHitDelegate, const FHitResult&, Hit);

/**
 *  Sweeps for melee hits during the active frames of an attack.
 *
 *  The task lives for the whole ability but only traces between Event.Melee.WindowBegin
 *  and Event.Melee.WindowEnd. Each tick it sweeps a sphere from the socket's previous
 *  position to its current one, so a fast swing cannot tunnel past a target between
 *  frames the way a single point-in-time trace can. Every actor is reported at most
 *  once per window, so one swing cannot multi-hit.
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
	 *  Traces for melee hits during the montage's active frames.
	 *  @param MeshComponent  Mesh owning the trace socket, normally the character mesh.
	 *  @param SocketName     Socket swept each tick, e.g. hand_r or a weapon socket.
	 *  @param Radius         Sphere radius in cm. Effectively the attack's hit thickness.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "true"))
	static UAbilityTask_MeleeTrace* MeleeTrace(UGameplayAbility* OwningAbility, USkeletalMeshComponent* MeshComponent, FName SocketName, float Radius, bool bDrawDebug);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:

	void HandleWindowBegin(FGameplayTag Tag, const FGameplayEventData* Payload);
	void HandleWindowEnd(FGameplayTag Tag, const FGameplayEventData* Payload);

	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;

	FName TraceSocket = NAME_None;
	float TraceRadius = 0.0f;
	bool bDrawDebugTrace = false;

	/** True only between WindowBegin and WindowEnd. */
	bool bWindowOpen = false;

	FVector PreviousSocketLocation = FVector::ZeroVector;
	bool bHasPreviousLocation = false;

	/** Actors already reported during the current window, so one swing hits once. */
	TSet<TWeakObjectPtr<AActor>> ActorsHitThisWindow;

	FDelegateHandle WindowBeginHandle;
	FDelegateHandle WindowEndHandle;
};
