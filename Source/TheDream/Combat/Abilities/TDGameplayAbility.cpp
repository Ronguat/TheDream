// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDGameplayAbility.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDCombatCharacter.h"
#include "Combat/Tasks/AbilityTask_FacingLunge.h"
#include "Combat/TDGameplayTags.h"
#include "Core/TheDreamCharacter.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"

UTDGameplayAbility::UTDGameplayAbility()
{
	// One instance per actor: abilities keep state across activations (combo index, input holds)
	// and per-execution instancing would throw that away every swing.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// Stated explicitly even though it is the engine default, because it is the default only by
	// accident -- UGameplayAbility's constructor never assigns NetExecutionPolicy, and
	// LocalPredicted is enum index 0. It is the right value for the agreed model: the client acts
	// immediately, the server stays the authority. Still owed is the other half -- prediction
	// windows, so a mispredicted activation is rolled back rather than left standing.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UTDGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		// Everything expressed as ActivationBlockedTags -- a committed swing, a committed guard,
		// exhaustion, being mid-dodge -- fails inside Super with nothing logged, which reads as
		// "nothing was refused" and is the opposite of the truth. Names the offending tags rather
		// than saying "blocked". An empty set is itself informative: the refusal was a cost, a
		// missing required tag, or the ability already running.
		if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
		{
			FGameplayTagContainer Owned;
			ActorInfo->AbilitySystemComponent->GetOwnedGameplayTags(Owned);

			// **Owned.Filter(Blocked), never Blocked.Filter(Owned) -- the direction is the whole
			// correctness of this line.** Filter expands the tags of the container it is called on, so
			// the reversed form expands each *blocked* tag upward and matches a blocked
			// State.Blocking.Committed against a merely-owned State.Blocking. This direction expands
			// the *owned* tags, as HasAnyMatchingGameplayTags does, so the set named here is the set
			// GAS actually refused on.
			const FGameplayTagContainer Offending = Owned.Filter(ActivationBlockedTags);

			// Deduped, because the resume retries every tick while its input is held: without this
			// a guard waiting on exhaustion would emit sixty identical lines a second and drown
			// every low-frequency event in the same window.
			const UWorld* World = ActorInfo->AvatarActor.IsValid() ? ActorInfo->AvatarActor->GetWorld() : nullptr;
			const float Now = World ? World->GetTimeSeconds() : 0.0f;
			const FString Reason = Offending.IsEmpty() ? TEXT("(not a tag)") : Offending.ToStringSimple();

			if (Reason != LastRefusalReason || Now - LastRefusalLoggedAt > 0.5f)
			{
				LastRefusalReason = Reason;
				LastRefusalLoggedAt = Now;

				TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: %s"),
					Now, *GetName(), *GetNameSafe(ActorInfo->AvatarActor.Get()), *Reason);
			}
		}
		return false;
	}

	// The refusals below are sited here rather than in each ability's ActivationBlockedTags under
	// one rule: **a refusal any single ability could be granted without is one that will eventually
	// be missed by one.** Each traces the avatar as well as the ability, instances being per actor
	// and the player's and the dummy's both called GA_Attack_C_0. Death is unconditional rather
	// than a flag like bBlockedWhileAirborne -- there is no ability a corpse should be able to use.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_Dead))
	{
		TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: dead"),
			ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->GetWorld()
				? ActorInfo->AvatarActor->GetWorld()->GetTimeSeconds() : 0.0f,
			*GetName(),
			*GetNameSafe(ActorInfo->AvatarActor.Get()));
		return false;
	}

	// A broken guard forbids everything for its duration. Deliberately *not* buffered -- see
	// ShouldBufferFailedInput: a guard break is a punish window, and replaying every press made
	// during one would hand back the exact seconds the break exists to take away.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_GuardBroken))
	{
		TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: guard broken"),
			ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->GetWorld()
				? ActorInfo->AvatarActor->GetWorld()->GetTimeSeconds() : 0.0f,
			*GetName(),
			*GetNameSafe(ActorInfo->AvatarActor.Get()));
		return false;
	}

	// **State.Parrying, the window itself.** The commitment runs from *activation*, not
	// from window close, so a parry cannot be attacked, blocked or dodged out of once thrown --
	// which is what stops the read being free. The other two exits need no code here, both already
	// removing this tag: a success ends the ability at the catch, and an attacker's punishment
	// cancels it. See CloseParryWindow.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_Parrying))
	{
		TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: parrying"),
			ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->GetWorld()
				? ActorInfo->AvatarActor->GetWorld()->GetTimeSeconds() : 0.0f,
			*GetName(),
			*GetNameSafe(ActorInfo->AvatarActor.Get()));
		return false;
	}

	// **The whiff recovery, its self-inflicted counterpart**, which is what makes the recovery a
	// *price* rather than a pause. A parry costs no stamina, so the whole cost of a missed read is
	// the time, and time you can act during is not a cost. A parry that connected charges no
	// recovery at all, so this state never arises for one.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_ParryRecovery))
	{
		TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: parry recovery"),
			ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->GetWorld()
				? ActorInfo->AvatarActor->GetWorld()->GetTimeSeconds() : 0.0f,
			*GetName(),
			*GetNameSafe(ActorInfo->AvatarActor.Get()));
		return false;
	}

	// **Being parried refuses everything, and the refusal is what makes the punish window real.**
	// The swing ended at the catch, so without this the attacker would be free the instant it did
	// -- and a parry would shorten their commitment instead of lengthening it.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_ParryLockout))
	{
		TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: parry lockout"),
			ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->GetWorld()
				? ActorInfo->AvatarActor->GetWorld()->GetTimeSeconds() : 0.0f,
			*GetName(),
			*GetNameSafe(ActorInfo->AvatarActor.Get()));
		return false;
	}

	// Hitstun refuses everything, defense included -- not a side effect but the entire mechanism
	// behind "any hit in the string guarantees the rest". Unlike death and the guard break it
	// deliberately DOES buffer: hitstun is brief, and a press made during it is the defender's
	// punish attempt, the input the string's delay-and-bait game exists to read. While the chain
	// stays tight the re-press is refused again before it can matter; the moment the attacker
	// delays, it fires.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_Hitstun))
	{
		TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: hitstun"),
			ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->GetWorld()
				? ActorInfo->AvatarActor->GetWorld()->GetTimeSeconds() : 0.0f,
			*GetName(),
			*GetNameSafe(ActorInfo->AvatarActor.Get()));
		return false;
	}

	// **The knockdown lockout, and the input window that follows it. Three phases, two answers.** The
	// lockout refuses everything, and so does the rise, a rise being committed the moment it starts.
	// Between them the input window admits exactly the abilities that opted in as get-up options.
	// Deliberately *not* exempted from the input buffer: a press made in the lockout is the defender
	// asking for their get-up, and firing it on the frame the lockout ends is the design.
	if (const ATDCombatCharacter* Downed = Cast<ATDCombatCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		if (Downed->IsKnockedDown() && !(bAllowedFromKnockdown && Downed->IsInKnockdownInputWindow()))
		{
			TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: knocked down (%s)"),
				Downed->GetWorld() ? Downed->GetWorld()->GetTimeSeconds() : 0.0f,
				*GetName(),
				*GetNameSafe(Downed),
				Downed->IsInKnockdownInputWindow() ? TEXT("not a get-up option") : TEXT("lockout"));
			return false;
		}
	}

	// A flag rather than an ActivationBlockedTags entry, the movement lock having no tag to express
	// it. Reads bAbilityMovementLocked, which is *someone else's* lock by construction: an ability
	// holding it is already running, and a second activation of the same ability is refused by GAS
	// before this is reached. **A get-up is exempt from the floor's own movement lock, and it has
	// to be** -- knockdown locks movement for the whole down state and the neutral stand answers
	// the jump input, so without this the floor's lock would refuse the very option that exists to
	// leave it. The knockdown check above has already decided whether this ability is legal here.
	if (bBlockedWhileMovementLocked)
	{
		const ATheDreamCharacter* Character = ActorInfo ? Cast<ATheDreamCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
		const ATDCombatCharacter* Combatant = Cast<const ATDCombatCharacter>(Character);
		const bool bLegalGetUp = bAllowedFromKnockdown && Combatant && Combatant->IsInKnockdownInputWindow();
		if (Character && Character->IsMovementLocked() && !bLegalGetUp)
		{
			TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: movement locked"),
				Character->GetWorld() ? Character->GetWorld()->GetTimeSeconds() : 0.0f,
				*GetName(),
				*GetNameSafe(Character));
			return false;
		}
	}

	if (bBlockedWhileAirborne)
	{
		const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		if (Movement && Movement->IsFalling())
		{
			// Traced, because a refused activation is otherwise indistinguishable from a dropped
			// input. Fires once per activation attempt.
			TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: airborne (mode=%d)"),
				Character->GetWorld() ? Character->GetWorld()->GetTimeSeconds() : 0.0f,
				*GetName(),
				*GetNameSafe(Character),
				static_cast<int32>(Movement->MovementMode.GetValue()));
			return false;
		}
	}

	return true;
}

