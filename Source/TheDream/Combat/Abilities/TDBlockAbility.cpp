// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDBlockAbility.h"
#include "Combat/TDCombatDebug.h"

UTDBlockAbility::UTDBlockAbility()
{
	// Every other ability in the project is LocalPredicted, and a guard has the strongest case of
	// any of them: it is a state the player holds rather than a discrete event, so waiting a round
	// trip to raise it would put the guard up after the attack it was raised against.
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

	TD_TIMING_LOG(TEXT("[%.3f] BLOCK      up on %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr));

	// No WaitInputRelease task. The character forwards both input edges to live instances already
	// -- that is what the attack ladder's hold-to-heavy conversion runs on -- so InputReleased
	// below is reached without an ability task, one fewer object per guard and one fewer thing
	// that can outlive the ability that made it.
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

	// bWasCancelled=false: the button coming up is the ability finishing the job it was given,
	// not something interrupting it. The distinction is not cosmetic -- EffectOnEnd is documented
	// as applying on cancellation too, so calling this a cancel would make being interrupted and
	// letting go indistinguishable to anything that later hangs off either.
	EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

void UTDBlockAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// **Logged here rather than in InputReleased, which is where it was and which hid a bug for a
	// day.** A guard ends five ways -- released, cancelled by a swing or a dodge, broken, or leaving
	// the ground -- and only the first goes through InputReleased. So the trace showed a guard going
	// up and never coming down, and a guard that survived its own guard break looked identical to
	// one that was correctly cancelled. EndAbility is where every exit converges, which is the same
	// argument that already puts the facing unlock here.
	//
	// bWasCancelled distinguishes letting go from being made to, which is the whole question when
	// reading a log back.
	//
	// **Guarded on IsActive, because UGameplayAbility::EndAbility silently no-ops otherwise** --
	// and logging before that check is what made a stuck guard unreadable: twenty "down" lines
	// after the last "up", every one of them a call that ended nothing. A trace that reports an
	// event which did not happen is worse than no trace, because it is evidence *against* the bug
	// that is actually present.
	if (IsActive())
	{
		TD_TIMING_LOG(TEXT("[%.3f] BLOCK      down on %s (%s)"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			*GetNameSafe(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr),
			bWasCancelled ? TEXT("cancelled") : TEXT("released"));
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
