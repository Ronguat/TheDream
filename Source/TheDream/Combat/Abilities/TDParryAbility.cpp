// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDParryAbility.h"
#include "Combat/TDCombatCharacter.h"
#include "Combat/TDCombatDebug.h"

UTDParryAbility::UTDParryAbility()
{
	// LocalPredicted like every other ability here, and the parry needs it most: the window is
	// 300 ms and a round trip spends a meaningful fraction of it, so an unpredicted parry would
	// open after the read that justified it. What the server actually decides is whether a hit was
	// negated -- that stays authority-side in the attacker's hit path.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// **Planted, unlike the guard.** The shared base's flag rather than anything bespoke. The split
	// it follows is action versus state: actions own their displacement -- an attack locks you, a
	// dodge moves you -- while a state you carry leaves you mobile, which is why GA_Block is the
	// one defensive ability that does not take this. A parry is an action, and it is the action
	// that manufactures a whiff at zero centimetres; a parrier free to drift while the attacker is
	// planted would blur the one piece of geometry the reward is derived from.
	bLocksMovement = true;

	// A parry is a claim about the ground you are standing on. Same reasoning as the guard's.
	bBlockedWhileAirborne = true;
}

void UTDParryAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// The base applies EffectOnStart, takes the movement lock this ability asked for above, and
	// runs the dead / exhausted / guard-broken / hitstun refusals shared by every ability.
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Pushed to the character rather than tracked here, for the reason the whole stun family lives
	// there: the window has to be readable by *someone else's* ability. It is the attacker's
	// HandleTraceHit that asks whether this window is open, and it has a character pointer, not an
	// ability one.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		Character->OpenParryWindow(ParryWindowSeconds, ParryWhiffLockoutSeconds);
	}

	// No montage played from here. The clip is cosmetic and is driven by State.Parrying the same
	// way the guard's stance is driven by State.Blocking -- so a missing or unfinished animation
	// cannot stop the parry working, which is what let this ship felt-not-seen exactly as
	// blockstun did.
}

void UTDParryAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// **Closing the window here is what makes every exit path safe**, and it is the same argument
	// the movement lock's release makes one level up: an ability that is cancelled -- hit out of,
	// guard-broken, killed -- must not leave a hit-negation window standing. The character's close
	// is idempotent, so the ordinary expiry having already run is not a special case.
	//
	// The whiff lockout is charged by the close itself rather than from here, so that a window
	// which ends without catching anything pays the same price however it ended. See
	// ATDCombatCharacter::CloseParryWindow.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		Character->CloseParryWindow();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UTDParryAbility::ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const
{
	return false;
}
