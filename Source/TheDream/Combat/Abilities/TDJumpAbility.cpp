// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDJumpAbility.h"
#include "Combat/TDCombatCharacter.h"
#include "Combat/TDCombatDebug.h"
#include "GameFramework/Character.h"

UTDJumpAbility::UTDJumpAbility()
{
	// LocalPredicted like every other ability in the project. Jump is the one where prediction is
	// least negotiable: an unpredicted jump would launch a round trip after the press, and the
	// character movement component already predicts and reconciles the launch itself.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// Refused while an ability owns movement input -- the fifth of the five rules Jump() used to
	// restate, and the only one with no tag to express it. Set here rather than left to the CDO
	// because a GA_Jump without it is a jump that walks out of a committed swing, which is a silent
	// design regression rather than a mis-tuned value.
	bBlockedWhileMovementLocked = true;

	// **Behaviour-preserving, not a new rule.** JumpMaxCount is 1, so the movement component
	// already refuses a second launch while falling; what this adds is that the refusal is
	// *traced* instead of being a silent no-op inside CMC. The same reasoning the dodge and the
	// parry use, which both take this flag: a jump is a claim about the ground you are standing on.
	bBlockedWhileAirborne = true;

	// The neutral stand: the jump input is knockdown's free, unprotected way off the floor.
	// Hard knockdown refuses it -- see CanActivateAbility.
	bAllowedFromKnockdown = true;
}

bool UTDJumpAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// **Hard knockdown removes the free stand.** The grade's whole shape is a narrower, meaner
	// split, and a costless exit would give back exactly the timing pressure the 1.5/0.5 buys. Hard
	// keeps the kip-up, the guard and the get-up attack -- all priced -- plus waiting. Sited here
	// rather than on the base's flag because it is a question about *this* option, and splitting
	// one ruling across two files is how the pair drifts apart. See bAllowedFromKnockdown.
	if (const ATDCombatCharacter* Downed = Cast<ATDCombatCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		if (Downed->IsKnockedDown() && Downed->GetKnockdownGrade() == ETDKnockdownGrade::Hard)
		{
			TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: no stand from a hard knockdown"),
				Downed->GetWorld() ? Downed->GetWorld()->GetTimeSeconds() : 0.0f,
				*GetName(),
				*GetNameSafe(Downed));
			return false;
		}
	}

	return true;
}

void UTDJumpAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	if (!Character)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// **From the floor this is a stand, not a jump.** The base already began the rise on the way
	// in; launching as well would fire the riser into the air, which is not what the neutral stand
	// is -- it is the plain, unprotected, fully committed way up. So the ability's entire body here
	// is the rise it already started, and it ends immediately: there is no button lifetime worth
	// owning when nothing set bPressedJump.
	if (const ATDCombatCharacter* Downed = Cast<ATDCombatCharacter>(Character))
	{
		if (Downed->IsKnockedDown())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
			return;
		}
	}

	// Records the press and drops the guard; the launch itself happens in CheckJumpInput on the
	// next movement tick, and OnJumped fires from there. ATDCombatCharacter's override does no
	// gating any more -- every question was answered before this line was reached.
	Character->Jump();

	// **Deliberately still active.** The ability's whole remaining job is to own the button until
	// it comes up, so InputReleased can clear the pressed flag. Nothing is blocked by its being
	// live: it owns no tags, takes no movement lock, and GAS refuses a second activation of an
	// ability already running, which is exactly the right answer for a button already held.
}

void UTDJumpAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	// Unconditional, unlike the guard's held-back release: there is no minimum duration to serve
	// and nothing to hold the button for once it is up.
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UTDJumpAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// **Before Super, which tears down the actor info this needs**, and unconditional rather than
	// guarded on how we got here. Cancellation is the path that matters: being hit out of a jump
	// ends this ability without the button ever coming up, and a pressed flag surviving that would
	// launch the character again the instant hitstun let go of them.
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->StopJumping();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
