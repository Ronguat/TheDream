// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Tasks/AbilityTask_MeleeTrace.h"
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

UAbilityTask_MeleeTrace* UAbilityTask_MeleeTrace::MeleeTrace(UGameplayAbility* OwningAbility, USkeletalMeshComponent* MeshComponent, FName SocketName, float Radius, bool bDrawDebug)
{
	UAbilityTask_MeleeTrace* Task = NewAbilityTask<UAbilityTask_MeleeTrace>(OwningAbility);
	Task->Mesh = MeshComponent;
	Task->TraceSocket = SocketName;
	Task->TraceRadius = Radius;
	Task->bDrawDebugTrace = bDrawDebug;

	return Task;
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

void UAbilityTask_MeleeTrace::HandleWindowBegin(FGameplayTag Tag, const FGameplayEventData* Payload)
{
	bWindowOpen = true;

	// A fresh window means a fresh swing, so previously hit actors are hittable again.
	ActorsHitThisWindow.Reset();
	bHasPreviousLocation = false;
}

void UAbilityTask_MeleeTrace::HandleWindowEnd(FGameplayTag Tag, const FGameplayEventData* Payload)
{
	bWindowOpen = false;
	bHasPreviousLocation = false;
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

	const FVector CurrentLocation = Mesh->GetSocketLocation(TraceSocket);

	// Sweeping from the previous frame's position closes the gap a fast swing would
	// otherwise skip over. The first tick of a window has no previous position yet.
	const FVector StartLocation = bHasPreviousLocation ? PreviousSocketLocation : CurrentLocation;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDMeleeTrace), false, Avatar);
	QueryParams.AddIgnoredActor(Avatar);

	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(
		Hits,
		StartLocation,
		CurrentLocation,
		FQuat::Identity,
		ECC_Pawn,
		FCollisionShape::MakeSphere(TraceRadius),
		QueryParams);

#if ENABLE_DRAW_DEBUG
	if (bDrawDebugTrace)
	{
		DrawDebugSphere(World, CurrentLocation, TraceRadius, 12, FColor::Red, false, 1.0f);
		DrawDebugLine(World, StartLocation, CurrentLocation, FColor::Red, false, 1.0f);
	}
#endif

	PreviousSocketLocation = CurrentLocation;
	bHasPreviousLocation = true;

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
