// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Tasks/AbilityTask_MeleeTrace.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif

UAbilityTask_MeleeTrace::UAbilityTask_MeleeTrace()
{
	bTickingTask = true;
}

UAbilityTask_MeleeTrace* UAbilityTask_MeleeTrace::MeleeTrace(UGameplayAbility* OwningAbility, USkeletalMeshComponent* MeshComponent, FName SocketName, FVector BladeAxisLocal, float BladeStartCm, float BladeLengthCm, int32 Segments, float Radius, bool bDrawDebug, const UAnimMontage* InExpectedMontage)
{
	UAbilityTask_MeleeTrace* Task = NewAbilityTask<UAbilityTask_MeleeTrace>(OwningAbility);
	Task->Mesh = MeshComponent;
	Task->TraceSocket = SocketName;

	// A zero axis would collapse every sample onto the socket and silently restore the old
	// single-point trace, so it falls back rather than producing a degenerate blade.
	Task->BladeAxis = BladeAxisLocal.IsNearlyZero() ? FVector::ForwardVector : BladeAxisLocal.GetSafeNormal();
	Task->BladeStart = BladeStartCm;
	Task->BladeLength = FMath::Max(0.0f, BladeLengthCm);
	Task->BladeSegments = FMath::Max(1, Segments);
	Task->TraceRadius = Radius;
	Task->bDrawDebugTrace = bDrawDebug;
	Task->ExpectedMontage = InExpectedMontage;

	return Task;
}

void UAbilityTask_MeleeTrace::BuildBladePoints(TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	if (!Mesh.IsValid())
	{
		return;
	}

	const FTransform SocketTransform = Mesh->GetSocketTransform(TraceSocket);
	const FVector AxisWorld = SocketTransform.TransformVectorNoScale(BladeAxis);
	const FVector Base = SocketTransform.GetLocation() + AxisWorld * BladeStart;

	// One segment means one point at the base, which is the old socket-only behaviour and the
	// sane degenerate case for a weapon with no authored length.
	if (BladeSegments <= 1 || BladeLength <= 0.0f)
	{
		OutPoints.Add(Base);
		return;
	}

	OutPoints.Reserve(BladeSegments);
	for (int32 Index = 0; Index < BladeSegments; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(BladeSegments - 1);
		OutPoints.Add(Base + AxisWorld * (BladeLength * Alpha));
	}
}

void UAbilityTask_MeleeTrace::Activate()
{
	Super::Activate();

	const FGameplayAbilityActorInfo* ActorInfo = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC)
	{
		EndTask();
		return;
	}

	// The notify state on the montage drives when tracing is live.
	WindowBeginHandle = ASC->AddGameplayEventTagContainerDelegate(
		FGameplayTagContainer(TDTags::Event_Melee_WindowBegin),
		FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &UAbilityTask_MeleeTrace::HandleWindowBegin));

	WindowEndHandle = ASC->AddGameplayEventTagContainerDelegate(
		FGameplayTagContainer(TDTags::Event_Melee_WindowEnd),
		FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &UAbilityTask_MeleeTrace::HandleWindowEnd));
}

bool UAbilityTask_MeleeTrace::IsWindowForThisAttack(const FGameplayEventData* Payload) const
{
	// Null means accept any, which is the pre-item-6 behaviour. Deliberately kept so an ability
	// can opt out, but it is not the default: the events reach the whole ASC.
	if (!ExpectedMontage.IsValid())
	{
		return true;
	}

	return Payload && Payload->OptionalObject == ExpectedMontage.Get();
}

void UAbilityTask_MeleeTrace::HandleWindowBegin(FGameplayTag Tag, const FGameplayEventData* Payload)
{
	if (!IsWindowForThisAttack(Payload))
	{
		// Ungated warning, like the other two in this system, because the failure it describes is
		// an attack that silently deals no damage rather than one that crashes. If this fires for
		// the montage an attack is actually playing, the notify and the ability disagree about
		// which asset is which and no trace will ever open.
		UE_LOG(LogTemp, Warning, TEXT("MeleeTrace: ignoring a Release Window from '%s'; this attack is tracing for '%s'."),
			(Payload && Payload->OptionalObject) ? *Payload->OptionalObject->GetName() : TEXT("<none>"),
			ExpectedMontage.IsValid() ? *ExpectedMontage->GetName() : TEXT("<none>"));
		return;
	}

	bWindowOpen = true;

	// A fresh window means a fresh swing, so previously hit actors are hittable again.
	ActorsHitThisWindow.Reset();
	PreviousBladePoints.Reset();
}

void UAbilityTask_MeleeTrace::HandleWindowEnd(FGameplayTag Tag, const FGameplayEventData* Payload)
{
	// Filtered on the way out too. A foreign montage's window ending must not close ours, which
	// would truncate an active swing rather than merely failing to start one.
	if (!IsWindowForThisAttack(Payload))
	{
		return;
	}

	bWindowOpen = false;
	PreviousBladePoints.Reset();
}

