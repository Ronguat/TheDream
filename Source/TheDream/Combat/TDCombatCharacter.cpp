// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDCombatCharacter.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Combat/Attributes/TDAttributeSet.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "Core/TDPlayerState.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ATDCombatCharacter::ATDCombatCharacter()
{
	// The fallback pair, used only when this character has no PlayerState -- the training dummy.
	// A player builds these too and then ignores them in favour of its PlayerState's; see the
	// header. The subobject *names* are unchanged from when this was the only ASC, so the two
	// character Blueprints keep resolving their component templates.
	OwnedAbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	OwnedAbilitySystemComponent->SetIsReplicated(true);

	// Mixed: full effect replication to the owning client, minimal to everyone else.
	OwnedAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	OwnedAttributeSet = CreateDefaultSubobject<UTDAttributeSet>(TEXT("AttributeSet"));

	// The pack's own Sword / Shield sockets, which hang off hand_r and hand_l and carry the
	// grip rotation and a non-uniform scale (the shield's is 0.25, 0.20, 0.30 -- the mesh is
	// authored several times too large and the socket is what corrects it). Attach here and
	// both props are right at identity; anything else means re-deriving what the pack knows.
	//
	// Deliberately NOT the weapon_r / weapon_l bones, which look like the obvious choice and
	// are worse twice over: they are absent from Epic's SKM_Manny_Simple entirely, and only
	// GDH clips animate them -- so under any Epic animation the props freeze at reference
	// pose. hand_r / hand_l are driven by every animation there is.
	//
	// Cosmetic only. Collision is off because the melee trace is UAbilityTask_MeleeTrace's
	// job -- a prop that could block or overlap would let the mesh quietly decide reach,
	// which is the thing spacing tests measure.
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(GetMesh(), TEXT("Sword"));
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ShieldMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldMesh"));
	ShieldMesh->SetupAttachment(GetMesh(), TEXT("Shield"));
	ShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Stamina regen runs per frame rather than on a timer, so the bar moves smoothly
	// instead of stepping.
	PrimaryActorTick.bCanEverTick = true;
}

void ATDCombatCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Attributes are authority-only state; clients see regen by replication.
	if (HasAuthority())
	{
		// Drain before regen, so a frame in which both are live spends first and tops up second.
		// The order is observable at exactly one point -- a guard held at full stamina -- where
		// the reverse would let regen mask the drain entirely and the bar would never move.
		TickBlockDrain(DeltaSeconds);
		TickStaminaRegen(DeltaSeconds);

		// The stun is a timestamp rather than a timer; see GuardBreakEndsAt.
		if (bGuardBroken)
		{
			const UWorld* World = GetWorld();
			if (World && World->GetTimeSeconds() >= GuardBreakEndsAt)
			{
				EndGuardBreak();
			}
		}
	}

	// A guard does not survive leaving the ground. Keyed to the *falling state* rather than to
	// having jumped, so walking off a ledge drops it too -- the user's call, and the opposite of
	// the jump regen pause beside it, which keys on the action precisely because it is charging
	// you for a choice. Here the question is what is physically coherent, and holding a shield up
	// is not something the air supports.
	//
	// bBlockedWhileAirborne on GA_Block covers only *raising* one; ActivationBlockedTags and the
	// airborne flag both gate activation and neither can end something already running.
	if (IsBlocking() && GetCharacterMovement() && GetCharacterMovement()->IsFalling())
	{
		CancelBlockAbility();
	}

	// Not authority-gated: the speed cap is local presentation of a local state, and the movement
	// component is already client-predicted.
	TickBlockingMoveSpeed();

	// Not authority-gated: a buffered press is local input waiting to be spent, and it is
	// spent through the same path a live press takes.
	TickInputBuffer();

#if ENABLE_DRAW_DEBUG
	// Same gate as the damage wedge (bDrawDebugTrace || the cvar), so the two appear together --
	// they were on different gates and only one showed by default, which is confusing precisely
	// when you are trying to compare their sizes.
	//
	// Drawn from the *aim* yaw, because that is the frame the wedge is actually tested in. Drawing
	// it on the body would diverge from the tested volume the moment homing turns the body, which
	// is a debug view lying exactly when the system acts.
	if (bAimAssistHoming && (bAimAssistDrawDebug || TDShouldDrawMeleeTrace()))
	{
		AimAssistWedge.DrawDebug(GetWorld(), GetActorLocation(), GetAimYawDegrees(), FColor::Cyan);
	}
#endif
}


void ATDCombatCharacter::TickStaminaRegen(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !AbilitySystem)
	{
		return;
	}

	// Exhaustion recovers at its own rate. Chosen before the early-out so a zero *normal* rate --
	// a legitimate thing to author, meaning "no passive regen" -- cannot also stall the exhaustion
	// exit, which would make an intentional design choice into a permanent lockout.
	const float RegenPerSecond = bExhausted ? ExhaustedStaminaRegenPerSecond : StaminaRegenPerSecond;
	if (RegenPerSecond <= 0.0f)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	bool bSuppressorActive = false;

	// While a suppressor is active the resume time keeps being pushed forward, which is what
	// makes each pause measure from when its cause *ends* without anyone tracking that. Taking
	// the max rather than assigning lets two overlap without the shorter cutting the longer short.
	if (StaminaRegenPausedTag.IsValid() && AbilitySystem->HasMatchingGameplayTag(StaminaRegenPausedTag))
	{
		RegenSuppressedUntil = FMath::Max(RegenSuppressedUntil, Now + StaminaRegenPauseSeconds);
		bSuppressorActive = true;
	}

	// Set at the jump's launch and cleared on landing, so the pause spans the whole airborne
	// period plus its tail. Deliberately not driven by IsFalling(): walking off a ledge is not
	// an action and costs nothing.
	if (bJumpRegenPauseActive)
	{
		RegenSuppressedUntil = FMath::Max(RegenSuppressedUntil, Now + JumpRegenPauseSeconds);
		bSuppressorActive = true;
	}

	// A broken guard suppresses regen for the stun, and the ordinary pause then runs on from
	// there -- which is what the user asked for and falls straight out of the max-push above
	// rather than needing to be sequenced. Pushing StaminaRegenPauseSeconds (not the stun's own
	// length) is the whole trick: while the stun is live the resume time keeps moving to
	// "half a second from now", so the pause begins measuring from the instant the stun ends.
	if (bGuardBroken)
	{
		RegenSuppressedUntil = FMath::Max(RegenSuppressedUntil, Now + StaminaRegenPauseSeconds);
		bSuppressorActive = true;
	}

	if (GetStamina() >= GetMaxStamina())
	{
		return;
	}

	// **Exhaustion does not bypass the pause.** It did for a few hours on 2026-08-14 and play threw
	// it straight back out: the bypass was written to stop a held guard stalling the only condition
	// that ends exhaustion, but it closed the *bounded* cases along with the unbounded one, so a
	// dodge that exhausted you started regenerating during its own duration. The pause is a cost of
	// acting, and being exhausted is not a refund -- if anything it is where the cost should bite
	// hardest.
	//
	// **The unbounded case stopped being a deadlock by design rather than by code.** A player may
	// hold block at zero stamina; it accomplishes nothing, since anything actually blocked breaks
	// the guard, and it suppresses only their own regen. Releasing is always available and always
	// correct, so this is a state chosen rather than one trapped in -- which is what separates it
	// from a deadlock and is why nothing here has to defend against it. See Docs/Combat-Decisions.md.
	if (bSuppressorActive || Now < RegenSuppressedUntil)
	{
		return;
	}

	AbilitySystem->ApplyModToAttribute(
		UTDAttributeSet::GetStaminaAttribute(),
		EGameplayModOp::Additive,
		RegenPerSecond * DeltaSeconds);
}

