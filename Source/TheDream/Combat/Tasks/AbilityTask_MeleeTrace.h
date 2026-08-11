// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AbilityTask_MeleeTrace.generated.h"

class USkeletalMeshComponent;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDMeleeTraceHitDelegate, const FHitResult&, Hit);

/**
 *  Sweeps for melee hits along the blade during the active frames of an attack.
 *
 *  The task lives for the whole ability but only traces between Event.Melee.WindowBegin
 *  and Event.Melee.WindowEnd. Each tick it samples the blade at BladeSegments points and
 *  sweeps a sphere from each point's previous position to its current one, so a fast swing
 *  cannot tunnel past a target between frames the way a single point-in-time trace can.
 *  Every actor is reported at most once per window, so one swing cannot multi-hit.
 *
 *  **The blade is authored, never measured from the weapon mesh.** The `Sword` socket lives on
 *  the skeleton and exists whether or not a prop hangs off it, so deriving length from the mesh
 *  would give an unarmed character a well-formed zero-length trace -- a hitbox that misses
 *  everything, with nothing null and nothing logged. The training dummy is exactly that case.
 *
 *  TraceRadius is the blade's *thickness*, not its reach. Reach comes from BladeLengthCm.
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
	 *  Traces for melee hits along the blade during the montage's active frames.
	 *  @param MeshComponent   Mesh owning the weapon socket, normally the character mesh.
	 *  @param SocketName      Socket the blade hangs off, e.g. the pack's `Sword` socket.
	 *  @param BladeAxisLocal  Socket-space direction the blade points. Normalised internally.
	 *  @param BladeStartCm    Distance from the socket to the blade's base, along that axis.
	 *  @param BladeLengthCm   Base-to-tip length. Authored, never measured from the weapon mesh.
	 *  @param Segments        Sample points along the blade. 1 degenerates to the old socket trace.
	 *  @param Radius          Sphere radius in cm -- the blade's *thickness*, not its reach.
	 *  @param ExpectedMontage Only windows sent by this montage open the trace. Null accepts any.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "true"))
	static UAbilityTask_MeleeTrace* MeleeTrace(UGameplayAbility* OwningAbility, USkeletalMeshComponent* MeshComponent, FName SocketName, FVector BladeAxisLocal, float BladeStartCm, float BladeLengthCm, int32 Segments, float Radius, bool bDrawDebug, const UAnimMontage* ExpectedMontage);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:

	void HandleWindowBegin(FGameplayTag Tag, const FGameplayEventData* Payload);
	void HandleWindowEnd(FGameplayTag Tag, const FGameplayEventData* Payload);

	/** Whether a window event came from the montage this attack is playing. */
	bool IsWindowForThisAttack(const FGameplayEventData* Payload) const;

	/** World-space sample points from blade base to tip, for this frame. */
	void BuildBladePoints(TArray<FVector>& OutPoints) const;

	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;

	FName TraceSocket = NAME_None;
	FVector BladeAxis = FVector::ForwardVector;
	float BladeStart = 0.0f;
	float BladeLength = 0.0f;
	int32 BladeSegments = 1;
	float TraceRadius = 0.0f;
	bool bDrawDebugTrace = false;

	/**
	 *  Only window events carrying this montage open the trace.
	 *
	 *  The events are broadcast to the whole ASC and carry no ownership of their own, so without
	 *  this any montage carrying the notify opens every listening trace. Null means accept any,
	 *  which is the pre-item-6 behaviour and is kept only so an ability may opt out deliberately.
	 */
	TWeakObjectPtr<const UAnimMontage> ExpectedMontage;

	/** True only between WindowBegin and WindowEnd. */
	bool bWindowOpen = false;

	/** Blade sample points from the previous tick, in world space. Empty on the window's first tick. */
	TArray<FVector> PreviousBladePoints;

	/** Actors already reported during the current window, so one swing hits once. */
	TSet<TWeakObjectPtr<AActor>> ActorsHitThisWindow;

	FDelegateHandle WindowBeginHandle;
	FDelegateHandle WindowEndHandle;
};
