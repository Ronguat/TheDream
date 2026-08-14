// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDGameplayAbility.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/Tasks/AbilityTask_FacingLunge.h"
#include "Combat/TDGameplayTags.h"
#include "Core/TheDreamCharacter.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"

UTDGameplayAbility::UTDGameplayAbility()
{
	// One instance per actor: abilities keep state across activations (combo index,
	// input holds) and per-execution instancing would throw that away every swing.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// Stated explicitly even though it is the engine default, because it is the engine default
	// only by accident -- UGameplayAbility's constructor never assigns NetExecutionPolicy, and
	// LocalPredicted is simply enum index 0. Every ability in this project was running the most
	// demanding policy without anyone choosing it.
	//
	// LocalPredicted *is* the right value for the agreed model (server-authoritative with
	// client prediction, decided 2026-08-11): the client starts acting immediately and the
	// server remains the authority. What is still owed is the other half -- prediction windows
	// so that a mispredicted activation is rolled back rather than left standing. Until that
	// exists this is a correct declaration of intent and an incomplete implementation of it,
	// which is deliberately better than an undeclared one.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

bool UTDGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// Checked here rather than by adding State.Dead to every ability's ActivationBlockedTags,
	// so a future ability cannot be granted without it. The dead are not a special case any
	// one ability should have to remember. Unconditional rather than a flag like
	// bBlockedWhileAirborne -- there is no ability a corpse should be able to use.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_Dead))
	{
		// The avatar's name, not just the ability's. Abilities are InstancedPerActor, so the
		// player and the training dummy each own an instance and both are legitimately called
		// GA_Attack_C_0 -- which made a real investigation impossible to finish, because the
		// trace could not say whose refusal it was. Added 2026-08-12 for exactly that reason.
		TD_TIMING_LOG(TEXT("[%.3f] REFUSED    %s on %s: dead"),
			ActorInfo->AvatarActor.IsValid() && ActorInfo->AvatarActor->GetWorld()
				? ActorInfo->AvatarActor->GetWorld()->GetTimeSeconds() : 0.0f,
			*GetName(),
			*GetNameSafe(ActorInfo->AvatarActor.Get()));
		return false;
	}

	// Same treatment as death and for the same reason: a stun that any one ability could be
	// granted without is a stun that will eventually be missed by one. A broken guard forbids
	// everything for its duration, so there is no per-ability judgement to make.
	//
	// Deliberately *not* buffered -- see ShouldBufferFailedInput. A guard break is a punish
	// window, and replaying every press made during one would hand back the exact seconds the
	// break exists to take away.
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

	if (bBlockedWhileAirborne)
	{
		const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		if (Movement && Movement->IsFalling())
		{
			// Traced, because a refused activation is otherwise indistinguishable from a
			// dropped input -- the exact failure this project treats as the worst feedback
			// available. Fires once per activation attempt, which is once per press while the
			// ability input is bound to Started; see the IA_Attack note in Combat-Decisions.
			//
			// Carries the avatar because the ability's own name cannot identify it: instances are
			// per actor, so the player's and the dummy's are both GA_Attack_C_0.
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
	// Death is the clearest case of a refusal that must not be remembered. It lasts whole
	// seconds, so a press buffered across it would fire on revive -- an action the player
	// asked for in a situation that no longer exists, which is precisely the false positive
	// the window length exists to prevent.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_Dead))
	{
		return false;
	}

	// A broken guard is the same shape as death, one second instead of several. The stun *is* the
	// punish window, so replaying whatever was mashed during it would refund the opening the break
	// was supposed to create -- and a player who has just been guard-broken is reacting to that,
	// not still asking for what they pressed a second ago.
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid()
		&& ActorInfo->AbilitySystemComponent->HasMatchingGameplayTag(TDTags::State_GuardBroken))
	{
		return false;
	}

	// The airborne refusal is the one kind that must not be remembered. Everything else this
	// ability can be refused by -- a committed swing, an exhaustion lockout, a live instance --
	// clears while the player is still standing there meaning it. Being in the air does not:
	// the thing it clears into is a landing, which is itself not a moment you can act through.
	// Buffering it would hand back a dodge on the touchdown frame that a landing recovery will
	// later have to take away again.
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

	// Ungated by role, like the facing lock and for the same reason: this is local input
	// suppression, and the machine that owns the input is the one that has to honour it. See the
	// facing-lock trap in Docs/Combat-Decisions.md, which covers both.
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
	// **Guarded on having taken it, not on bLocksMovement.** This runs on the shared base, so
	// every ability passes through here -- an unguarded release would let any ability's ending
	// hand movement back while a *different* one still owns it. Guarding on the property instead
	// would strand the lock if it were ever toggled off mid-run. The instance flag is the only
	// version that answers "did *I* take this", which is the actual question. Same reasoning as
	// bAttackCommitted guarding the committed tag's removal.
	if (bTookMovementLock)
	{
		if (ATheDreamCharacter* Character = Cast<ATheDreamCharacter>(GetAvatarActorFromActorInfo()))
		{
			Character->SetAbilityMovementLocked(false);
		}
		bTookMovementLock = false;
	}

	// Before Super, which tears down the actor info this needs. Authority-only, like every
	// other effect application in this project -- clients see the result by replication.
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

	// Target Lock's standoff goes to the source rather than being applied to the distance here.
	// Pre-shortening was the first implementation and it was wrong -- see StandoffCm on
	// FTDRootMotionSource_FacingForce for what play found and why the gate has to be per tick.
	UAbilityTask_FacingLunge* LungeTask = UAbilityTask_FacingLunge::ApplyFacingLunge(
		this,
		NAME_None,
		DistanceCm,
		DurationSeconds,
		StrengthCurve,
		StandoffCm,
		YawOffsetDegrees,
		// Velocity is clamped to nothing when the lunge ends, so no momentum survives into the
		// next phase. Without this a lunge hands its speed to whatever follows -- for the light
		// that is the branch lunge, which would then overshoot its own authored distance, and
		// for a cancelled attack it is a character who keeps sliding after the swing is gone.
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
		// Traced because there is otherwise no way to see this happen. The gate's effect is visible
		// as travel that did not occur, but a stop and a gate that stayed shut for the rest of the
		// lunge produce an identical resting position -- so without a line saying which one ran,
		// the two are indistinguishable from the outside, and only one of them survives the target
		// dying. The distance is what separates a stop-on-contact from a stop that arrived late.
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