void ATDCombatCharacter::TickBlockDrain(float DeltaSeconds)
{
	if (!AbilitySystem || BlockDrainPerSecond <= 0.0f || !IsBlocking())
	{
		return;
	}

	// Floors at zero and stays there. **Drain can never break a guard** -- that is the line
	// between the two stamina mechanisms, and it is enforced here by simply not asking. A guard
	// held at zero is a guard that has stopped being able to absorb anything, which is a
	// consequence an attacker has to come and collect rather than one that arrives on its own.
	//
	// The attribute set clamps to [0, Max], so no floor is applied here; doing it in both places
	// would be a second copy of a rule that already has one home.
	//
	// Flagged across the write so HandleStaminaChanged can tell this apart from every other way
	// the bar empties. See bApplyingBlockDrain -- drain parks you at zero, it does not exhaust you.
	bApplyingBlockDrain = true;
	AbilitySystem->ApplyModToAttribute(
		UTDAttributeSet::GetStaminaAttribute(),
		EGameplayModOp::Additive,
		-BlockDrainPerSecond * DeltaSeconds);
	bApplyingBlockDrain = false;
}

void ATDCombatCharacter::CancelBlockAbility()
{
	if (!AbilitySystem || !BlockingTag.IsValid())
	{
		return;
	}

	FGameplayTagContainer BlockingTags;
	BlockingTags.AddTag(BlockingTag);
	AbilitySystem->CancelAbilities(&BlockingTags);
}

void ATDCombatCharacter::TickBlockingMoveSpeed()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || DefaultMaxWalkSpeed <= 0.0f)
	{
		return;
	}

	// Recomputed every tick from the current state rather than set on the ability's edges. An
	// edge-driven version has to restore on every exit path -- released, cancelled, guard broken,
	// airborne, interrupted -- and stranding the slow speed is both easy and invisible, since a
	// character that walks at a quarter speed forever looks like a tuning mistake rather than a bug.
	const float Target = IsBlocking() ? BlockingMaxWalkSpeed : DefaultMaxWalkSpeed;
	if (!FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Target))
	{
		Movement->MaxWalkSpeed = Target;
	}
}

void ATDCombatCharacter::HandleAbilityEndedForResume(const FAbilityEndedData& EndedData)
{
	if (!AbilitySystem)
	{
		return;
	}

	// A held button is a continuous statement of intent, so an ability that *is* a held state comes
	// back when whatever interrupted it finishes. Opt-in per ability -- see bResumeWhileInputHeld --
	// because the general form turns a held attack button into auto-repeat.
	//
	// Iterated over a copy: activating inside the loop can reallocate the spec array, and the
	// original is the ASC's live container.
	TArray<FGameplayAbilitySpecHandle> Resumable;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (Spec.IsActive() || !Spec.InputPressed)
		{
			continue;
		}

		const UTDGameplayAbility* Ability = Cast<UTDGameplayAbility>(Spec.Ability);
		if (Ability && Ability->ShouldResumeWhileInputHeld())
		{
			Resumable.Add(Spec.Handle);
		}
	}

	for (const FGameplayAbilitySpecHandle& Handle : Resumable)
	{
		// Goes through CanActivateAbility like any press, so exhaustion, a broken guard or being
		// airborne refuse it exactly as they would refuse a fresh one. Nothing here has to know
		// which of those is currently true.
		AbilitySystem->TryActivateAbility(Handle);
	}
}

bool ATDCombatCharacter::IsBlocking() const
{
	return AbilitySystem && BlockingTag.IsValid() && AbilitySystem->HasMatchingGameplayTag(BlockingTag);
}

bool ATDCombatCharacter::IsGuardFacing(const FVector& AttackOriginWorld) const
{
	FVector ToAttacker = AttackOriginWorld - GetActorLocation();
	ToAttacker.Z = 0.0f;

	// An attacker standing exactly on top of you has no bearing to test, and refusing the block
	// there would be an arbitrary answer to a degenerate question. Grant it: the defender is
	// holding a guard, and there is no direction the hit demonstrably came from.
	if (ToAttacker.IsNearlyZero())
	{
		return true;
	}

	// 180 degrees means the whole forward hemisphere, so the test is simply "not behind me".
	// Written as a dot product rather than an angle because the spec's number is exactly the
	// one value where the comparison needs no arc arithmetic at all -- and if the arc ever stops
	// being 180 this becomes an authored number and should move to a UPROPERTY rather than
	// growing a constant here.
	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.0f;

	return FVector::DotProduct(Forward.GetSafeNormal(), ToAttacker.GetSafeNormal()) >= 0.0f;
}

void ATDCombatCharacter::ApplyStaminaDamage(float Amount)
{
	if (!HasAuthority() || !AbilitySystem || Amount <= 0.0f)
	{
		return;
	}

	AbilitySystem->ApplyModToAttribute(
		UTDAttributeSet::GetStaminaAttribute(),
		EGameplayModOp::Additive,
		-Amount);

	// **Read the bar back rather than predicting it.** The attribute set clamps to [0, Max] in
	// both base and current value, so "did this empty them" is a question only the clamped result
	// can answer -- computing it from Amount would disagree with the bar the moment anything else
	// touches stamina in the same frame, which the drain above does whenever a guard is held.
	if (GetStamina() <= 0.0f)
	{
		EnterGuardBreak();
	}
}

