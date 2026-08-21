// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDBlockAbility.h"
#include "Combat/TDCombatCharacter.h"
#include "Combat/TDCombatDebug.h"

UTDBlockAbility::UTDBlockAbility()
{
	// LocalPredicted like every other ability, and a guard has the strongest case of any of them:
	// it is a state the player holds rather than a discrete event, so waiting a round trip to raise
	// it would put the guard up after the attack it was raised against.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

void UTDBlockAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// The base applies EffectOnStart and takes the movement lock if this ability wanted one --
	// block does not. It also runs the airborne and guard-broken refusals.
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Reset per activation: this instance is InstancedPerActor and therefore reused, so a release
	// remembered from a previous guard would end the next one instantly.
	bReleasePending = false;

	// Pushed from here rather than detected as an edge on the character, because a guard cancelled
	// and resumed inside one frame has no observable edge.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		Character->BeginBlockCommitment();

		// After the commitment rather than before, so a cost which exhausts the player cannot leave
		// a guard standing with no floor under it. Both run unconditionally once activation has
		// succeeded: by then the ability's cancel tags have already fired, which is what makes "you
		// can cancel an attack, but doing so exhausts you" true rather than a race.
		Character->PayBlockInitialCost();
	}

	TD_TIMING_LOG(TEXT("[%.3f] BLOCK      up on %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr));

	// No WaitInputRelease task: the character forwards both input edges to live instances already,
	// so InputReleased below is reached without an ability task.
}

void UTDBlockAbility::InputReleased(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	if (!bEndOnInputRelease)
	{
		return;
	}

	// **Held back, not dropped.** Inside the guard's minimum duration the button coming up does not
	// lower the guard -- the character finishes this when the commitment expires. Discarding the
	// release would force the player to keep holding through the entire minimum just to get a guard
	// that ends when they asked, which is the opposite of a floor.
	if (const ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		if (Character->IsBlockCommitted())
		{
			bReleasePending = true;
			return;
		}
	}

	// bWasCancelled=false: the button coming up is the ability finishing the job it was given, not
	// something interrupting it. EffectOnEnd applies on cancellation too, so calling this a cancel
	// would make being interrupted and letting go indistinguishable to anything hanging off either.
	EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

bool UTDBlockAbility::ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// Unconditional, and deliberately not "unless the refusal was the commit window". Every reason a
	// guard is refused -- already blocking, committed, exhausted, airborne, mid-dodge, guard broken
	// -- is a reason the *stale* press is no longer what the player is asking for. If they still
	// want a guard the button is still down and the resume brings it up.
	return false;
}

void UTDBlockAbility::FinishPendingRelease()
{
	if (!bReleasePending)
	{
		return;
	}

	bReleasePending = false;

	// bWasCancelled=false, matching the ordinary release path: the guard is ending because the
	// player asked it to, just later than they asked.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		/*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

void UTDBlockAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// **Logged here rather than in InputReleased.** A guard ends five ways -- released, cancelled by
	// a swing or a dodge, broken, or leaving the ground -- and only the first goes through
	// InputReleased, so the trace would show a guard going up and never coming down. EndAbility is
	// where every exit converges, the same argument that puts the facing unlock here.
	//
	// **Guarded on IsActive, because UGameplayAbility::EndAbility silently no-ops otherwise.**
	// Logging before that check reports events that did not happen -- twenty "down" lines after the
	// last "up", every one a call that ended nothing -- which is worse than no trace, because it is
	// evidence *against* the bug actually present.
	if (IsActive())
	{
		TD_TIMING_LOG(TEXT("[%.3f] BLOCK      down on %s (%s)"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
			bWasCancelled ? TEXT("cancelled") : TEXT("released"));
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
