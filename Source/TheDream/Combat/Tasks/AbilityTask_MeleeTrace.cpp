// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Tasks/AbilityTask_MeleeTrace.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif

namespace
{
	/**
	 *  The upright cylinder a hitbox is tested against. A Character's own capsule is the honest
	 *  answer and is what every combatant has. Anything else falls back to its bounds rather than
	 *  being skipped, so a future prop or destructible is hit rather than silently ignored.
	 */
	bool GetTargetCylinder(const AActor* Actor, FVector& OutCentre, float& OutRadiusCm, float& OutHalfHeightCm)
	{
		if (const ACharacter* Character = Cast<ACharacter>(Actor))
		{
			if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			{
				OutCentre = Capsule->GetComponentLocation();
				OutRadiusCm = Capsule->GetScaledCapsuleRadius();
				OutHalfHeightCm = Capsule->GetScaledCapsuleHalfHeight();
				return true;
			}
		}

		if (!Actor)
		{
			return false;
		}

		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		Actor->GetActorBounds(true, Origin, Extent);
		OutCentre = Origin;
		OutRadiusCm = FMath::Max(Extent.X, Extent.Y);
		OutHalfHeightCm = Extent.Z;
		return true;
	}
}

UAbilityTask_MeleeTrace::UAbilityTask_MeleeTrace()
{
	bTickingTask = true;
}

UAbilityTask_MeleeTrace* UAbilityTask_MeleeTrace::MeleeTrace(UGameplayAbility* OwningAbility, const TArray<FTDAttackHitbox>& InHitboxes, bool bDrawDebug, const UAnimMontage* InExpectedMontage)
{
	UAbilityTask_MeleeTrace* Task = NewAbilityTask<UAbilityTask_MeleeTrace>(OwningAbility);
	Task->Hitboxes = InHitboxes;
	Task->bDrawDebugTrace = bDrawDebug;
	Task->ExpectedMontage = InExpectedMontage;

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

	// The notify state on the montage drives when the hitboxes are live.
	WindowBeginHandle = ASC->AddGameplayEventTagContainerDelegate(
		FGameplayTagContainer(TDTags::Event_Melee_WindowBegin),
		FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &UAbilityTask_MeleeTrace::HandleWindowBegin));

	WindowEndHandle = ASC->AddGameplayEventTagContainerDelegate(
		FGameplayTagContainer(TDTags::Event_Melee_WindowEnd),
		FGameplayEventTagMulticastDelegate::FDelegate::CreateUObject(this, &UAbilityTask_MeleeTrace::HandleWindowEnd));
}

bool UAbilityTask_MeleeTrace::IsWindowForThisAttack(const FGameplayEventData* Payload) const
{
	// Null means accept any, which is the pre-Attack-Swap behaviour. Deliberately kept so an
	// ability can opt out, but it is not the default: the events reach the whole ASC.
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
		// Ungated warning, like the others on this category, because the failure it describes is
		// an attack that silently deals no damage rather than one that crashes. If this fires for
		// the montage an attack is actually playing, the notify and the ability disagree about
		// which asset is which and no hitbox will ever go live.
		UE_LOG(LogTDCombatTiming, Warning, TEXT("MeleeTrace: ignoring a Release Window from '%s'; this attack is testing for '%s'."),
			(Payload && Payload->OptionalObject) ? *Payload->OptionalObject->GetName() : TEXT("<none>"),
			ExpectedMontage.IsValid() ? *ExpectedMontage->GetName() : TEXT("<none>"));
		return;
	}

	bWindowOpen = true;

	// A fresh window means a fresh swing, so previously hit actors are hittable again.
	ActorsHitThisWindow.Reset();
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
}

void UAbilityTask_MeleeTrace::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);

	UWorld* World = GetWorld();
	const FGameplayAbilityActorInfo* ActorInfo = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!World || !Avatar)
	{
		return;
	}