void ATDCombatCharacter::EnterGuardBreak()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Re-entrant deliberately: a second blocked hit landing during a stun extends it rather than
	// being ignored. Being hit again while your guard is already broken is strictly worse than
	// being hit once, and the alternative -- an early-out on bGuardBroken -- would make the stun
	// a window of free hits.
	GuardBreakEndsAt = World->GetTimeSeconds() + GuardBreakStunSeconds;

	if (!bGuardBroken)
	{
		bGuardBroken = true;
		ApplyGuardBreakState();
	}
}

void ATDCombatCharacter::EndGuardBreak()
{
	if (!bGuardBroken)
	{
		return;
	}

	bGuardBroken = false;
	ClearGuardBreakState();
}

void ATDCombatCharacter::ApplyGuardBreakState()
{
	if (AbilitySystem)
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_GuardBroken);

		// The guard is gone, so the ability holding it has to go with it -- otherwise BlockingTag
		// survives the break and the drain keeps running on a guard the player no longer has.
		// Cancelled rather than left to end on input release, because a broken guard should not
		// wait for the player to notice.
		CancelBlockAbility();
	}

	TD_TIMING_LOG(TEXT("[%.3f] GUARD BREAK %s  stun=%.2fs"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		GuardBreakStunSeconds);
}

void ATDCombatCharacter::ClearGuardBreakState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_GuardBroken);
	}

	TD_TIMING_LOG(TEXT("[%.3f] GUARD END  %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName());
}

void ATDCombatCharacter::OnRep_GuardBroken()
{
	if (bGuardBroken)
	{
		ApplyGuardBreakState();
	}
	else
	{
		ClearGuardBreakState();
	}
}

bool ATDCombatCharacter::IsIdle() const
{
	if (!Super::IsIdle())
	{
		return false;
	}

	// A press that was refused and stored is still a press. The player has asked for something
	// and is waiting on it, which is not idling.
	if (BufferedInput.IsSet())
	{
		return false;
	}

	if (!AbilitySystem)
	{
		return true;
	}

	// Any active ability at all, rather than a list of state tags. Attacking and dodging are
	// covered today; block, parry and every stun state are covered without editing this.
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (Spec.IsActive())
		{
			return false;
		}
	}

	return true;
}

bool ATDCombatCharacter::IsStaminaRegenPaused() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const bool bActionRunning = AbilitySystem && StaminaRegenPausedTag.IsValid()
		&& AbilitySystem->HasMatchingGameplayTag(StaminaRegenPausedTag);

	return bActionRunning || bJumpRegenPauseActive || World->GetTimeSeconds() < RegenSuppressedUntil;
}

void ATDCombatCharacter::HandleStaminaChanged(const FOnAttributeChangeData& Data)
{
	if (!bExhausted)
	{
		// **Drain never exhausts.** Holding a guard runs the bar to zero and leaves it there, which
		// is what converts holding into risk rather than into a countdown: a guard at zero has
		// stopped being able to absorb anything and breaks to the next blocked hit. Every other
		// spender still exhausts -- a dodge taken at 30 empties you and locks you out, as designed.
		if (Data.NewValue <= 0.0f && !bApplyingBlockDrain)
		{
			EnterExhaustion();
		}
		return;
	}

	// Recovery ends exhaustion, not a clock. Regen is the only thing that can get here, which
	// is why it must keep running while exhausted -- see ExhaustedTag.
	if (Data.NewValue >= GetMaxStamina())
	{
		ExitExhaustion();
	}
}

void ATDCombatCharacter::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (bDead || Data.NewValue > 0.0f)
	{
		return;
	}

	EnterDeath();
}

void ATDCombatCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// To everyone, not just the owner: a simulated proxy has to know an opponent is dead or
	// exhausted, because that is what its own ragdoll and greyed bar are drawn from.
	DOREPLIFETIME(ATDCombatCharacter, bDead);
	DOREPLIFETIME(ATDCombatCharacter, bExhausted);
	DOREPLIFETIME(ATDCombatCharacter, bGuardBroken);
}

void ATDCombatCharacter::EnterDeath()
{
	if (bDead)
	{
		return;
	}
	bDead = true;

	TD_TIMING_LOG(TEXT("[%.3f] DEATH      %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetName());

	// Server-only, and deliberately outside ApplyDeathState. Cancelling abilities is an
	// authority decision that replicates through GAS on its own; running it again from a
	// client's OnRep would cancel that client's *predicted* copies out from under a
	// correction that may never come.
	if (AbilitySystem)
	{
		// The tag alone only refuses *new* activations, which is exhaustion's rule and is
		// visibly wrong here: a killing blow landing mid-swing would otherwise leave a corpse
		// finishing its attack, hitbox included. Cancelling also clears State.Attacking and
		// State.Attacking.Committed through the normal ability-end path, so death cannot leak
		// the tags that would forbid every future defensive action on revive.
		AbilitySystem->CancelAllAbilities();
	}

	// A buffered press must not survive death: it would fire on revive, an action asked for
	// in a situation that no longer exists. Local input state, so it is meaningless on any
	// machine that is not the one that pressed the button.
	BufferedInput.Clear();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BufferedReleaseTimerHandle);
	}

	if (DebugAutoReviveSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			DebugReviveTimerHandle,
			this,
			&ATDCombatCharacter::ReviveFromDebug,
			DebugAutoReviveSeconds,
			false);
	}

	ApplyDeathState();
}

void ATDCombatCharacter::OnRep_Dead()
{
	// The server ran the matching half directly. Everything below is what a client needs in
	// order to *look* and *behave* dead without being told twice.
	if (bDead)
	{
		ApplyDeathState();
	}
	else
	{
		ClearDeathState();
	}
}

void ATDCombatCharacter::ApplyDeathState()
{
	if (AbilitySystem)
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_Dead);
	}

	// Otherwise "dead" is only a tag and the corpse keeps walking, which is the exact
	// complaint this item exists to fix. Velocity is zeroed as well as input disabled, or
	// momentum carries the body along for a second afterwards.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	// Dying airborne would otherwise strand this flag set forever: DisableMovement stops the
	// fall, so Landed() never fires to clear it, and regen stays suppressed for the rest of
	// the character's life -- past the revive, silently. Death ends the jump it was tracking.
	bJumpRegenPauseActive = false;

	if (bRagdollOnDeath)
	{
		StartRagdoll();
	}
}