bool UTDGameplayAbility::ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// Death lasts whole seconds, so a press buffered across it would fire on revive -- an action
	// the player asked for in a situation that no longer exists.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_Dead))
	{
		return false;
	}

	// A broken guard is the same shape as death, one second instead of several. The stun *is* the
	// punish window, so replaying whatever was mashed during it would refund the opening the break
	// was supposed to create.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_GuardBroken))
	{
		return false;
	}

	// Everything else this ability can be refused by -- a committed swing, an exhaustion lockout, a
	// live instance -- clears while the player is still standing there meaning it. Being in the air
	// does not: it clears into a landing, which is itself not a moment you can act through.
	if (bBlockedWhileAirborne)
	{
		const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		if (Movement && Movement->IsFalling())
		{
			return false;
		}
	}

	return true;
}

void UTDGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Reset first: an ability instanced per actor outlives its activation, so a flag left true by
	// the previous run would make this one release a lock it never took.
	bTookMovementLock = false;

	// **The action is the exit.** A get-up option starting from the floor *is* the rise -- there is
	// no shared pre-rise for it to wait through, which is what makes the option's own timing the
	// thing the defender is choosing. Invincibility ends on this line.
	if (bAllowedFromKnockdown)
	{
		if (ATDCombatCharacter* Downed = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
		{
			if (Downed->IsKnockedDown())
			{
				Downed->BeginKnockdownRise(GetKnockdownRiseLabel(Downed), /*bPlayRiseMontage=*/!BringsOwnRiseMontage());
			}
		}
	}

	// Ungated by role, like the facing lock: this is local input suppression, and the machine that
	// owns the input is the one that has to honour it.
	if (bLocksMovement)
	{
		if (ATheDreamCharacter* Character = Cast<ATheDreamCharacter>(GetAvatarActorFromActorInfo()))
		{
			Character->SetAbilityMovementLocked(true);
			bTookMovementLock = true;
		}
	}

	if (EffectOnStart && HasAuthority(&ActivationInfo))
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(EffectOnStart, GetAbilityLevel());
			if (SpecHandle.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}
}

void UTDGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Before Super, which tears down the actor info this needs.
	//
	// **Guarded on having taken it, not on bLocksMovement.** This runs on the shared base, so every
	// ability passes through here -- an unguarded release would let any ability's ending hand
	// movement back while a *different* one still owns it. Guarding on the property instead would
	// strand the lock if it were ever toggled off mid-run. The instance flag is the only version
	// that answers "did *I* take this".
	if (bTookMovementLock)
	{
		if (ATheDreamCharacter* Character = Cast<ATheDreamCharacter>(GetAvatarActorFromActorInfo()))
		{
			Character->SetAbilityMovementLocked(false);
		}
		bTookMovementLock = false;
	}

	// Before Super, which tears down the actor info this needs. Authority-only, like every other
	// effect application in this project -- clients see the result by replication.
	if (EffectOnEnd && IsValid(this) && HasAuthority(&ActivationInfo))
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(EffectOnEnd, GetAbilityLevel());
			if (SpecHandle.IsValid())
			{
				ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UTDGameplayAbility::StartLunge(
	float DistanceCm,
	float DurationSeconds,
	UCurveFloat* StrengthCurve,
	float StandoffCm,
	float YawOffsetDegrees)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || DistanceCm <= 0.0f || DurationSeconds <= 0.0f)
	{
		return;
	}

	if (const ATDCombatCharacter* Combatant = Cast<ATDCombatCharacter>(Avatar))
	{
		if (Combatant->IsDebugLungeSuppressed())
		{
			TD_TIMING_LOG(TEXT("[%.3f] LUNGE SKIP %s suppressed %.0fcm (fixture)"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
				*GetNameSafe(Avatar),
				DistanceCm);
			return;
		}
	}

	// Target Lock's standoff goes to the source rather than being applied to the distance here --
	// see StandoffCm on FTDRootMotionSource_FacingForce for why the gate has to be per tick.
	UAbilityTask_FacingLunge* LungeTask = UAbilityTask_FacingLunge::ApplyFacingLunge(
		this,
		NAME_None,
		DistanceCm,
		DurationSeconds,
		StrengthCurve,
		StandoffCm,
		YawOffsetDegrees,
		// Velocity is clamped to nothing when the lunge ends, so no momentum survives into the next
		// phase. Without this the light's branch lunge would overshoot its own authored distance,
		// and a cancelled attack would leave a character sliding after the swing is gone.
		ERootMotionFinishVelocityMode::ClampVelocity,
		FVector::ZeroVector,
		/*ClampVelocityOnFinish=*/0.0f,
		/*bEnableGravity=*/true);

	if (LungeTask)
	{
		ActiveLungeTask = LungeTask;
		LungeTask->ReadyForActivation();
	}
}

void UTDGameplayAbility::StopLunge()
{
	if (UAbilityTask_FacingLunge* LungeTask = ActiveLungeTask.Get())
	{
		// Traced because a stop and a gate that stayed shut for the rest of the lunge produce an
		// identical resting position -- so without a line saying which one ran, the two are
		// indistinguishable from the outside, and only one of them survives the target dying.
		const AActor* Avatar = GetAvatarActorFromActorInfo();
		const UWorld* World = GetWorld();
		TD_TIMING_LOG(TEXT("[%.3f] LUNGE STOP %s"),
			World ? World->GetTimeSeconds() : -1.0f,
			Avatar ? *Avatar->GetName() : TEXT("<no avatar>"));

		// EndTask rather than ExternalCancel: the task's OnDestroy removes the root motion source
		// from the movement component, which is what actually stops the character. Cancelling would
		// additionally broadcast, and OnFinish is documented as *not* firing when the ability ends a
		// lunge early -- a hit is exactly that case, not the duration elapsing.
		LungeTask->EndTask();
	}

	ActiveLungeTask.Reset();
}