#if ENABLE_DRAW_DEBUG
	// Deliberately outside the authority gate and outside the window check. Drawing is local, and
	// a volume that is only visible while it is resolving hits can only be judged in hindsight --
	// the editor has no hitbox preview, so this is the whole authoring affordance.
	//
	// How much of an attack this covers depends on when the task started, which differs by path:
	// UTDMeleeAttackAbility starts it at activation and so draws the entire windup, while
	// UTDChargedAttackAbility cannot start it until the branch is chosen and so draws only the
	// commit-to-release run-up and then the recovery. The recovery half is the useful one -- the
	// wedge sits still, in place, for as long as it takes to look at.
	if (bDrawDebugTrace || TDShouldDrawMeleeTrace())
	{
		const FVector Location = Avatar->GetActorLocation();
		const float Yaw = Avatar->GetActorRotation().Yaw;
		const FColor Color = bWindowOpen ? FColor::Red : FColor(60, 60, 60);

		for (const FTDAttackHitbox& Hitbox : Hitboxes)
		{
			Hitbox.DrawDebug(World, Location, Yaw, Color);
		}
	}
#endif

	if (!bWindowOpen)
	{
		return;
	}

	// Hit detection is the server's alone. Abilities are LocalPredicted, so without this the test
	// also runs on the owning client -- from a *different* position, the two machines being a round
	// trip apart in the swing. That client result can never apply damage (UTDMeleeAttackAbility
	// gates on authority), so at best it is wasted work and at worst a second opinion about what
	// was hit that nothing reconciles. Prediction, when it arrives, does not change this: what a
	// client predicts is its *own* action, never whether it connected with someone else.
	if (!Avatar->HasAuthority())
	{
		return;
	}

	ResolveHits(World, Avatar);
}

void UAbilityTask_MeleeTrace::ResolveHits(UWorld* World, AActor* Avatar)
{
	const FVector AttackerLocation = Avatar->GetActorLocation();
	const float AttackerYaw = Avatar->GetActorRotation().Yaw;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(TDMeleeHitbox), false, Avatar);
	QueryParams.AddIgnoredActor(Avatar);

	for (const FTDAttackHitbox& Hitbox : Hitboxes)
	{
		// Broad phase. One sphere per hitbox, sized so it cannot miss anything the exact filter
		// would accept; the filter below is what actually decides.
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByChannel(
			Overlaps,
			AttackerLocation,
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeSphere(Hitbox.GetBroadPhaseRadiusCm()),
			QueryParams);

		for (const FOverlapResult& Overlap : Overlaps)
		{
			AActor* Candidate = Overlap.GetActor();
			if (!Candidate || ActorsHitThisWindow.Contains(Candidate))
			{
				continue;
			}

			FVector TargetCentre = FVector::ZeroVector;
			float TargetRadius = 0.0f;
			float TargetHalfHeight = 0.0f;
			if (!GetTargetCylinder(Candidate, TargetCentre, TargetRadius, TargetHalfHeight))
			{
				continue;
			}

			if (!Hitbox.OverlapsCapsule(AttackerLocation, AttackerYaw, TargetCentre, TargetRadius, TargetHalfHeight))
			{
				continue;
			}

			// One entry covers every axis at once: the same actor cannot be reported twice by two
			// hitboxes in a tick, nor by the same hitbox on the next tick, nor by two overlap
			// results resolving to one actor. It resets only when a new window opens.
			ActorsHitThisWindow.Add(Candidate);

			// Synthesised rather than reported by a sweep, because there is no sweep any more. The
			// point on the target's body nearest the attacker is what a hit effect would want, and
			// it is clamped into the band so it does not sit above or below the volume that struck.
			FVector ToTarget = TargetCentre - AttackerLocation;
			ToTarget.Z = 0.0f;
			const FVector Direction = ToTarget.IsNearlyZero() ? Avatar->GetActorForwardVector() : ToTarget.GetSafeNormal();

			FVector ImpactPoint = TargetCentre - Direction * TargetRadius;
			ImpactPoint.Z = FMath::Clamp(
				ImpactPoint.Z,
				AttackerLocation.Z + Hitbox.HeightMinCm,
				AttackerLocation.Z + Hitbox.HeightMaxCm);

			const FHitResult Hit(Candidate, Overlap.GetComponent(), ImpactPoint, -Direction);

			if (ShouldBroadcastAbilityTaskDelegates())
			{
				OnHit.Broadcast(Hit);
			}
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