void ATDCombatCharacter::ClearDeathState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_Dead);
	}

	// Before the teleport, deliberately. StopRagdoll reattaches the mesh to the capsule, and
	// moving the actor while physics still drives the mesh in world space would leave the body
	// behind. Order is the whole correctness argument here.
	StopRagdoll();

	// An auto-attacker revives at the spot it was placed rather than wherever its last root
	// motion carried it. Without this it stands up displaced and only drifts home on its next
	// attack cycle -- which is what play reported. No-op for anything that is not an
	// auto-attacker, so the player revives where they fell.
	ReturnToDebugAutoAttackHome();

	// Falling rather than Walking, deliberately. The character may have died in mid-air, and
	// forcing Walking there leaves it hovering with no gravity until something else disturbs
	// it. Falling is self-correcting in both cases: on the ground the movement component
	// resolves it to Walking on the next update, in the air it simply resumes the fall.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Falling);
	}
}

void ATDCombatCharacter::StartRagdoll()
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!SkeletalMesh || bRagdollActive)
	{
		return;
	}

	// Physics silently refuses to simulate without one, leaving the character standing dead
	// with no error anywhere. Warned rather than logged quietly: a mesh swap is exactly how
	// this would break, and the symptom -- death stops looking like death -- reads as a
	// gameplay regression rather than a missing asset.
	if (!SkeletalMesh->GetPhysicsAsset())
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("%s: bRagdollOnDeath is set but the mesh has no physics asset; death will not ragdoll."),
			*GetName());
		return;
	}

	bRagdollActive = true;

	// The capsule stops colliding so it cannot hold the body up or shove it around. It is
	// deliberately left in place rather than moved: it is still what the actor's transform
	// means, and the revive puts the mesh back onto it.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// The profile replaces the whole response table, so it drops the camera-probe exemption the
	// constructor set on this mesh -- which is why the spring arm starts colliding with corpses.
	// Re-applied rather than avoided: the Ragdoll profile is genuinely what the simulated body
	// wants, it just must not be the last word on ECC_Camera.
	SkeletalMesh->SetCollisionProfileName(TEXT("Ragdoll"));
	ApplyCameraCollisionExemption();

	SkeletalMesh->SetAllBodiesSimulatePhysics(true);
	SkeletalMesh->SetSimulatePhysics(true);
	SkeletalMesh->WakeAllRigidBodies();
}

void ATDCombatCharacter::StopRagdoll()
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!SkeletalMesh || !bRagdollActive)
	{
		return;
	}
	bRagdollActive = false;

	SkeletalMesh->SetSimulatePhysics(false);
	SkeletalMesh->SetAllBodiesSimulatePhysics(false);
	SkeletalMesh->PutAllRigidBodiesToSleep();

	// Same trap on the way back, and this half is the worse one: restoring `CharacterMesh` reads
	// as returning to the known-good state, so the exemption stays dropped for the rest of the
	// character's life rather than only while the body is on the floor.
	SkeletalMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
	ApplyCameraCollisionExemption();

	// Reattached explicitly: simulation detaches the mesh from the capsule in all but name,
	// and setting the relative transform without reattaching leaves it in world space.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		SkeletalMesh->AttachToComponent(Capsule, FAttachmentTransformRules::KeepRelativeTransform);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// From the value captured before physics ever touched it. Reading the current relative
	// transform here would bake the ragdoll's final pose in as the rest offset.
	SkeletalMesh->SetRelativeTransform(MeshRestRelativeTransform);
}

void ATDCombatCharacter::ReviveFromDebug()
{
	if (!bDead)
	{
		return;
	}
	bDead = false;

	TD_TIMING_LOG(TEXT("[%.3f] REVIVE     %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetName());

	// Authority-only: attribute writes are the server's, and clients receive them by
	// replication. Restoring them from a client's OnRep would be a client rewriting its own
	// health, which is both wrong and the shape of an exploit.
	if (AbilitySystem)
	{
		AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), GetMaxHealth());

		// Stamina too, deliberately. Dying at low stamina and reviving instantly exhausted is
		// a debug annoyance with no design content -- and exhaustion ends only at full, so it
		// would outlast the revive by several seconds.
		AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetStaminaAttribute(), GetMaxStamina());
	}

	// Exhaustion is cleared explicitly rather than waiting for the stamina delegate: the
	// delegate fires on a *change*, and reviving from an already-full bar changes nothing.
	ExitExhaustion();

	ClearDeathState();
}

void ATDCombatCharacter::EnterExhaustion()
{
	bExhausted = true;
	ApplyExhaustionState();
}

void ATDCombatCharacter::ExitExhaustion()
{
	if (!bExhausted)
	{
		return;
	}
	bExhausted = false;

	ClearExhaustionState();
}

void ATDCombatCharacter::OnRep_Exhausted()
{
	if (bExhausted)
	{
		ApplyExhaustionState();
	}
	else
	{
		ClearExhaustionState();
	}
}

void ATDCombatCharacter::ApplyExhaustionState()
{
	if (AbilitySystem && ExhaustedTag.IsValid())
	{
		AbilitySystem->AddLooseGameplayTag(ExhaustedTag);
	}
}

void ATDCombatCharacter::ClearExhaustionState()
{
	if (AbilitySystem && ExhaustedTag.IsValid())
	{
		AbilitySystem->RemoveLooseGameplayTag(ExhaustedTag);
	}
}

void ATDCombatCharacter::Jump()
{
	// Deliberately silent. Exhaustion is communicated by the tag and the empty bar; a
	// failed jump that plays nothing reads as the lockout it is. Jump is not a
	// GameplayAbility, so the dead check in UTDGameplayAbility does not cover it and has
	// to be repeated here -- the one place that rule is not centralised.
	//
	// The movement lock is here for the same reason and is the third rule this function has had
	// to copy. That is the argument for jump eventually becoming an ability: every lockout the
	// abilities get for free has to be restated here, and this is the only place that can be
	// forgotten. Deferred to the structure audit rather than done inline.
	if (bExhausted || bDead || IsMovementLocked())
	{
		return;
	}

	Super::Jump();

	// **The guard does not survive the jump, and is dropped here rather than on becoming airborne.**
	// Tick already drops it on the falling state, which covers walking off a ledge -- but that is a
	// frame or more away, and a jump that begins with the shield still up is visible. Doing both is
	// deliberate: this one is for the look, the Tick one is for the rule.
	//
	// After Super::Jump() so a refused jump -- one the movement component itself declines, having
	// passed the checks above -- does not silently cost the player their guard.
	CancelBlockAbility();
}

void ATDCombatCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();

	// Hooked here rather than in Jump(), which only records the button press. A press that
	// never becomes a launch -- held against a ceiling, or pressed while already falling --
	// must not pause regen, or the pause would be charging for something that did not happen.
	bJumpRegenPauseActive = true;
}

void ATDCombatCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// Clearing the flag is the whole job: the last airborne tick already pushed
	// RegenSuppressedUntil to JumpRegenPauseSeconds ahead, so the tail measures from here.
	// Landing after walking off a ledge clears a flag that was never set, which is the point.
	bJumpRegenPauseActive = false;
}

UAbilitySystemComponent* ATDCombatCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

void ATDCombatCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Captured before anything can move it, so the revive has a true rest pose to restore.
	if (const USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		MeshRestRelativeTransform = SkeletalMesh->GetRelativeTransform();
	}

	// Captured before a guard can lower it, so the restore returns to whatever this Blueprint
	// authored rather than to a constant this class invented.
	if (const UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		DefaultMaxWalkSpeed = Movement->MaxWalkSpeed;
	}

	// Covers characters that are never possessed, such as a placed training dummy.
	InitialiseAbilitySystem();
}

void ATDCombatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Possession is where a player's PlayerState first becomes reachable on the server, so this
	// is the call that swaps a player off the fallback ASC and onto the real one.
	InitialiseAbilitySystem();
}

void ATDCombatCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// The client half of the same swap. PossessedBy does not run on a simulated proxy, and the
	// PlayerState replicates after the pawn, so without this a client keeps the fallback ASC.
	InitialiseAbilitySystem();
}

UAbilitySystemComponent* ATDCombatCharacter::ResolveAbilitySystem(AActor*& OutOwner) const
{
	if (ATDPlayerState* TDPlayerState = GetPlayerState<ATDPlayerState>())
	{
		OutOwner = TDPlayerState;
		return TDPlayerState->GetAbilitySystemComponent();
	}

	OutOwner = const_cast<ATDCombatCharacter*>(this);
	return OwnedAbilitySystemComponent;
}

void ATDCombatCharacter::InitialiseAbilitySystem()
{
	AActor* OwnerActor = nullptr;
	UAbilitySystemComponent* Resolved = ResolveAbilitySystem(OwnerActor);
	if (!Resolved)
	{
		return;
	}

	AbilitySystem = Resolved;

	if (ATDPlayerState* TDPlayerState = Cast<ATDPlayerState>(OwnerActor))
	{
		AttributeSet = TDPlayerState->GetAttributeSet();
	}
	else
	{
		AttributeSet = OwnedAttributeSet;
	}

	// Rebound every time rather than once: possession changes the owner, and the owner is what
	// GAS resolves prediction keys and net roles against. The avatar stays the pawn, so traces,
	// sockets and montages keep reading the body.
	AbilitySystem->InitAbilityActorInfo(OwnerActor, this);

	// Attributes and abilities are authority-only state; clients receive them by replication.
	if (HasAuthority())
	{
		SeedAbilitySystemDefaults();
	}
}

void ATDCombatCharacter::SeedAbilitySystemDefaults()
{
	// Guarded per *ASC*, not per call and not per character. Actor info is rebound on every
	// possession but seeding must happen once, or a pawn possessed after BeginPlay is granted
	// every ability a second time and stacks a second copy of every DefaultEffect -- and for an
	// infinite effect that is a permanently doubled magnitude rather than a visible one-off.
	//
	// Where the flag lives is the subtle half. A player's BeginPlay runs *before* possession, so
	// a single flag on the character would be spent on the fallback ASC and the PlayerState's
	// real one would never be seeded -- a player with no attributes and no abilities, while the
	// never-possessed training dummy worked perfectly.
	ATDPlayerState* TDPlayerState = GetPlayerState<ATDPlayerState>();

	if (TDPlayerState)
	{
		if (TDPlayerState->HasSeededDefaults())
		{
			return;
		}
		TDPlayerState->MarkDefaultsSeeded();
	}
	else
	{
		if (bOwnedDefaultsApplied)
		{
			return;
		}
		bOwnedDefaultsApplied = true;
	}

	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetMaxHealthAttribute(), StartingMaxHealth);
	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), StartingMaxHealth);
	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetMaxStaminaAttribute(), StartingMaxStamina);
	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetStaminaAttribute(), StartingMaxStamina);

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
	{
		if (AbilityClass)
		{
			// Input is matched against the ability's InputTag at press time, so the spec
			// needs no input ID of its own.
			AbilitySystem->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	}

	FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
	Context.AddSourceObject(this);

	for (const TSubclassOf<UGameplayEffect>& EffectClass : DefaultEffects)
	{
		if (!EffectClass)
		{
			continue;
		}

		const FGameplayEffectSpecHandle SpecHandle = AbilitySystem->MakeOutgoingSpec(EffectClass, 1, Context);
		if (SpecHandle.IsValid())
		{
			AbilitySystem->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}

	// Bound after seeding, so the initial fill to full does not read as a change to zero.
	AbilitySystem->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetStaminaAttribute())
		.AddUObject(this, &ATDCombatCharacter::HandleStaminaChanged);

	AbilitySystem->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetHealthAttribute())
		.AddUObject(this, &ATDCombatCharacter::HandleHealthChanged);

	// Bound for every character rather than behind a flag: resuming is a property of the ability
	// that opted in, not of the pawn, so a dummy granted a guard later gets it without a change here.
	AbilitySystem->OnAbilityEnded.AddUObject(this, &ATDCombatCharacter::HandleAbilityEndedForResume);

	if (bDebugAutoAttack && DebugAutoAttackInputTag.IsValid())
	{
		// Captured before the first swing, so it is the placed transform rather than wherever
		// root motion has since carried us.
		DebugAutoAttackHomeTransform = GetActorTransform();

		// Reset when the swing actually finishes rather than on a fixed delay: attack length
		// varies by tier, and a delay long enough for a charged attack would be most of the gap.
		AbilitySystem->OnAbilityEnded.AddUObject(this, &ATDCombatCharacter::HandleDebugAutoAttackEnded);

		GetWorldTimerManager().SetTimer(
			DebugAutoAttackTimerHandle,
			this,
			&ATDCombatCharacter::DebugAutoAttackPress,
			DebugAutoAttackInterval,
			true,
			DebugAutoAttackInterval);
	}
}