void UAbilityTask_MeleeTrace::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);

	if (!bWindowOpen || !Mesh.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	const FGameplayAbilityActorInfo* ActorInfo = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!World || !Avatar)
	{
		return;
	}

	// Hit detection is the server's alone. Abilities are LocalPredicted, so without this the
	// sweep also runs on the owning client -- from a *different* socket position, because the
	// two machines are a round trip apart in the swing. That client result can never apply
	// damage (UTDMeleeAttackAbility gates on authority), so at best it is wasted work, and at
	// worst it is a second opinion about what was hit that nothing reconciles.
	//
	// Prediction, when it arrives, does not change this: what a client predicts is its *own*
	// action, never whether that action connected with someone else.
	if (!Avatar->HasAuthority())
	{
		return;
	}

	TArray<FVector> CurrentPoints;
	BuildBladePoints(CurrentPoints);
	if (CurrentPoints.Num() == 0)
	{
		return;
	}

	// The blade is sampled at BladeSegments points and each is swept from where it was last
	// frame. Two gaps get closed by this: along the blade (a target between base and tip) and
	// through time (a fast swing passing a target entirely between frames). The first tick of a
	// window has no previous positions, so each point sweeps against itself.
	const bool bHasPrevious = PreviousBladePoints.Num() == CurrentPoints.Num();

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDMeleeTrace), false, Avatar);
	QueryParams.AddIgnoredActor(Avatar);

	// Either the ability's own flag or the console toggle. The cvar exists so the question
	// "where is this blade actually sweeping" costs a keypress instead of an editor restart.
	const bool bDraw = bDrawDebugTrace || TDShouldDrawMeleeTrace();

	// A capsule per blade segment rather than a sphere per sample point. Spheres coupled
	// TraceRadius to BladeTraceSegments -- drop the radius below half the sample spacing and
	// the blade grows holes between its own samples -- so thickness could not be tuned without
	// also tuning density. A capsule spans the segment by construction, which makes the radius
	// mean thickness and nothing else.
	//
	// Deliberately *not* line traces, though the blade is geometrically a line. Exact hit
	// detection rewards precision only where the player has precise control, and here an attack
	// is a canned animation that cannot be aimed -- not even by looking up or down. A near miss
	// would then be something the player had no means to avoid, which reads as the game being
	// finicky rather than as their own imprecision. What reads as *precise* under this control
	// scheme is landing in the same place at the same range every time, so thickness is what
	// delivers the spacing goal rather than compromising it.
	for (int32 Index = 0; Index + 1 < CurrentPoints.Num(); ++Index)
	{
		const FVector CurrentA = CurrentPoints[Index];
		const FVector CurrentB = CurrentPoints[Index + 1];
		const FVector PreviousA = bHasPrevious ? PreviousBladePoints[Index] : CurrentA;
		const FVector PreviousB = bHasPrevious ? PreviousBladePoints[Index + 1] : CurrentB;

		const FVector Segment = CurrentB - CurrentA;
		const float SegmentLength = Segment.Size();
		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		// UE measures a capsule's half height including its hemispherical caps, so the
		// cylindrical part only spans the segment once the radius is added back.
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(TraceRadius, SegmentLength * 0.5f + TraceRadius);
		const FQuat Orientation = FQuat::FindBetweenNormals(FVector::UpVector, Segment / SegmentLength);

		// Each segment sweeps from where it was last frame to where it is now, so a swing that
		// rotates fast is covered by its own arc rather than by one capsule's translation.
		const FVector StartLocation = (PreviousA + PreviousB) * 0.5f;
		const FVector CurrentLocation = (CurrentA + CurrentB) * 0.5f;

		TArray<FHitResult> Hits;
		World->SweepMultiByChannel(
			Hits,
			StartLocation,
			CurrentLocation,
			Orientation,
			ECC_Pawn,
			Capsule,
			QueryParams);

#if ENABLE_DRAW_DEBUG
		if (bDraw)
		{
			DrawDebugCapsule(World, CurrentLocation, SegmentLength * 0.5f + TraceRadius, TraceRadius,
				Orientation, FColor::Red, false, 1.0f);
			DrawDebugLine(World, StartLocation, CurrentLocation, FColor::Silver, false, 1.0f);
		}
#endif

		for (const FHitResult& Hit : Hits)
		{
			AActor* HitActor = Hit.GetActor();
			if (!HitActor || ActorsHitThisWindow.Contains(HitActor))
			{
				continue;
			}

			ActorsHitThisWindow.Add(HitActor);

			if (ShouldBroadcastAbilityTaskDelegates())
			{
				OnHit.Broadcast(Hit);
			}
		}
	}

#if ENABLE_DRAW_DEBUG
	// The blade itself, so its authored length can be judged against the weapon on screen --
	// which is the only way to tell BladeLengthCm is wrong, since nothing else reports it.
	if (bDraw && CurrentPoints.Num() > 1)
	{
		DrawDebugLine(World, CurrentPoints[0], CurrentPoints.Last(), FColor::Yellow, false, 1.0f, 0, 1.5f);
	}
#endif

	PreviousBladePoints = MoveTemp(CurrentPoints);
}

void UAbilityTask_MeleeTrace::OnDestroy(bool bInOwnerFinished)
{
	const FGameplayAbilityActorInfo* ActorInfo = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	if (UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
	{
		ASC->RemoveGameplayEventTagContainerDelegate(FGameplayTagContainer(TDTags::Event_Melee_WindowBegin), WindowBeginHandle);
		ASC->RemoveGameplayEventTagContainerDelegate(FGameplayTagContainer(TDTags::Event_Melee_WindowEnd), WindowEndHandle);
	}

	Super::OnDestroy(bInOwnerFinished);
}