void ATDCombatCharacter::ReturnToDebugAutoAttackHome()
{
	// Guarded on the auto-attack flag as well as the reset flag, because HomeTransform is only
	// captured for auto-attackers. Without this, calling it on anything else -- the player, on
	// revive -- teleports to an identity transform, i.e. the world origin.
	if (!bDebugAutoAttackResetPosition || !bDebugAutoAttack)
	{
		return;
	}

	// Death cancels the running attack, which fires the ability-ended reset -- so without this
	// the dummy's corpse teleports home mid-ragdoll, dragging the capsule out from under a mesh
	// that physics is driving in world space. ReviveFromDebug calls this again once the ragdoll
	// has been put away, which is what actually gets the body home.
	if (bDead)
	{
		return;
	}

	// Teleported rather than swept: a swept move would be blocked by whatever the attacker has
	// walked into, which is exactly the state being undone.
	SetActorTransform(DebugAutoAttackHomeTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// Root motion leaves velocity behind; without this the attacker slides away from home
	// immediately after being put back.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
}

void ATDCombatCharacter::UpdateDebugFacingFocus(bool bAttacking)
{
	if (DebugAutoAttackFacingMode == ETDDebugFacingMode::Never)
	{
		return;
	}

	// Only an AI controller has a focus. A player-controlled character reaching here is not an
	// error -- the debug attacker lives on the shared base -- it simply has nothing to set.
	AAIController* AI = Cast<AAIController>(GetController());
	if (!AI)
	{
		return;
	}

	// WhileAttacking hands facing back the moment the swing ends, and the position reset then
	// restores the placed yaw. Clearing rather than re-aiming is what keeps the dummy from
	// following the player around between swings.
	if (!bAttacking && DebugAutoAttackFacingMode == ETDDebugFacingMode::WhileAttacking)
	{
		AI->ClearFocus(EAIFocusPriority::Gameplay);
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Nearest other living pawn, with no probe radius: this is a test level, and inventing a
	// cutoff here would be a second spacing number nobody authored. Deliberately not "the player
	// pawn" -- the auto-attacker is on the shared base, so a self-attacking player would
	// otherwise focus on itself.
	APawn* Nearest = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (!Candidate || Candidate == this)
		{
			continue;
		}

		// A corpse is not something to aim at, and the dummy outlives the player's deaths.
		const ATDCombatCharacter* AsCombatant = Cast<ATDCombatCharacter>(Candidate);
		if (AsCombatant && AsCombatant->IsDead())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			Nearest = Candidate;
		}
	}

	if (Nearest)
	{
		AI->SetFocus(Nearest, EAIFocusPriority::Gameplay);
	}
}

void ATDCombatCharacter::HandleDebugAutoAttackEnded(const FAbilityEndedData& EndedData)
{
	// Before the reset, not after: the reset restores the placed yaw, and holding the focus
	// through it would have the controller immediately turn back out of what it just restored.
	UpdateDebugFacingFocus(/*bAttacking=*/false);

	// The interesting reset: it puts the attacker home for the whole gap between swings, so it
	// idles where it was placed instead of wherever its last lunge left it.
	//
	// Delayed, because the ability ends when its montage blends out rather than when the swing
	// looks finished -- resetting on that edge alone visibly snaps the attacker home in the
	// middle of its follow-through.
	if (!bDebugAutoAttackResetPosition)
	{
		return;
	}

	if (DebugAutoAttackResetDelaySeconds <= 0.0f)
	{
		ReturnToDebugAutoAttackHome();
		return;
	}

	GetWorldTimerManager().SetTimer(
		DebugAutoAttackResetTimerHandle,
		this,
		&ATDCombatCharacter::ReturnToDebugAutoAttackHome,
		DebugAutoAttackResetDelaySeconds,
		false);
}

void ATDCombatCharacter::DebugAutoAttackPress()
{
	// A pending delayed reset must not survive into the next swing, or it would snap the
	// attacker home mid-attack. The reset below covers the same ground immediately.
	GetWorldTimerManager().ClearTimer(DebugAutoAttackResetTimerHandle);

	// Belt and braces. The post-attack reset normally leaves nothing to do here, but an ability
	// that is cancelled or interrupted may never end cleanly, and this preserves the guarantee
	// that every swing starts from an identical transform.
	ReturnToDebugAutoAttackHome();

	// After the reset, so the turn starts from the placed yaw the reset just restored, and
	// before the press, so the windup is already closing the angle rather than starting a frame
	// late. In Always mode this is also what establishes the focus in the first place -- a
	// placed dummy is possessed before the player pawn exists, so there is nothing to aim at
	// until the first swing comes round.
	UpdateDebugFacingFocus(/*bAttacking=*/true);

	OnAbilityInputPressed(DebugAutoAttackInputTag);

	if (DebugAutoAttackHoldSeconds <= 0.0f)
	{
		DebugAutoAttackRelease();
		return;
	}

	// Held rather than tapped, because how long the button stays down is what selects the
	// tier -- releasing immediately would make the dummy incapable of anything but a light.
	GetWorldTimerManager().SetTimer(
		DebugAutoAttackReleaseTimerHandle,
		this,
		&ATDCombatCharacter::DebugAutoAttackRelease,
		DebugAutoAttackHoldSeconds,
		false);
}

void ATDCombatCharacter::DebugAutoAttackRelease()
{
	OnAbilityInputReleased(DebugAutoAttackInputTag);
}

void ATDCombatCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	for (const TPair<TObjectPtr<UInputAction>, FGameplayTag>& Binding : AbilityInputActions)
	{
		if (!Binding.Key || !Binding.Value.IsValid())
		{
			continue;
		}

		EnhancedInput->BindAction(Binding.Key, ETriggerEvent::Started, this, &ATDCombatCharacter::OnAbilityInputPressed, Binding.Value);
		EnhancedInput->BindAction(Binding.Key, ETriggerEvent::Completed, this, &ATDCombatCharacter::OnAbilityInputReleased, Binding.Value);
	}
}

void ATDCombatCharacter::GatherAbilitiesForInput(const FGameplayTag& InputTag, TArray<FGameplayAbilitySpecHandle>& OutHandles) const
{
	if (!AbilitySystem || !InputTag.IsValid())
	{
		return;
	}

	// Collect handles rather than acting inside the loop: activating an ability can
	// modify the spec list, and the specs would move underneath the iterator.
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		const UTDGameplayAbility* Ability = Cast<UTDGameplayAbility>(Spec.Ability);
		if (Ability && Ability->InputTag.MatchesTagExact(InputTag))
		{
			OutHandles.Add(Spec.Handle);
		}
	}
}

bool ATDCombatCharacter::TryActivateAbilitiesForInput(const FGameplayTag& InputTag, bool bForwardToActive)
{
	if (!AbilitySystem)
	{
		return false;
	}

	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	bool bActivated = false;

	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		if (!Spec)
		{
			continue;
		}

		// Marks the spec as held before activating, which is the state WaitInputRelease reads
		// -- so an ability starts life knowing the button is down and holds keep working.
		// Skipped for anything already running on a retry: see bForwardToActive.
		if (bForwardToActive || !Spec->IsActive())
		{
			AbilitySystem->AbilitySpecInputPressed(*Spec);
		}

		if (!Spec->IsActive())
		{
			bActivated |= AbilitySystem->TryActivateAbility(Handle);
		}
	}

	return bActivated;
}

void ATDCombatCharacter::ReleaseAbilitiesForInput(const FGameplayTag& InputTag)
{
	if (!AbilitySystem)
	{
		return;
	}

	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		if (FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle))
		{
			AbilitySystem->AbilitySpecInputReleased(*Spec);
		}
	}
}

bool ATDCombatCharacter::ShouldBufferInput(const FGameplayTag& InputTag) const
{
	if (InputBufferSeconds <= 0.0f || !AbilitySystem)
	{
		return false;
	}

	const FGameplayAbilityActorInfo* ActorInfo = AbilitySystem->AbilityActorInfo.Get();

	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	// Asked of the abilities rather than decided here, so the rule sits next to the flag that
	// creates it. Any one of them wanting the press remembered is enough.
	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		const UTDGameplayAbility* Ability = Spec ? Cast<UTDGameplayAbility>(Spec->Ability) : nullptr;
		if (Ability && Ability->ShouldBufferFailedInput(ActorInfo))
		{
			return true;
		}
	}

	return false;
}

void ATDCombatCharacter::OnAbilityInputPressed(FGameplayTag InputTag)
{
	// A live edge always beats a recorded one. Without this, a replay still scheduled from an
	// earlier buffered press would land on whatever ability is running by the time it fires --
	// releasing a hold the player is in the middle of.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BufferedReleaseTimerHandle);
	}

	// Any new press supersedes a buffered one, whether or not this press succeeds and whatever
	// it was. Pressing something else says you have stopped waiting on the last thing -- and
	// without this a dodge buffered into a lockout could still surface after an attack the
	// player chose instead of it. Pressing the *same* thing again is unremarkable and is only
	// cleared here so it cannot outlive the press that replaced it.
	if (BufferedInput.IsSet())
	{
		if (!BufferedInput.InputTag.MatchesTagExact(InputTag))
		{
			TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: dropped, superseded by %s"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f,
				*BufferedInput.InputTag.ToString(),
				*InputTag.ToString());
		}

		BufferedInput.Clear();
	}

	if (TryActivateAbilitiesForInput(InputTag, /*bForwardToActive=*/true))
	{
		return;
	}

	if (!ShouldBufferInput(InputTag))
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// One slot, last press wins. A queue would replay stale intent as a burst: press dodge
	// then attack into a lockout and you would get both, in an order you had already stopped
	// asking for. Only the most recent press is still something the player wants.
	const float Now = World->GetTimeSeconds();

	BufferedInput.Clear();
	BufferedInput.InputTag = InputTag;
	BufferedInput.PressWorldTime = Now;
	BufferedInput.ExpiryWorldTime = Now + InputBufferSeconds;

	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: stored"), Now, *InputTag.ToString());
}

void ATDCombatCharacter::OnAbilityInputReleased(FGameplayTag InputTag)
{
	// Same reason as the press: a real release makes any pending replay redundant at best.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BufferedReleaseTimerHandle);
	}

	ReleaseAbilitiesForInput(InputTag);

	// Recorded rather than acted on. The buffer needs this edge because the attack ladder
	// resolves its tier from whether the button is still down at each checkpoint: a press
	// replayed as though it were still held would run past every one of them and turn a tap
	// into a charged heavy. It is also what starts the window counting down -- until now the
	// press was a held button, which is live intent rather than something to expire.
	const UWorld* World = GetWorld();
	if (!World || !BufferedInput.IsSet() || BufferedInput.bReleased)
	{
		return;
	}

	if (!BufferedInput.InputTag.MatchesTagExact(InputTag))
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	BufferedInput.bReleased = true;
	BufferedInput.HoldSeconds = FMath::Max(0.0f, Now - BufferedInput.PressWorldTime);
	BufferedInput.ExpiryWorldTime = Now + InputBufferSeconds;

	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: released after %.0fms held"),
		Now, *InputTag.ToString(), BufferedInput.HoldSeconds * 1000.0f);
}

void ATDCombatCharacter::ReplayBufferedRelease(FGameplayTag InputTag)
{
	ReleaseAbilitiesForInput(InputTag);

	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: replayed release"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *InputTag.ToString());
}

void ATDCombatCharacter::TickInputBuffer()
{
	if (!BufferedInput.IsSet())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// A button still down is not a stale input, so it never expires -- the same
	// push-the-deadline-forward idiom the regen pause uses. This is what makes a buffered
	// heavy reachable at all: its 200 ms boundary is past any window this size, so every
	// tier above light necessarily comes from a hold that outlives the window.
	if (!BufferedInput.bReleased)
	{
		BufferedInput.ExpiryWorldTime = FMath::Max(BufferedInput.ExpiryWorldTime, Now + InputBufferSeconds);
	}

	if (Now >= BufferedInput.ExpiryWorldTime)
	{
		TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: expired, %.0fms after press"),
			Now, *BufferedInput.InputTag.ToString(), (Now - BufferedInput.PressWorldTime) * 1000.0f);
		BufferedInput.Clear();
		return;
	}

	// Retried every frame rather than woken by an event. Every reason an activation can be
	// refused -- a blocking tag, a live instance, an ability ending, the airborne check --
	// would otherwise have to be enumerated and hooked, and missing one fails silently.
	// Polling cannot be incomplete, and there is at most one buffered input to retry.
	if (!TryActivateAbilitiesForInput(BufferedInput.InputTag, /*bForwardToActive=*/false))
	{
		return;
	}

	const FGameplayTag FiredTag = BufferedInput.InputTag;
	const bool bWasReleased = BufferedInput.bReleased;
	const float HoldSeconds = BufferedInput.HoldSeconds;
	const float LateBySeconds = Now - BufferedInput.PressWorldTime;

	BufferedInput.Clear();

	if (!bWasReleased)
	{
		// Still held, so the live release edge arrives on its own and the hold is measured
		// from activation. The time held before then is deliberately not credited: the windup
		// is preset, and crediting it would land the attack sooner than its tier is authored
		// to take.
		TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: fired %.0fms late, still held"),
			Now, *FiredTag.ToString(), LateBySeconds * 1000.0f);
		return;
	}

	// The button came up before anything could answer it, so replay that edge at the offset it
	// really had. Releasing at once instead would flatten every buffered hold to the shortest
	// branch -- a 236ms hold, a heavy by every rule the ladder has, came out a light before
	// this existed. The windup still runs its full preset length from activation; only the
	// *tier* is carried across, never the time already spent holding.
	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: fired %.0fms late, replaying release at +%.0fms"),
		Now, *FiredTag.ToString(), LateBySeconds * 1000.0f, HoldSeconds * 1000.0f);

	if (HoldSeconds <= KINDA_SMALL_NUMBER)
	{
		ReplayBufferedRelease(FiredTag);
		return;
	}

	World->GetTimerManager().SetTimer(
		BufferedReleaseTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, FiredTag]() { ReplayBufferedRelease(FiredTag); }),
		HoldSeconds,
		false);
}

float ATDCombatCharacter::GetHealth() const
{
	return AttributeSet ? AttributeSet->GetHealth() : 0.0f;
}

float ATDCombatCharacter::GetMaxHealth() const
{
	return AttributeSet ? AttributeSet->GetMaxHealth() : 0.0f;
}

float ATDCombatCharacter::GetStamina() const
{
	return AttributeSet ? AttributeSet->GetStamina() : 0.0f;
}

float ATDCombatCharacter::GetMaxStamina() const
{
	return AttributeSet ? AttributeSet->GetMaxStamina() : 0.0f;
}

float ATDCombatCharacter::GetHealthPercent() const
{
	const float Max = GetMaxHealth();
	return (Max > 0.0f) ? GetHealth() / Max : 0.0f;
}

float ATDCombatCharacter::GetStaminaPercent() const
{
	const float Max = GetMaxStamina();
	return (Max > 0.0f) ? GetStamina() / Max : 0.0f;
}

void ATDCombatCharacter::SetAimAssistHoming(const FTDAttackHitbox& InWedge, const FGameplayTagContainer& InImmunityTags, bool bActive, bool bInDrawDebug)
{
	AimAssistWedge = InWedge;
	AimAssistImmunityTags = InImmunityTags;
	bAimAssistHoming = bActive && InWedge.IsEnabled();
	bAimAssistDrawDebug = bInDrawDebug;

	// **Traced because this wedge is what the debug draw shows, and a wedge cannot be read by eye.**
	// A whole session was lost to exactly that: the drawn volume was branch 0's for every tier, and
	// judging its radius visually made two never-observed values look authored. Sizes are the one
	// thing a viewport is bad at, so the number belongs in the log beside the ESCALATE line that
	// changes it. Reach of 0 prints as the disable, which is the transition worth seeing.
	TD_TIMING_LOG(TEXT("[%.3f] AIM WEDGE  %s  reach=%.0f arc=%.0f homing=%d"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		InWedge.MaxReachCm,
		InWedge.ArcDegrees,
		bAimAssistHoming ? 1 : 0);
}

AActor* ATDCombatCharacter::FindAimAssistTarget(
	const AActor* Attacker,
	float AimYawDegrees,
	const FTDAttackHitbox& Wedge,
	const FGameplayTagContainer& ImmunityTags,
	float& OutBearingDegrees)
{
	const UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	if (!World || !Wedge.IsEnabled())
	{
		return nullptr;
	}

	const FVector Origin = Attacker->GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(Wedge.GetBroadPhaseRadiusCm()),
		FCollisionQueryParams(SCENE_QUERY_STAT(TDAimAssist), /*bTraceComplex=*/false, Attacker));

	AActor* Best = nullptr;
	float BestBearing = 0.0f;
	float BestDistance = 0.0f;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Candidate = Overlap.GetActor();
		const ACharacter* CandidateCharacter = Cast<ACharacter>(Candidate);
		const UCapsuleComponent* Capsule = CandidateCharacter ? CandidateCharacter->GetCapsuleComponent() : nullptr;
		if (!Capsule || Candidate == Attacker)
		{
			continue;
		}

		// The same list damage uses, never a second one. Steering onto something that cannot be
		// hurt would make the dodge stronger than designed by dragging the attacker onto the one
		// target it cannot touch.
		if (!ImmunityTags.IsEmpty())
		{
			const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate);
			if (TargetASC && TargetASC->HasAnyMatchingGameplayTags(ImmunityTags))
			{
				continue;
			}
		}

		const FVector TargetCentre = Candidate->GetActorLocation();
		const float TargetRadius = Capsule->GetScaledCapsuleRadius();

		if (!Wedge.OverlapsCapsule(Origin, AimYawDegrees, TargetCentre, TargetRadius, Capsule->GetScaledCapsuleHalfHeight()))
		{
			continue;
		}

		float Bearing = 0.0f;
		float HalfArc = 0.0f;
		if (!Wedge.GetBearingToCapsule(Origin, AimYawDegrees, TargetCentre, TargetRadius, Bearing, HalfArc))
		{
			continue;
		}

		const float Distance = FVector::Dist2D(TargetCentre, Origin);

		const bool bBetter = !Best
			|| FMath::Abs(Bearing) < FMath::Abs(BestBearing) - KINDA_SMALL_NUMBER
			|| (FMath::IsNearlyEqual(FMath::Abs(Bearing), FMath::Abs(BestBearing)) && Distance < BestDistance);

		if (bBetter)
		{
			Best = Candidate;
			BestBearing = Bearing;
			BestDistance = Distance;
		}
	}

	OutBearingDegrees = Best ? BestBearing : 0.0f;
	return Best;
}

bool ATDCombatCharacter::GetFacingHomingYaw(float& OutYaw) const
{
	if (!bAimAssistHoming || bDead)
	{
		return false;
	}

	// Re-selected every tick from the camera, which is what makes steering work as *choosing*
	// rather than as fighting: aim at someone else and they become the target.
	const float AimYaw = GetAimYawDegrees();

	float Bearing = 0.0f;
	const AActor* Target = FindAimAssistTarget(this, AimYaw, AimAssistWedge, AimAssistImmunityTags, Bearing);
	if (!Target)
	{
		return false;
	}

	// World yaw straight to the target, so the body ends up dead on rather than merely inside a
	// tolerance -- the wedge is the margin of error, and it is aimed rather than corrected into.
	const FVector Delta = Target->GetActorLocation() - GetActorLocation();
	OutYaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	return true;
}
