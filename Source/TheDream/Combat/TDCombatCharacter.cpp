// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDCombatCharacter.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Combat/Attributes/TDAttributeSet.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "Combat/Abilities/TDBlockAbility.h"
#include "Combat/Abilities/TDParryAbility.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "Core/TDPlayerState.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ATDCombatCharacter::ATDCombatCharacter()
{
	// The fallback pair, used only when this character has no PlayerState -- the training dummy. A
	// player builds these too and ignores them in favour of its PlayerState's. The subobject names
	// are unchanged from when this was the only ASC, so both Blueprints keep resolving templates.
	OwnedAbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	OwnedAbilitySystemComponent->SetIsReplicated(true);

	// Mixed: full effect replication to the owning client, minimal to everyone else.
	OwnedAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	OwnedAttributeSet = CreateDefaultSubobject<UTDAttributeSet>(TEXT("AttributeSet"));

	// Guard, dodge, get-up attack, neutral stand. Requested rather than referenced from a native
	// tag table: these are the abilities' authored InputTags, and resolving them by name keeps this
	// order editable beside them rather than compiled against a second list.
	KnockdownGetUpPriority = {
		FGameplayTag::RequestGameplayTag(TEXT("InputTag.Block"), false),
		FGameplayTag::RequestGameplayTag(TEXT("InputTag.Dodge"), false),
		FGameplayTag::RequestGameplayTag(TEXT("InputTag.Attack"), false),
		FGameplayTag::RequestGameplayTag(TEXT("InputTag.Jump"), false),
	};

	// The pack's own Sword / Shield sockets, on hand_r and hand_l, carrying the grip rotation and a
	// non-uniform scale that corrects meshes authored several times too large. Attach here and both
	// props are right at identity.
	//
	// Not the weapon_r / weapon_l bones: absent from Epic's SKM_Manny_Simple, and animated only by
	// GDH clips, so under any Epic animation the props freeze at reference pose.
	//
	// Cosmetic only. Collision is off because the melee trace is UAbilityTask_MeleeTrace's job -- a
	// prop that could block or overlap would let the mesh quietly decide reach.
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

		// Same shape, separate state: a successful block's lockout runs independently of a broken
		// guard's stun and neither ends the other. They cannot start together -- the melee ability
		// picks one -- but a break landing during a running blockstun must not clear it early.
		if (bInBlockstun)
		{
			const UWorld* World = GetWorld();
			if (World && World->GetTimeSeconds() >= BlockstunEndsAt)
			{
				EndBlockstun();
			}
		}

		// The third of the family. Hitstun and blockstun cannot start from the same hit -- the
		// melee ability's blocked/unblocked fork picks exactly one -- but they can overlap across
		// two hits, and each runs to its own deadline.
		if (bInHitstun)
		{
			const UWorld* World = GetWorld();
			if (World && World->GetTimeSeconds() >= HitstunEndsAt)
			{
				EndHitstun();
			}
		}

		// The ninth of the family, and the only one that is a small state machine rather than a
		// deadline: lockout, input window, rise and stand are four boundaries under one tag.
		TickKnockdown();

		// Server-side, so the turn replicates as ordinary actor rotation. Unlike the facing *lock*,
		// which is local input suppression, this rotates a real body -- driving it on both machines
		// would have the client fighting the correction it is being sent.
		TickForcedFacing(DeltaSeconds);

		// The tenth of the family. Externally inflicted and total; it composes with a recovery by
		// the standing schema, in which a lockout overrides one.
		if (bInParryLockout)
		{
			const UWorld* World = GetWorld();
			if (World && World->GetTimeSeconds() >= ParryLockoutEndsAt)
			{
				EndParryLockout();
			}
		}

		// The parry window, same timestamp shape as the three above. Its close is *not* a plain
		// expiry, though: closing charges the whiff recovery, so this is where a missed read starts
		// paying for itself. See CloseParryWindow.
		if (bParryWindowOpen)
		{
			const UWorld* World = GetWorld();
			if (World && World->GetTimeSeconds() >= ParryWindowEndsAt)
			{
				CloseParryWindow(ETDParryCloseReason::Expired);
			}
		}

		// Parry Grace, and it is the simplest state on this character by design: no tag, no lock,
		// nothing to unwind. It expires and that is all. See ParryGraceSeconds.
		if (bInParryGrace)
		{
			const UWorld* World = GetWorld();
			if (World && World->GetTimeSeconds() >= ParryGraceEndsAt)
			{
				EndParryGrace();
			}
		}

		// And the recovery the close produces. Separate state from the window: the window is 300 ms
		// and the recovery twice that, so neither derives from the other. Its expiry also ends
		// GA_Parry, which stayed alive across the recovery to hold the movement lock.
		if (bInParryRecovery)
		{
			const UWorld* World = GetWorld();
			if (World && World->GetTimeSeconds() >= ParryRecoveryEndsAt)
			{
				EndParryRecovery();
			}
		}

		// The dodge's own tail, which forbids only a parry -- its own tag rather than the parry
		// recovery's, which refuses everything.
		if (bInDodgeRecovery)
		{
			const UWorld* World = GetWorld();
			if (World && World->GetTimeSeconds() >= DodgeRecoveryEndsAt)
			{
				EndDodgeRecovery();
			}
		}

		// The on-hit waiver's movement half, deliberately last: it only ever *returns* control, so
		// it can run beside anything above without ordering mattering.
		if (bOnHitMovementWaiverPending)
		{
			const UWorld* World = GetWorld();
			if (World && World->GetTimeSeconds() >= OnHitMovementWaiverAt)
			{
				bOnHitMovementWaiverPending = false;
				SetAbilityMovementLocked(false);

				TD_TIMING_LOG(TEXT("[%.3f] MOVE UNLOCK %s  (on-hit waiver)"),
					World->GetTimeSeconds(),
					*GetName());
			}
		}
	}

	// A guard does not survive leaving the ground. Keyed to the falling *state* rather than to
	// having jumped, so walking off a ledge drops it too -- the opposite of the jump regen pause
	// beside it, which keys on the action because it charges for a choice.
	//
	// bBlockedWhileAirborne on GA_Block covers only raising one: activation gates cannot end
	// something already running.
	if (IsBlocking() && GetCharacterMovement() && GetCharacterMovement()->IsFalling())
	{
		CancelBlockAbility();
	}

	// Not authority-gated: the speed cap is local presentation of a local state, and the movement
	// component is already client-predicted.
	TickMoveSpeedClamps();

	// Before the resume, so a guard that has just finished a held-back release does not get raised
	// again in the same frame by an input the player has already let go of.
	if (const UWorld* World = GetWorld())
	{
		TickBlockCommitment(World->GetTimeSeconds());
	}

	// **The floor takes the held input first, and the resume is suppressed while it does.** Both
	// would answer a held guard during the input window, from two different mechanisms in the same
	// frame -- and which won would be decided by whichever ticked first, an ordering nobody
	// authored. One road down, so the priority below is the only thing that decides.
	if (bKnockedDown)
	{
		TickKnockdownGetUpFromHeldInput();
	}
	// Deliberately after the airborne cancel above, so a resume requested by landing is evaluated
	// against a frame in which the guard has already been taken down rather than one where the two
	// are fighting.
	else
	{
		TickResumeHeldAbilities();
	}

	// Not authority-gated: a buffered press is local input waiting to be spent, and it is
	// spent through the same path a live press takes.
	TickInputBuffer();

#if ENABLE_DRAW_DEBUG
	// Same gate as the damage wedge (bDrawDebugTrace || the cvar), so the two appear together.
	//
	// Drawn from the aim yaw, the frame the wedge is tested in. Drawing it on the body would
	// diverge from the tested volume the moment homing turns the body -- a debug view lying exactly
	// when the system acts.
	if (bAimAssistHoming && (bAimAssistDrawDebug || TDShouldDrawMeleeTrace()))
	{
		FTDAttackHitbox Drawn = AimAssistWedge;
		Drawn.MaxReachCm = FMath::Max(0.0f, AimAssistWedge.MaxReachCm - FVector::Dist2D(GetActorLocation(), AimAssistOrigin));
		Drawn.DrawDebug(GetWorld(), GetActorLocation(), GetAimYawDegrees(), FColor::Cyan);
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

	// A broken guard suppresses regen for the stun, then the ordinary pause runs on from there --
	// which falls out of the max-push above rather than needing sequencing. Pushing
	// StaminaRegenPauseSeconds rather than the stun's length is the trick: while the stun is live
	// the resume time keeps moving to "half a second from now".
	if (bGuardBroken)
	{
		RegenSuppressedUntil = FMath::Max(RegenSuppressedUntil, Now + StaminaRegenPauseSeconds);
		bSuppressorActive = true;
	}

	// Knockdown suppresses regen -- unless you are already exhausted.
	//
	// The exception prices a vortex out. Every other suppressor is bounded: an action pause is
	// self-inflicted and ends, and the guard break's holds only until the stun does, because every
	// break exhausts you and an exhausted player cannot raise a guard to be broken again. Knockdown
	// is the first an opponent can refresh indefinitely, and a player who can neither act nor
	// recover is in a cutscene rather than a combat state.
	//
	// So the rule is by class, not by name: refreshable suppression does not bind the exhausted.
	// Self-inflicted pauses still bind, and so does the break's bounded one.
	if (bKnockedDown && !bExhausted)
	{
		RegenSuppressedUntil = FMath::Max(RegenSuppressedUntil, Now + StaminaRegenPauseSeconds);
		bSuppressorActive = true;
	}

	if (GetStamina() >= GetMaxStamina())
	{
		return;
	}

	// Exhaustion does not bypass the pause. A bypass would close the bounded cases along with the
	// unbounded one, so a dodge that exhausted you would regenerate during its own duration. The
	// pause is a cost of acting, and being exhausted is not a refund.
	//
	// The unbounded case is not a deadlock: a player may hold block at zero, it accomplishes nothing
	// since anything blocked breaks the guard, and it suppresses only their own regen. Releasing is
	// always available, so it is a state chosen rather than one trapped in.
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

	// Floors at zero and stays there. Drain can never break a guard -- the line between the two
	// stamina mechanisms, enforced here by not asking. A guard held at zero has stopped being able
	// to absorb anything, which an attacker must come and collect.
	//
	// The attribute set clamps to [0, Max], so no floor is applied here.
	//
	// Flagged across the write so HandleStaminaChanged can tell this from every other way the bar
	// empties. See bApplyingBlockDrain.
	bApplyingBlockDrain = true;
	AbilitySystem->ApplyModToAttribute(
		UTDAttributeSet::GetStaminaAttribute(),
		EGameplayModOp::Additive,
		-BlockDrainPerSecond * DeltaSeconds);
	bApplyingBlockDrain = false;
}

void ATDCombatCharacter::CancelBlockAbility()
{
	if (!AbilitySystem)
	{
		return;
	}

	// Not CancelAbilities(&BlockingTags): that matches an ability's *asset* tags
	// (Ability.Defend.Block), while BlockingTag is State.Blocking, granted through
	// ActivationOwnedTags. The two sets are unrelated, so every match fails silently and every
	// caller cancels nothing while looking correct.
	//
	// Matched on the ability's type rather than a second tag, so the block's identity has no third
	// place to drift. The cost: a Blueprint-only guard not deriving from UTDBlockAbility is missed.
	TArray<FGameplayAbilitySpecHandle> ToCancel;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->IsA<UTDBlockAbility>())
		{
			ToCancel.Add(Spec.Handle);
		}
	}

	// Collected first: cancelling inside the loop can reallocate the ASC's live spec array.
	for (const FGameplayAbilitySpecHandle& Handle : ToCancel)
	{
		AbilitySystem->CancelAbilityHandle(Handle);
	}
}

void ATDCombatCharacter::TickMoveSpeedClamps()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || DefaultMaxWalkSpeed <= 0.0f)
	{
		return;
	}

	// Recomputed every tick from current state rather than on the ability's edges. An edge-driven
	// version must restore on every exit path, and stranding the slow speed is both easy and
	// invisible -- a character walking at a quarter speed forever looks like a tuning mistake.
	//
	// The slowest live cap wins, and the overlap is reachable: raising a guard you cannot afford
	// exhausts you with the guard still up. Taking the minimum is the only combination that cannot
	// be gamed by entering the states in a particular order.
	float Target = DefaultMaxWalkSpeed;
	if (IsBlocking())
	{
		Target = FMath::Min(Target, BlockingMaxWalkSpeed);
	}
	if (bExhausted)
	{
		Target = FMath::Min(Target, ExhaustedMaxWalkSpeed);
	}

	if (!FMath::IsNearlyEqual(Movement->MaxWalkSpeed, Target))
	{
		Movement->MaxWalkSpeed = Target;
	}
}

void ATDCombatCharacter::HandleAbilityEndedForResume(const FAbilityEndedData& EndedData)
{
	// Requested, never performed here. OnAbilityEnded fires synchronously inside EndAbility, which
	// makes this re-entrant: raising a block cancels the attack, the attack's end re-enters while
	// block is still mid-activation, block's spec does not read active yet, and block activates a
	// second time. The spec's activeCount leaks to 2, one release brings it to 1, and the guard is
	// stuck up forever -- which also stops block ever activating again, so it stops cancelling
	// attacks too.
	//
	// Deferring to the next tick makes the re-entrancy unrepresentable rather than guarded against.
	bResumePending = true;
}

void ATDCombatCharacter::TickKnockdownGetUpFromHeldInput()
{
	// The lockout answers nothing: that span is the floor's cost, and admitting a held input inside
	// it would price the whole down state at one button.
	if (!AbilitySystem || !IsInKnockdownInputWindow())
	{
		return;
	}

	// **Priority, not preference.** Ordered by what an unwanted selection costs across both ledgers
	// -- the bar and the exposure. A guarded rise is 15 and releasable at once; a dodge is 50 and
	// commits a trajectory, though its i-frames leave you safe; the get-up attack is free on the bar
	// and the worst of the three, because it is committed from activation and guarantees no
	// follow-up. The neutral stand is last for a different reason: it is the default, and a default
	// that outranked a deliberate choice would rob you of the choice.
	//
	// **Read the order, not the stamina.** The attack costs nothing on the bar and still loses to
	// both defensive options.
	for (const FGameplayTag& InputTag : KnockdownGetUpPriority)
	{
		if (!InputTag.IsValid() || !IsInputHeldForAbility(InputTag))
		{
			continue;
		}

		// First held option that will actually activate wins, and one activation ends the search:
		// the rise clears the input window, so nothing else can follow it this frame or any other.
		if (TryActivateAbilitiesForInput(InputTag, /*bForwardToActive=*/false))
		{
			TD_TIMING_LOG(TEXT("[%.3f] KNOCKDOWN  %s rose on held %s"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetName(), *InputTag.ToString());
			return;
		}
	}
}

bool ATDCombatCharacter::IsInputHeldForAbility(const FGameplayTag& InputTag) const
{
	if (!AbilitySystem)
	{
		return false;
	}

	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		// InputPressed rather than a stored press: it is marked even when activation is refused and
		// cleared on the real release edge, which is exactly "the button is down now". A buffered
		// press would answer "it was down recently", which is the buffer's question, not this one.
		const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		if (Spec && Spec->InputPressed && !Spec->IsActive())
		{
			return true;
		}
	}

	return false;
}

void ATDCombatCharacter::TickResumeHeldAbilities()
{
	if (!bResumePending || !AbilitySystem)
	{
		return;
	}

	// Nothing resumes while anything else is running -- the whole rule, not a safety check. A guard
	// displaced by an attack means the attack is still going when block's end requests the resume;
	// resuming immediately puts the guard back a frame later, and the guard cancels attacks, so the
	// swing dies a frame after it started.
	//
	// The specification is that the guard comes back after recovery ends, which is this condition,
	// because recovery ending is the ability ending.
	//
	// Left pending rather than consumed when skipping. The attack's own end will request again, but
	// relying on that makes correctness depend on which events happen to fire. It also means a guard
	// blocked by exhaustion comes up the instant exhaustion lifts.
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (Spec.IsActive())
		{
			return;
		}
	}

	// A held button is a continuous statement of intent, so an ability that *is* a held state comes
	// back when whatever interrupted it finishes. Opt-in per ability -- see bResumeWhileInputHeld --
	// because the general form turns a held attack button into auto-repeat.
	//
	// Collected before activating: activating inside the loop can reallocate the ASC's spec array.
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
		// Re-checked rather than trusted from the gather above: anything activated earlier in this
		// same loop can have changed it, and a double activation is the specific failure this whole
		// deferral exists to prevent.
		const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		if (!Spec || Spec->IsActive())
		{
			continue;
		}

		// Goes through CanActivateAbility like any press, so exhaustion, a broken guard or being
		// airborne refuse it as they would a fresh one. Nothing here needs to know which.
		//
		AbilitySystem->TryActivateAbility(Handle);
	}

	// Cleared only once nothing is still waiting. Assigning false before the attempt above would let
	// a refused resume consume the request and never retry, so the guard promised above -- one
	// blocked by exhaustion coming up the instant exhaustion lifts -- would not arrive.
	//
	// The ordinary path is the exhausted guard force-ending at its commitment. Retrying costs a
	// refused activation per tick, which is what REFUSED's dedupe was built for.
	bool bStillWaiting = false;
	for (const FGameplayAbilitySpecHandle& Handle : Resumable)
	{
		const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		if (Spec && !Spec->IsActive() && Spec->InputPressed)
		{
			bStillWaiting = true;
			break;
		}
	}

	bResumePending = bStillWaiting;
}

bool ATDCombatCharacter::IsBlocking() const
{
	return AbilitySystem && BlockingTag.IsValid() && AbilitySystem->HasMatchingGameplayTag(BlockingTag);
}

bool ATDCombatCharacter::IsBlockCommitted() const
{
	return AbilitySystem && AbilitySystem->HasMatchingGameplayTag(TDTags::State_Blocking_Committed);
}

void ATDCombatCharacter::BeginBlockCommitment()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// A resume is an intended block, and all blocks are created equal -- governing the initial cost
	// as well as this; see BlockInitialStaminaCost.
	//
	// Exempting resumed guards makes durations bimodal: 250 ms when pressed, 50-70 ms when resumed,
	// so rapid tapping still produces sub-minimum guards at a slower cadence. A floor with an
	// exemption is not a floor.
	//
	// Assigned rather than maxed: a guard raised again is a new guard and gets a full commitment,
	// which stops a player shortening their own floor by tapping through it.
	BlockCommitEndsAt = World->GetTimeSeconds() + MinimumBlockSeconds;

	// Applied here rather than left to the next tick. The tick maintains this tag, but a tick is a
	// frame, and in that frame an attack can still activate and cancel the guard. A commitment
	// enforced one frame late is not enforced at the moment it matters most.
	if (AbilitySystem && !AbilitySystem->HasMatchingGameplayTag(TDTags::State_Blocking_Committed))
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_Blocking_Committed);
	}
}

void ATDCombatCharacter::PayBlockInitialCost()
{
	if (!HasAuthority() || !AbilitySystem || BlockInitialStaminaCost <= 0.0f)
	{
		return;
	}

	// Not flagged as drain, unlike TickBlockDrain: drain is continuous and cannot exhaust you, while
	// this is a one-off cost, and a cost that empties the bar exhausts you exactly as a dodge's
	// does. So it reaches EnterExhaustion by the ordinary route.
	AbilitySystem->ApplyModToAttribute(
		UTDAttributeSet::GetStaminaAttribute(),
		EGameplayModOp::Additive,
		-BlockInitialStaminaCost);

	TD_TIMING_LOG(TEXT("[%.3f] BLOCK      cost %.0f on %s  remaining=%.1f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		BlockInitialStaminaCost,
		*GetName(),
		GetStamina());
}

void ATDCombatCharacter::TickBlockCommitment(float Now)
{
	if (!AbilitySystem)
	{
		return;
	}

	// The tag is a description of the current state, recomputed every frame, so no exit path can
	// strand it. A stuck commit tag refuses attacking, dodging and jumping indefinitely with nothing
	// on screen to say why.
	const bool bShouldBeCommitted = IsBlocking() && Now < BlockCommitEndsAt;
	const bool bIsCommitted = AbilitySystem->HasMatchingGameplayTag(TDTags::State_Blocking_Committed);

	if (bShouldBeCommitted && !bIsCommitted)
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_Blocking_Committed);
	}
	else if (!bShouldBeCommitted && bIsCommitted)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_Blocking_Committed);
	}

	if (bShouldBeCommitted)
	{
		return;
	}

	// An exhausted guard ends the instant its commitment expires, held button or not. It follows
	// from two rules in force: you cannot block while exhausted, and all blocks are created equal.
	// Raising a guard you cannot afford is allowed, charges its cost, exhausts you -- and then owes
	// the full commitment, because exempting it is the exemption that made the floor bimodal.
	//
	// So the commitment is the only thing keeping this guard up, and when it lapses the ordinary
	// refusal takes over. Cancelled rather than released: a release would be the player's, and this
	// is the system taking something back.
	//
	// After the tag maintenance above, so the commit tag is gone when the guard drops.
	if (bExhausted && IsBlocking())
	{
		TD_TIMING_LOG(TEXT("[%.3f] BLOCK down %s (exhausted)"),
			Now,
			*GetName());

		CancelBlockAbility();
		return;
	}

	// The commitment is over, so a release that arrived during it takes effect now. Found by asking
	// the live instance rather than caching a pointer, because the ability can be torn down by a
	// cancel, a guard break or going airborne without passing through here.
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (!Spec.IsActive())
		{
			continue;
		}

		for (UGameplayAbility* Instance : Spec.GetAbilityInstances())
		{
			if (UTDBlockAbility* Block = Cast<UTDBlockAbility>(Instance))
			{
				if (Block->IsReleasePending())
				{
					Block->FinishPendingRelease();
					return;
				}
			}
		}
	}
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

	// 180 degrees means the whole forward hemisphere, so the test is "not behind me". A dot product
	// rather than an angle because that is the one value needing no arc arithmetic -- if the arc
	// ever stops being 180 this becomes an authored number and should move to a UPROPERTY.
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

	// Read the bar back rather than predicting it. The attribute set clamps [0, Max] in both base
	// and current value, so "did this empty them" is a question only the clamped result answers --
	// computing it from Amount would disagree whenever anything else touches stamina in the frame.
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

	// Re-entrant deliberately: a second blocked hit during a stun extends it rather than being
	// ignored. Being hit again while broken is strictly worse than being hit once, and an early-out
	// on bGuardBroken would make the stun a window of free hits.
	GuardBreakEndsAt = World->GetTimeSeconds() + GuardBreakStunSeconds;

	// Unreachable today for the same reason blockstun's is -- breaking a guard needs a guard, and
	// GA_Parry refuses to activate while State.Blocking is present. Hooked because the rule is
	// about lockouts overriding recoveries in general, not about the paths that can fire now.
	OverrideParryRecovery(TEXT("guard break"));

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

		// The guard is gone, so the ability holding it goes too -- otherwise BlockingTag survives
		// the break and the drain keeps running on a guard the player no longer has. Cancelled
		// rather than left to end on release, because a broken guard should not wait to be noticed.
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

void ATDCombatCharacter::EnterBlockstun(float DurationSeconds)
{
	const UWorld* World = GetWorld();
	if (!World || !HasAuthority() || DurationSeconds <= 0.0f)
	{
		return;
	}

	// Extended by taking the max, never reassigned. A second blocked hit inside a running lockout
	// can only lengthen it; assigning would let a light thrown after a heavy shorten the heavy's
	// lockout, making a faster follow-up a favour to the defender.
	BlockstunEndsAt = FMath::Max(BlockstunEndsAt, World->GetTimeSeconds() + DurationSeconds);

	// Stamped on every call, including one that lands inside a running lockout: the serial is what
	// restarts the tell, and the span is measured to the extended end rather than the duration
	// passed in, so a tell always finishes with the stun it belongs to.
	++BlockstunTellSerial;
	BlockstunTellSpanSeconds = BlockstunEndsAt - World->GetTimeSeconds();
	BlockstunTellStartTime = World->GetTimeSeconds();

	// Unreachable today and hooked anyway: a parrier cannot hold a guard, since GA_Parry refuses to
	// activate while State.Blocking is present. Hooked because the rule is about lockouts overriding
	// recoveries in general, and a hook covering only today's paths is one nobody extends.
	OverrideParryRecovery(TEXT("blockstun"));

	if (!bInBlockstun)
	{
		bInBlockstun = true;
		ApplyBlockstunState();
	}
}

void ATDCombatCharacter::EndBlockstun()
{
	if (!bInBlockstun)
	{
		return;
	}

	bInBlockstun = false;
	ClearBlockstunState();
}

void ATDCombatCharacter::ApplyBlockstunState()
{
	if (AbilitySystem)
	{
		// Nothing is cancelled here, unlike the guard break. Blockstun refuses activations via
		// ActivationBlockedTags and lets whatever is running finish -- the defender keeps the guard
		// they successfully used, and never released the button.
		AbilitySystem->AddLooseGameplayTag(TDTags::State_Blockstun);
	}

	TD_TIMING_LOG(TEXT("[%.3f] BLOCKSTUN  %s  until=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		BlockstunEndsAt);
}

void ATDCombatCharacter::ClearBlockstunState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_Blockstun);
	}

	TD_TIMING_LOG(TEXT("[%.3f] BLOCKSTUN END  %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName());
}

void ATDCombatCharacter::OnRep_Blockstun()
{
	if (bInBlockstun)
	{
		ApplyBlockstunState();
	}
	else
	{
		ClearBlockstunState();
	}
}

float ATDCombatCharacter::ComputeTellTime(float StartTime, float SpanSeconds, float PortionSeconds,
	const UCurveFloat* PacingCurve) const
{
	const UWorld* World = GetWorld();
	if (!World || SpanSeconds <= 0.0f)
	{
		return 0.0f;
	}

	const float Progress = FMath::Clamp((World->GetTimeSeconds() - StartTime) / SpanSeconds, 0.0f, 1.0f);

	// The curve remaps progress through the *portion*, never past it: the portion is still the
	// whole span the tell may use, and a curve leaving 0..1 would run the playhead off the clip.
	const float Mapped = PacingCurve
		? FMath::Clamp(PacingCurve->GetFloatValue(Progress), 0.0f, 1.0f)
		: Progress;
	return Mapped * PortionSeconds;
}

float ATDCombatCharacter::GetHitstunTellTime() const
{
	return ComputeTellTime(HitstunTellStartTime, HitstunTellSpanSeconds, HitstunTellPortionSeconds,
		HitstunTellPacingCurve);
}

float ATDCombatCharacter::GetBlockstunTellTime() const
{
	return ComputeTellTime(BlockstunTellStartTime, BlockstunTellSpanSeconds, BlockstunTellPortionSeconds);
}

float ATDCombatCharacter::GetParryLockoutTellTime() const
{
	return ComputeTellTime(ParryLockoutTellStartTime, ParryLockoutTellSpanSeconds, ParryLockoutTellPortionSeconds);
}

void ATDCombatCharacter::OnRep_HitstunTell()
{
	// The client's clock starts when it learns of the hit, not when the server logged it. Both are
	// late by the same half round trip, so the tell and the state it draws begin together here.
	const UWorld* World = GetWorld();
	HitstunTellStartTime = World ? World->GetTimeSeconds() : 0.0f;
}

void ATDCombatCharacter::OnRep_BlockstunTell()
{
	const UWorld* World = GetWorld();
	BlockstunTellStartTime = World ? World->GetTimeSeconds() : 0.0f;
}

void ATDCombatCharacter::OnRep_ParryLockoutTell()
{
	const UWorld* World = GetWorld();
	ParryLockoutTellStartTime = World ? World->GetTimeSeconds() : 0.0f;
}

void ATDCombatCharacter::EnterHitstun(float DurationSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || DurationSeconds <= 0.0f || bDead)
	{
		return;
	}

	// Max-extended like blockstun and re-entrant like the guard break: a second hit inside a running
	// stun lengthens the sentence, never shortens it. That re-extension is the string guarantee's
	// arithmetic, each chained contact refreshing the stun before the last expires.
	HitstunEndsAt = FMath::Max(HitstunEndsAt, World->GetTimeSeconds() + DurationSeconds);

	// Blockstun's stamp, same contract -- see EnterBlockstun.
	++HitstunTellSerial;
	HitstunTellSpanSeconds = HitstunEndsAt - World->GetTimeSeconds();
	HitstunTellStartTime = World->GetTimeSeconds();

	// Being hit cancels everything, committed or not. Server-only and outside the Apply half, as
	// death's cancel is: a client's OnRep must not cancel predicted copies out from under a
	// correction. Cancelling runs each ability's EndAbility, which clears State.Attacking, restores
	// facing, tears down the lunge and resets the string -- nothing here repeats that.
	if (AbilitySystem)
	{
		AbilitySystem->CancelAllAbilities();
	}

	// The explicit reset covers the victim who was *not* mid-ability: a pending advance from an
	// earlier swing must not survive being cleanly hit. Idempotent beside the cancel path's.
	ResetString(TEXT("cleanly hit"));

	// **After the cancel, deliberately.** The cancel is what closes any open parry window and bills
	// its whiff, so overriding before it would leave the recovery to be charged a line later and
	// outlive the punishment that was supposed to supersede it.
	OverrideParryRecovery(TEXT("hitstun"));

	if (!bInHitstun)
	{
		bInHitstun = true;
		ApplyHitstunState();
	}
}

void ATDCombatCharacter::EndHitstun()
{
	if (!bInHitstun)
	{
		return;
	}

	bInHitstun = false;
	ClearHitstunState();
}

void ATDCombatCharacter::ApplyHitstunState()
{
	if (AbilitySystem)
	{
		// The tag alone from here on: the cancel happened once, on the server, in EnterHitstun.
		// This half only makes the state true on this machine, which for a client is the
		// difference between predicting an action the server already refused and knowing better.
		AbilitySystem->AddLooseGameplayTag(TDTags::State_Hitstun);
	}

	TD_TIMING_LOG(TEXT("[%.3f] HITSTUN    %s  until=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		HitstunEndsAt);
}

void ATDCombatCharacter::ClearHitstunState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_Hitstun);
	}

	TD_TIMING_LOG(TEXT("[%.3f] HITSTUN END  %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName());
}

void ATDCombatCharacter::OnRep_Hitstun()
{
	if (bInHitstun)
	{
		ApplyHitstunState();
	}
	else
	{
		ClearHitstunState();
	}
}

float ATDCombatCharacter::GetKnockdownLockoutSeconds() const
{
	return KnockdownType == ETDKnockdownType::Hard ? KnockdownLockoutSecondsHard : KnockdownLockoutSecondsNormal;
}

float ATDCombatCharacter::GetKnockdownInputWindowSeconds() const
{
	return KnockdownType == ETDKnockdownType::Hard ? KnockdownInputWindowSecondsHard : KnockdownInputWindowSecondsNormal;
}

bool ATDCombatCharacter::IsInKnockdownInputWindow() const
{
	const UWorld* World = GetWorld();
	if (!World || !bKnockedDown || bKnockdownRising)
	{
		return false;
	}
	return World->GetTimeSeconds() >= KnockdownLockoutEndsAt;
}

void ATDCombatCharacter::EnterKnockdown(ETDKnockdownType Type, AActor* Attacker)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || Type == ETDKnockdownType::None || bDead)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// Last hit wins, as the knockback slide already ruled. A knockdown on a body already down
	// restarts the clock at the new type rather than extending -- unlike hitstun, which
	// max-extends. Hitstun is a duration; this is a state machine, and extending would leave a body
	// in a lockout whose input window was computed against a different type.
	KnockdownType = Type;
	bKnockdownRising = false;
	KnockdownLockoutEndsAt = Now + GetKnockdownLockoutSeconds();
	KnockdownInputWindowEndsAt = KnockdownLockoutEndsAt + GetKnockdownInputWindowSeconds();
	KnockdownRiseEndsAt = 0.0f;
	bDebugGetUpPressed = false;

	// Cancels through the death path's funnel, and for the same reason. Server-only and outside the
	// Apply half: a client's OnRep must not cancel predicted copies out from under a correction.
	// Cancelling restores facing, tears down lunges and clears committed tags -- nothing below
	// repeats that.
	if (AbilitySystem)
	{
		AbilitySystem->CancelAllAbilities();
	}

	// A pending advance must not survive being floored: a mid-string attacker stands up to swing
	// 0. Idempotent beside whatever the cancel path already reset.
	ResetString(TEXT("knocked down"));

	// **After the cancel, deliberately** -- the cancel is what closes an open parry window and
	// bills its whiff, so overriding first would leave a recovery to be charged a line later and
	// outlive the punishment meant to supersede it. Same ordering EnterHitstun uses.
	OverrideParryRecovery(TEXT("knockdown"));

	// **Carry first, then the tag.** ApplyKnockdownState prints the bearing, and the bearing
	// is a product of the fall -- so the trace line has to come after the geometry it
	// reports, not before it.
	ApplyKnockdownFall(Attacker);
	BeginForcedFacing(Attacker);

	// Fitted to the fall, so the body reaches the floor as the displacement finishes. The clip
	// authors bEnableAutoBlendOut false, which is what holds its last frame as the ground pose for
	// the lockout and the input window that follow.
	PlayKnockdownMontage(KnockdownMontage, KnockdownFallSeconds, TEXT("fall"),
		KnockdownFallClipSeconds, KnockdownFallClipStartSeconds);

	if (!bKnockedDown)
	{
		bKnockedDown = true;
		ApplyKnockdownState();
	}
	else
	{
		// Already down and re-floored: the tag is present and correct, but the trace still owes a
		// line or the new type's clock would start invisibly.
		TD_TIMING_LOG(TEXT("[%.3f] KNOCKDOWN  %s retyped  type=%s"),
			Now, *GetName(), KnockdownType == ETDKnockdownType::Hard ? TEXT("hard") : TEXT("normal"));
	}

}

void ATDCombatCharacter::ApplyKnockdownState()
{
	if (AbilitySystem)
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_KnockedDown);
	}

	// Bearing is the assertable half: the radial axis's angle off the attacker's facing. In a 1v1
	// the two coincide near zero; in the ender's 360-degree finish the two victims diverge to about
	// plus and minus ninety, which makes "the axis radiates" observable rather than intended.
	TD_TIMING_LOG(TEXT("[%.3f] KNOCKDOWN  %s  type=%s lockout=%.3f inputWindow=%.3f rise=%.3f spacing=%.0f bearing=%.1f z=%.1f airborne=%d"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		KnockdownType == ETDKnockdownType::Hard ? TEXT("hard") : TEXT("normal"),
		GetKnockdownLockoutSeconds(),
		GetKnockdownInputWindowSeconds(),
		KnockdownRiseSeconds,
		KnockdownSpacingCm,
		LastKnockdownBearingDegrees,
		GetActorLocation().Z,
		GetCharacterMovement() && GetCharacterMovement()->IsFalling() ? 1 : 0);
}

void ATDCombatCharacter::ClearKnockdownState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_KnockedDown);
	}

	// Height at the stand, against the height at entry: **the two-point measurement that
	// makes the airborne rule checkable.** Equal heights across a fall mean the body hung
	// -- the juggling IgnoreZAccumulate exists to prevent -- and nothing else in the trace
	// could tell you.
	TD_TIMING_LOG(TEXT("[%.3f] KNOCKDOWN STAND  %s  z=%.1f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		GetActorLocation().Z);
}

void ATDCombatCharacter::OnRep_KnockedDown()
{
	if (bKnockedDown)
	{
		ApplyKnockdownState();
	}
	else
	{
		ClearKnockdownState();
	}
}

void ATDCombatCharacter::BeginKnockdownRise(const TCHAR* By, bool bPlayRiseMontage, float RiseSecondsOverride)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || !bKnockedDown || bKnockdownRising)
	{
		return;
	}

	// **Invincibility ends on this line**, before anything else happens, because every get-up
	// option prices its own rise from here on: the dodge brings i-frames, block brings a guard,
	// the attack brings a threat, and the plain stand brings nothing at all.
	bKnockdownRising = true;
	// An option that brings its own exit sets the rise's length to its own, so the knockdown
	// ends when the action does. Left shared, a shorter option finishes and then waits out the
	// difference still flagged knocked down -- movement and facing both locked, and its own
	// protection already expired.
	const float RiseSeconds = RiseSecondsOverride > 0.0f ? RiseSecondsOverride : KnockdownRiseSeconds;
	KnockdownRiseEndsAt = World->GetTimeSeconds() + RiseSeconds;
	KnockdownHomeResetAt = World->GetTimeSeconds() + KnockdownRiseSeconds;

	TD_TIMING_LOG(TEXT("[%.3f] KNOCKDOWN RISE  %s  by=%s  stands=%.3f"),
		World->GetTimeSeconds(), *GetName(), By, KnockdownRiseEndsAt);

	// **Only for exits that do not animate themselves.** The auto-rise, the neutral stand and the
	// block get-up all use the type's rise clip; the dodge brings its own roll or kip-up, and
	// playing this first would be a frame of the wrong animation before it replaced us.
	if (bPlayRiseMontage)
	{
		UAnimMontage* Rise = (KnockdownType == ETDKnockdownType::Hard) ? RiseHardMontage : RiseMontage;
		PlayKnockdownMontage(Rise, KnockdownRiseSeconds, TEXT("rise"));
	}
}

void ATDCombatCharacter::EndKnockdown()
{
	if (!bKnockedDown)
	{
		return;
	}

	bKnockedDown = false;
	bKnockdownRising = false;
	KnockdownType = ETDKnockdownType::None;
	ClearKnockdownState();

	// **The fixture's way back into the fight, and it has to be here.**
	//
	// A knockdown carries its victim to KnockdownSpacingCm, past the light's whole covered range, so
	// a body that does not walk sits permanently outside anything the ladder can reach. A human
	// walks back in; **a stationary dummy never does**, so after its first knockdown a defensive
	// fixture is out of the exchange for the rest of the session -- windows opening against attacks
	// that can never arrive, which reads as the defence failing rather than as the pawn having left.
	//
	// **The stand is the only safe moment.** The dodge fixture re-homes on its press because that
	// press precedes the attack; a parry's press is timed *into* one, and teleporting mid-swing
	// would resolve the hit at a distance nobody aimed at. Hitstun's end will not serve either --
	// a typed hit knocks down instead of stunning, so that hook never fires.
	//
	// **The moment is the shared rise mark, which the stand no longer always coincides with.**
	// An option that shortens its own rise ends the knockdown earlier, and teleporting there
	// would land inside the travel that option just made -- so the reset keeps the clock it
	// always had and the delay is whatever is left of it. Zero for every unshortened rise,
	// which is the immediate call this replaces.
	//
	// Self-guarding: ReturnToDebugAutoAttackHome never moves a player pawn.
	const UWorld* ResetWorld = GetWorld();
	const float HomeResetDelay = ResetWorld
		? FMath::Max(KnockdownHomeResetAt - ResetWorld->GetTimeSeconds(), 0.0f)
		: 0.0f;
	if (HomeResetDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			KnockdownHomeResetTimerHandle,
			this,
			&ATDCombatCharacter::ReturnToDebugAutoAttackHome,
			HomeResetDelay,
			false);
	}
	else
	{
		ReturnToDebugAutoAttackHome();
	}
}

void ATDCombatCharacter::EnterParryLockout(float LockoutSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || bDead)
	{
		return;
	}

	if (LockoutSeconds <= 0.0f)
	{
		// An authored zero means a swing whose owner owes nothing for being parried. Honoured
		// silently rather than clamped to a minimum: inventing a floor here would author the
		// reward behind the author's back, which is the whole reason there is no floor.
		return;
	}

	// Max-extended like every other lockout on this character: a second parry landing inside a
	// running one lengthens the sentence and can never shorten it.
	ParryLockoutEndsAt = FMath::Max(ParryLockoutEndsAt, World->GetTimeSeconds() + LockoutSeconds);

	// The stuns' stamp, same contract -- see EnterHitstun. Measured to the extended end, so a
	// second catch inside a running lockout lengthens the tell with the sentence.
	++ParryLockoutTellSerial;
	ParryLockoutTellSpanSeconds = ParryLockoutEndsAt - World->GetTimeSeconds();
	ParryLockoutTellStartTime = World->GetTimeSeconds();

	// **The string dies explicitly, and stays explicit.** bParried's chain gate is subsumed by the
	// ability no longer existing -- there is nothing left to chain out of -- so this is now the
	// only thing keeping "no more games" true.
	ResetString(TEXT("parried"));

	if (!bInParryLockout)
	{
		bInParryLockout = true;
		ApplyParryLockoutState();
	}
}

void ATDCombatCharacter::EndParryLockout()
{
	if (!bInParryLockout)
	{
		return;
	}

	bInParryLockout = false;
	ClearParryLockoutState();
}

void ATDCombatCharacter::ApplyParryLockoutState()
{
	if (AbilitySystem)
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_ParryLockout);
	}

	// Movement goes too, and is taken by the flag rather than by a call: IsMovementLocked reads
	// bInParryLockout, so the lock lasts exactly as long as the tag and nothing else can clear it.
	TD_TIMING_LOG(TEXT("[%.3f] PARRY LOCKOUT  %s  until=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		ParryLockoutEndsAt);
}

void ATDCombatCharacter::ClearParryLockoutState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_ParryLockout);
	}

	TD_TIMING_LOG(TEXT("[%.3f] PARRY LOCKOUT END  %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName());
}

void ATDCombatCharacter::OnRep_ParryLockout()
{
	if (bInParryLockout)
	{
		ApplyParryLockoutState();
	}
	else
	{
		ClearParryLockoutState();
	}
}

void ATDCombatCharacter::TickKnockdown()
{
	UWorld* World = GetWorld();
	if (!World || !bKnockedDown)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	if (bKnockdownRising)
	{
		// The stand. **The rise is the lockout's time to the last frame** -- no tail, because a
		// tail is what a self-inflicted action pause leaves behind and nobody chose to be here.
		if (Now >= KnockdownRiseEndsAt)
		{
			EndKnockdown();
		}
		return;
	}

	// The fixture's press, once per knockdown, just inside the input window.
	if (DebugGetUpMode != ETDDebugGetUpMode::Wait && !bDebugGetUpPressed
		&& Now >= KnockdownLockoutEndsAt + DebugGetUpDelaySeconds)
	{
		bDebugGetUpPressed = true;
		DebugGetUpPress();
	}

	// The auto-rise: the input window closed without anything being chosen. Committed, vulnerable
	// and locked from here, exactly like a chosen stand -- the difference is only who decided.
	if (Now >= KnockdownInputWindowEndsAt)
	{
		BeginKnockdownRise(TEXT("auto"));
	}
}

void ATDCombatCharacter::BeginForcedFacing(AActor* Toward)
{
	if (!Toward || Toward == this)
	{
		return;
	}

	bForcedFacingActive = true;
	ForcedFacingTarget = Toward;
	ForcedFacingStartYaw = GetActorRotation().Yaw;
}

void ATDCombatCharacter::TickForcedFacing(float DeltaSeconds)
{
	if (!bForcedFacingActive)
	{
		return;
	}

	const AActor* Target = ForcedFacingTarget.Get();
	if (!Target || bDead)
	{
		bForcedFacingActive = false;
		return;
	}

	FVector ToAttacker = Target->GetActorLocation() - GetActorLocation();
	ToAttacker.Z = 0.0f;
	if (!ToAttacker.Normalize())
	{
		// Exactly co-located: there is no bearing to turn toward and any answer would be invented.
		bForcedFacingActive = false;
		return;
	}

	const float DesiredYaw = ToAttacker.Rotation().Yaw;
	const FRotator Current = GetActorRotation();
	const float Remaining = FMath::FindDeltaAngleDegrees(Current.Yaw, DesiredYaw);
	const float StepDegrees = ForcedFacingTurnRateDegrees * DeltaSeconds;

	if (FMath::Abs(Remaining) <= StepDegrees)
	{
		SetActorRotation(FRotator(Current.Pitch, DesiredYaw, Current.Roll));
		bForcedFacingActive = false;

		// One line per hit, at the end, carrying the span actually covered -- which is what the
		// derivation needs checking against. A rate that cannot finish 180 degrees inside the
		// shortest felt hitstun is a body still turning when it regains control.
		TD_TIMING_LOG(TEXT("[%.3f] FACING FORCED  %s toward %s  span=%.1f rate=%.0f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			*GetName(),
			*GetNameSafe(Target),
			FMath::Abs(FMath::FindDeltaAngleDegrees(ForcedFacingStartYaw, DesiredYaw)),
			ForcedFacingTurnRateDegrees);
		return;
	}

	SetActorRotation(FRotator(Current.Pitch, Current.Yaw + FMath::Sign(Remaining) * StepDegrees, Current.Roll));
}


void ATDCombatCharacter::PlayKnockdownMontage(UAnimMontage* Montage, float TargetSeconds, const TCHAR* Label,
	float ClipPortionSeconds, float ClipStartSeconds)
{
	if (!Montage || TargetSeconds <= 0.0f)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = GetMesh();
	UAnimInstance* Anim = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		return;
	}

	// **The clip conforms to the duration, never the reverse** -- the standing rule for every
	// authored timing in this project. The state machine's spans are the design; the montage is
	// stretched to cover them.
	const float Length = Montage->GetPlayLength();

	// The span fitted is the portion when one is given, not the clip. What follows the portion
	// still plays, at the same rate, so a clip whose tail is a settle lands on time and then
	// settles rather than being cut. Clamped to the clip so an over-long portion cannot slow it.
	const float End = (ClipPortionSeconds > 0.0f) ? FMath::Min(ClipPortionSeconds, Length) : Length;
	const float From = FMath::Clamp(ClipStartSeconds, 0.0f, FMath::Max(End - 0.01f, 0.0f));
	const float Fitted = End - From;
	const float Rate = (Fitted > 0.0f) ? FMath::Max(Fitted / TargetSeconds, 0.01f) : 1.0f;

	const float PlayingLength =
		Anim->Montage_Play(Montage, Rate, EMontagePlayReturnType::MontageLength, From);
	if (PlayingLength <= 0.0f)
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("%s montage %s was refused by the anim instance and nothing is playing. The usual "
				 "cause is its skeleton being neither the mesh's nor listed compatible by the mesh's."),
			Label, *Montage->GetName());
	}

	// **The blend-out boundary moves with the rate, so a fast montage can blend itself out before
	// the span it was fitted to has elapsed.** Warned rather than corrected: the fix is per clip
	// (a BlendOutTriggerTime shorter than the blend-out moves the boundary later at every rate) and
	// the right value is a look decision.
	// Ungated, following StartAttackMontage's root-motion warning -- silent phase loss is the
	// failure this family of warnings exists to prevent. bEnableAutoBlendOut false exempts a clip
	// outright, which is why the knockdown's ground pose holds.
	if (Montage->bEnableAutoBlendOut)
	{
		const float Trigger = (Montage->BlendOutTriggerTime >= 0.0f)
			? Montage->BlendOutTriggerTime
			: Montage->BlendOut.GetBlendTime();
		const float BlendStartsAt = Length - (Trigger * Rate);
		if (BlendStartsAt < Length * 0.5f)
		{
			UE_LOG(LogTDCombatTiming, Warning,
				TEXT("%s montage %s begins blending out at %.0f%% (pos %.3f of %.3f) at rate %.2f -- "
					 "over half the clip is blend. Set a BlendOutTriggerTime shorter than the blend-out."),
				Label, *Montage->GetName(), 100.0f * BlendStartsAt / Length, BlendStartsAt, Length, Rate);
		}
	}

	TD_TIMING_LOG(TEXT("[%.3f] KNOCKDOWN MONTAGE  %s  %s len=%.3f from=%.3f fitted=%.3f rate=%.3f "
			 "want=%.3fs played=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(), Label, Length, From, Fitted, Rate, TargetSeconds, PlayingLength);
}
void ATDCombatCharacter::ApplyKnockdownFall(AActor* Attacker)
{
	if (!Attacker || KnockdownSpacingCm <= 0.0f)
	{
		return;
	}

	const FVector AttackerLoc = Attacker->GetActorLocation();
	FVector Radial = GetActorLocation() - AttackerLoc;
	Radial.Z = 0.0f;

	if (!Radial.Normalize())
	{
		// **Degenerate: exactly co-located, so there is no bearing.** Falls back to the attacker's
		// facing, which is the only axis still meaningful when the victim has none of their own.
		// Reachable in principle by two capsules resolved to the same XY; it has never been seen.
		Radial = Attacker->GetActorForwardVector();
		Radial.Z = 0.0f;
		if (!Radial.Normalize())
		{
			return;
		}
	}

	// The bearing, recorded for the trace before the clamp touches anything: the angle between the
	// radial axis and the attacker's facing. Roughly zero when the victim was in front, roughly
	// plus or minus ninety for the two victims of a 360-degree finisher.
	FVector AttackerFacing = Attacker->GetActorForwardVector();
	AttackerFacing.Z = 0.0f;
	if (AttackerFacing.Normalize())
	{
		const float Dot = FMath::Clamp(FVector::DotProduct(AttackerFacing, Radial), -1.0f, 1.0f);
		const float Signed = FMath::Atan2(
			FVector::DotProduct(FVector::CrossProduct(AttackerFacing, Radial), FVector::UpVector),
			Dot);
		LastKnockdownBearingDegrees = FMath::RadiansToDegrees(Signed);
	}
	else
	{
		LastKnockdownBearingDegrees = 0.0f;
	}

	// **Never inward**, the same clamp the knockback carries and for the same reason: a contact
	// already beyond the authored spacing keeps its distance rather than being pulled toward the
	// sword. Deleting this max() is the whole change if a pull-in is ever wanted.
	const float CurrentCm = static_cast<float>(FVector::Dist2D(GetActorLocation(), AttackerLoc));
	const float FinalSpacingCm = FMath::Max(KnockdownSpacingCm, CurrentCm);

	FVector Destination = AttackerLoc + Radial * FinalSpacingCm;

	// **Z is carried across, and gravity overrides it.** The destination needs some Z or the body
	// would be driven toward the attacker's feet; passing the victim's own contact height makes the
	// vector purely horizontal. What stops that height being *held* is IgnoreZAccumulate on the
	// root motion source -- see ReceiveKnockback, where the juggling it prevents is explained. An
	// airborne victim is knocked down mid-air and falls while the fall moves them.
	Destination.Z = GetActorLocation().Z;
	// **The carry outlives the fall deliberately.** The montage plays past its fitted window while
	// the body settles, and a carry ending on the fitted boundary strands that settle with no
	// horizontal at all -- which reads as inertia vanishing on contact. The extra span carries the
	// decaying skid; both curves are normalised over the total, not over the fall.
	ReceiveKnockback(Destination, KnockdownFallSeconds + KnockdownCarrySettleSeconds,
		KnockdownFallTimeMappingCurve, KnockdownFallPathOffsetCurve);
}

void ATDCombatCharacter::OpenParryWindow(float DurationSeconds, float WhiffRecoverySeconds)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || DurationSeconds <= 0.0f || bDead)
	{
		return;
	}

	// Assigned rather than max-extended, which is the deliberate difference from every stun beside
	// it. A stun is a sentence and two of them must never total less than one; a parry window is a
	// *purchase*, and GA_Parry cannot be re-entered while one is open anyway -- State.Parrying
	// blocks its own activation the way GA_Block does. So there is no second window to merge with,
	// and taking a max here would only matter if that guarantee had already failed.
	bParryWindowOpen = true;
	bParryCaughtThisWindow = false;
	ApplyParryWindowState();
	ParryWindowEndsAt = World->GetTimeSeconds() + DurationSeconds;
	PendingParryWhiffRecoverySeconds = WhiffRecoverySeconds;

	TD_TIMING_LOG(TEXT("[%.3f] PARRY WINDOW open on %s  until=%.3f"),
		World->GetTimeSeconds(),
		*GetName(),
		ParryWindowEndsAt);
}

void ATDCombatCharacter::CloseParryWindow(ETDParryCloseReason Reason)
{
	UWorld* World = GetWorld();
	if (!World || !bParryWindowOpen)
	{
		return;
	}

	// Cleared before anything else, so that the cancel below -- which re-enters here through
	// GA_Parry's EndAbility -- finds nothing to do. Idempotence by ordering rather than by a
	// re-entrancy flag.
	bParryWindowOpen = false;
	ClearParryWindowState();

	bParryCaughtThisWindow = false;

	// **Cancellation does not reach here at all** -- "parry is sacred", so an attacker can never
	// interrupt an active parry, and GA_Parry::EndAbility leaves an open window running and warns
	// rather than closing it.
	//
	// **Only Expired bills.** A catch owes nothing, and death is on the house because dying resets
	// your starting conditions anyway.
	if (Reason == ETDParryCloseReason::Expired)
	{
		ApplyParryRecovery(PendingParryWhiffRecoverySeconds);

		TD_TIMING_LOG(TEXT("[%.3f] PARRY WHIFF  %s  recovery=%.3f"),
			World->GetTimeSeconds(),
			*GetName(),
			PendingParryWhiffRecoverySeconds);
	}
	else if (Reason == ETDParryCloseReason::Caught)
	{
		// **Grace starts here and only here.** Siting it on the window's catch rather than in
		// NotifyParrySuccess is what makes "does not re-arm" structural: a Grace catch never
		// reaches this function, because there is no window left to close.
		BeginParryGrace();
	}

	PendingParryWhiffRecoverySeconds = 0.0f;

	// **A catch and a death end the ability at once; an expiry does not.** A whiff keeps GA_Parry
	// alive across State.ParryRecovery so its movement lock holds and the recovery is a real
	// commitment rather than a refusal you can walk around during -- that cancel happens in
	// EndParryRecovery instead. A catch frees the parrier instantly, which is the reward the whole
	// design derives from: "successful parries can retrigger without impeding other actions"
	// survives only because this path ends immediately.
	if (Reason != ETDParryCloseReason::Expired)
	{
		CancelParryAbility();
	}
}

void ATDCombatCharacter::CancelParryAbility()
{
	// Matched on type rather than on a tag, for the reason CancelBlockAbility gives at length --
	// CancelAbilities matches *asset* tags, not the ActivationOwnedTags a reader expects.
	if (!AbilitySystem)
	{
		return;
	}

	TArray<FGameplayAbilitySpecHandle> ToCancel;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->IsA<UTDParryAbility>())
		{
			ToCancel.Add(Spec.Handle);
		}
	}

	// Collected first: cancelling inside the loop can reallocate the ASC's live spec array.
	for (const FGameplayAbilitySpecHandle& Handle : ToCancel)
	{
		AbilitySystem->CancelAbilityHandle(Handle);
	}
}

void ATDCombatCharacter::NotifyParrySuccess(AActor* Attacker)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}

	// **Two things can catch, and which one did is the entire no-re-arm rule.** An open window
	// catches and then starts a Grace tail; Grace itself catches and starts nothing. The window
	// wins when both are somehow true, because only it has a tail to give.
	const bool bByWindow = bParryWindowOpen;
	const bool bByGrace = !bByWindow && bInParryGrace;
	if (!bByWindow && !bByGrace)
	{
		return;
	}

	bParryCaughtThisWindow = bByWindow;

	// Read before the write so the trace can print what was *actually* credited rather than what
	// was authored. The two differ whenever the bar is near full, and printing the authored number
	// there would be an instrument reporting the input as though it were the output -- the exact
	// failure that let two never-observed wedge values ship. See the gained= field below.
	const float StaminaBefore = GetStamina();

	if (AbilitySystem && ParryStaminaReward > 0.0f)
	{
		// Additive through the ordinary attribute path, so the attribute set's clamp applies and a
		// reward at full stamina is silently free rather than an overflow. Same route
		// PayBlockInitialCost takes in the other direction.
		AbilitySystem->ApplyModToAttribute(
			UTDAttributeSet::GetStaminaAttribute(),
			EGameplayModOp::Additive,
			ParryStaminaReward);
	}

	// **The pause is discharged outright rather than left to tail off** -- the half of the
	// reward that lives in the stamina ledger. GA_Parry carries
	// State.StaminaRegenPaused like every other ability, so a *whiffed* parry pays the pause in the
	// ordinary way; a successful one does not. Clearing the timestamp is what makes it instant:
	// the tag coming off with the ability would otherwise still leave StaminaRegenPauseSeconds of
	// tail behind it.
	RegenSuppressedUntil = 0.0f;

	// **gained= is the credited delta, not the authored reward**, and the distinction is the whole
	// reason it is printed. They are equal only when the bar had room; at full stamina the clamp
	// makes the reward silently free, and a line printing the authored 25 there would assert a
	// payment that never happened.
	//
	// Today the fixture can only ever produce gained=0.0: a parry costs nothing, so an unattended
	// parrier never spends and its bar never leaves 100. That is the clamp behaving, not a fault --
	// and it is why the reward's *magnitude* is filed as untested rather than asserted. This field
	// is what makes it assertable the moment a fixture exists that can spend the parrier's stamina.
	//
	// **by= names which of the two caught it.** Without it a run in which Grace never fires is
	// indistinguishable from one in which it fires constantly, and the no-re-arm rule -- at most one
	// by=window per tail -- is unassertable.
	TD_TIMING_LOG(TEXT("[%.3f] PARRY SUCCESS %s parried %s  by=%s gained=%.1f stamina=%.1f"),
		World->GetTimeSeconds(),
		*GetName(),
		*GetNameSafe(Attacker),
		bByWindow ? TEXT("window") : TEXT("grace"),
		GetStamina() - StaminaBefore,
		GetStamina());

	// Free instantly: no success recovery, so "successful parries can retrigger without impeding
	// other actions" survives from the spec. The close charges nothing and starts the Grace tail.
	//
	// **A Grace catch closes nothing and starts nothing**, which is the whole of "it does not
	// re-arm": there is no window to close, the parrier was already free, and the tail keeps
	// running on its original clock. It still paid the full reward above, because it is a parry.
	if (bByWindow)
	{
		CloseParryWindow(ETDParryCloseReason::Caught);
	}
}

void ATDCombatCharacter::ApplyParryWindowState()
{
	if (AbilitySystem)
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_Parrying);
	}
}

void ATDCombatCharacter::ClearParryWindowState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_Parrying);
	}
}

void ATDCombatCharacter::OnRep_ParryWindow()
{
	// The tag belongs to the window rather than to GA_Parry, so a client applies it from the
	// replicated bool exactly as it does for the recoveries and the stuns.
	if (bParryWindowOpen)
	{
		ApplyParryWindowState();
	}
	else
	{
		ClearParryWindowState();
	}
}

void ATDCombatCharacter::BeginParryGrace()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || ParryGraceSeconds <= 0.0f)
	{
		return;
	}

	// **Assigned, never max-extended, and that is deliberate against every other timestamp here.**
	// Max-extension is what the stuns use so overlapping causes cannot shorten a sentence; Grace
	// has exactly one cause and must not accumulate, so a plain assignment *is* the no-re-arm rule.
	// Only a window catch reaches this, so there is nothing to accumulate anyway -- the assignment
	// is belt and braces against a future caller.
	ParryGraceEndsAt = World->GetTimeSeconds() + ParryGraceSeconds;
	bInParryGrace = true;

	TD_TIMING_LOG(TEXT("[%.3f] PARRY GRACE  %s  until=%.3f"),
		World->GetTimeSeconds(), *GetName(), ParryGraceEndsAt);
}

void ATDCombatCharacter::EndParryGrace()
{
	if (!bInParryGrace)
	{
		return;
	}

	bInParryGrace = false;

	// Nothing else to undo. Grace applies no tag, takes no lock and refuses nothing, so expiring
	// is the whole of ending it -- which is what "self-contained" means in practice.
	TD_TIMING_LOG(TEXT("[%.3f] PARRY GRACE END %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetName());
}

void ATDCombatCharacter::ApplyParryRecovery(float DurationSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || DurationSeconds <= 0.0f || bDead)
	{
		return;
	}

	// Max-extended like blockstun and hitstun. **One cause today** -- a whiffed parry -- and kept as
	// a max rather than an assignment anyway, because two whiffs cannot overlap only by construction
	// (GA_Parry refuses itself while State.ParryRecovery is present), and an assignment would
	// quietly become the bug the moment anything else charged one.
	ParryRecoveryEndsAt = FMath::Max(ParryRecoveryEndsAt, World->GetTimeSeconds() + DurationSeconds);

	if (!bInParryRecovery)
	{
		bInParryRecovery = true;
		ApplyParryRecoveryState();
	}
}

void ATDCombatCharacter::OverrideParryRecovery(const TCHAR* Cause)
{
	if (!bInParryRecovery)
	{
		return;
	}

	// **A lockout overrides a recovery, and that is the schema doing the work rather than a
	// special case.** A lockout is *externally inflicted* and a recovery is *self-inflicted*, so the
	// general rule falls straight out: being punished supersedes the price you were paying yourself,
	// and there is no need to enumerate which punishments count.
	//
	// **Deliberately not narrowed to hitstun**: knockdown wants this, and so will ability effects
	// that do not exist yet. **Anything that inflicts a lockout should call this**, which is why it
	// is a named function rather than three copies of a bool assignment.
	//
	// The charge usually happened moments earlier -- EnterHitstun cancels every ability, which runs
	// GA_Parry's EndAbility, which closes the window and bills the whiff. Charging and then
	// overriding is correct rather than wasteful: the trace shows the read failed *and* that the
	// punishment absorbed its price.
	TD_TIMING_LOG(TEXT("[%.3f] PARRY RECOVERY OVERRIDDEN %s  by=%s  remaining=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		Cause,
		GetWorld() ? FMath::Max(0.0f, ParryRecoveryEndsAt - GetWorld()->GetTimeSeconds()) : -1.0f);

	EndParryRecovery();
}

void ATDCombatCharacter::EndParryRecovery()
{
	if (!bInParryRecovery)
	{
		return;
	}

	bInParryRecovery = false;
	ClearParryRecoveryState();

	// **This is where a whiffed parry's ability finally ends.** CloseParryWindow deliberately left
	// it running so the movement lock held across the recovery; without this the ability would
	// outlive its own recovery and the character would never get moving again. Harmless when the
	// recovery came from anything else, since nothing else charges this one.
	CancelParryAbility();
}

void ATDCombatCharacter::ApplyParryRecoveryState()
{
	if (AbilitySystem)
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_ParryRecovery);
	}

	TD_TIMING_LOG(TEXT("[%.3f] PARRY RECOVERY %s  until=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		ParryRecoveryEndsAt);
}

void ATDCombatCharacter::ClearParryRecoveryState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_ParryRecovery);
	}

	TD_TIMING_LOG(TEXT("[%.3f] PARRY RECOVERY END %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName());
}

void ATDCombatCharacter::OnRep_ParryRecovery()
{
	if (bInParryRecovery)
	{
		ApplyParryRecoveryState();
	}
	else
	{
		ClearParryRecoveryState();
	}
}

void ATDCombatCharacter::ApplyDodgeRecovery(float DurationSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || DurationSeconds <= 0.0f || bDead)
	{
		return;
	}

	// Max-extended: two dodges landing close together must not let the second one's gap cut the
	// first one short,
	// which would make chaining dodges the cheap route to the parry this forbids.
	DodgeRecoveryEndsAt = FMath::Max(DodgeRecoveryEndsAt, World->GetTimeSeconds() + DurationSeconds);

	if (!bInDodgeRecovery)
	{
		bInDodgeRecovery = true;
		ApplyDodgeRecoveryState();
	}
}

void ATDCombatCharacter::EndDodgeRecovery()
{
	if (!bInDodgeRecovery)
	{
		return;
	}

	bInDodgeRecovery = false;
	ClearDodgeRecoveryState();

	// **No ability cancel here, unlike EndParryRecovery.** This gap forbids one activation; it was
	// never a commitment, and GA_Dodge has been over since before it started.
}

void ATDCombatCharacter::ApplyDodgeRecoveryState()
{
	if (AbilitySystem)
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_DodgeRecovery);
	}

	TD_TIMING_LOG(TEXT("[%.3f] DODGE RECOVERY %s  until=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		DodgeRecoveryEndsAt);
}

void ATDCombatCharacter::ClearDodgeRecoveryState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_DodgeRecovery);
	}

	TD_TIMING_LOG(TEXT("[%.3f] DODGE RECOVERY END %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName());
}

void ATDCombatCharacter::OnRep_DodgeRecovery()
{
	if (bInDodgeRecovery)
	{
		ApplyDodgeRecoveryState();
	}
	else
	{
		ClearDodgeRecoveryState();
	}
}

void ATDCombatCharacter::BeginOnHitMovementWaiver(float DelaySeconds)
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}

	// Taken as the *earliest* of any pending waiver rather than the latest, which is the opposite
	// of the stun family beside it and is the right way round for the same reason: these are
	// releases, not sentences. Two hits landing in one exchange should hand movement back at the
	// first moment either of them allows, never make the attacker wait for the slower one.
	const float ReturnAt = World->GetTimeSeconds() + FMath::Max(DelaySeconds, 0.0f);
	OnHitMovementWaiverAt = bOnHitMovementWaiverPending ? FMath::Min(OnHitMovementWaiverAt, ReturnAt) : ReturnAt;
	bOnHitMovementWaiverPending = true;

	// The fixture that witnesses the waiver, here because this is the one place that runs exactly
	// when an attacker has connected. Deduped on a short window rather than the attack instance: a
	// swing hitting two bodies calls this twice in one tick, and the intent is one dodge per
	// connecting attack, not one per victim.
	if (bDebugDodgeAfterHit && DebugDefendDodgeInputTag.IsValid()
		&& World->GetTimeSeconds() - DebugLastDodgeAfterHitAt > 0.25f)
	{
		DebugLastDodgeAfterHitAt = World->GetTimeSeconds();

		// The input edges directly rather than DebugAutoDodgePress(), which re-homes the pawn first.
		// Here the dodger *is* the attacker, mid-recovery, and teleporting it home would destroy the
		// thing being measured -- whether the dodge came out -- by moving the body as it starts.
		OnAbilityInputPressed(DebugDefendDodgeInputTag);

		GetWorldTimerManager().SetTimer(
			DebugAutoDodgeReleaseTimerHandle,
			this,
			&ATDCombatCharacter::DebugAutoDodgeRelease,
			0.05f,
			false);
	}
}

void ATDCombatCharacter::ReceiveKnockback(const FVector& DestinationWorld, float DurationSeconds, UCurveFloat* TimeMappingCurve,
	UCurveVector* PathOffsetCurve)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement || !HasAuthority() || bDead || DurationSeconds <= 0.0f)
	{
		return;
	}

	// A re-hit mid-slide replaces the running translation outright -- last hit wins, and the new
	// destination is computed from the new contact, so two overlapping sources never fight.
	if (KnockbackRootMotionSourceID != 0)
	{
		Movement->RemoveRootMotionSourceByID(KnockbackRootMotionSourceID);
		KnockbackRootMotionSourceID = 0;
	}

	// The engine's fixed-destination source: variable magnitude, exact endpoint, which is the whole
	// design. The *dynamic* variant with a static target, because only it carries TimeMappingCurve.
	// Same channel, priority and accumulate mode as the lunge, because this is the lunge's
	// target-side twin and must interact with the movement stack the same way.
	TSharedPtr<FRootMotionSource_MoveToDynamicForce> MoveTo = MakeShared<FRootMotionSource_MoveToDynamicForce>();
	MoveTo->InstanceName = FName("TDKnockback");
	MoveTo->AccumulateMode = ERootMotionAccumulateMode::Override;
	MoveTo->Priority = 5;
	MoveTo->StartLocation = GetActorLocation();
	MoveTo->InitialTargetLocation = DestinationWorld;
	MoveTo->TargetLocation = DestinationWorld;
	MoveTo->Duration = DurationSeconds;
	MoveTo->bRestrictSpeedToExpected = false;
	MoveTo->TimeMappingCurve = TimeMappingCurve;
	MoveTo->PathOffsetCurve = PathOffsetCurve;
	MoveTo->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	MoveTo->FinishVelocityParams.ClampVelocity = 0.0f;

	// Gravity keeps the Z axis. This is what stops juggling: an Override source overrides velocity,
	// gravity included, so a fixed destination whose Z is the target's contact height pins an
	// airborne body there for the source's duration, and ClampVelocity then drops them from rest.
	// Land a second hit before they fall clear and the hang re-arms indefinitely.
	//
	// IgnoreZAccumulate is the engine's own answer: UCharacterMovementComponent tracks override
	// sources carrying it separately (bHasOverrideSourcesWithIgnoreZAccumulate), so vertical motion
	// stays with the physics while XY still arrives at the authored destination.
	//
	// Applied to both paths: the knockdown carry and the knockback share this function, and a rule
	// holding for one of two displacement paths would be rediscovered as a bug.
	//
	// **An authored arc is the one case that wants Z back.** A path offset whose Z is discarded is
	// no arc at all, so supplying one drops the flag and the source owns the vertical for its
	// duration. Scoped to the caller rather than to the function: the knockback's spacing reset
	// passes no curve and keeps gravity, so a light hit does not start arcing because a knockdown
	// does.
	//
	// **An already-airborne victim keeps gravity even so**, because the destination's Z is their
	// own current height: the lerp would hold them there for the whole duration and the arc would
	// hop from a height they never fell from. That is the hang IgnoreZAccumulate exists to stop,
	// and it is what s6-airborne asserts against. An arc is authored from the ground, so a body
	// that is not on it has no reference to arc from.
	const bool bAirborne = Movement->IsFalling();
	if (!PathOffsetCurve || bAirborne)
	{
		MoveTo->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	}

	KnockbackRootMotionSourceID = Movement->ApplyRootMotionSource(MoveTo);
}

int32 ATDCombatCharacter::ResolveStringSwingIndexForActivation(int32 SwingCount)
{
	if (bStringAdvancePending && StringIndex + 1 < SwingCount)
	{
		++StringIndex;
	}
	else
	{
		StringIndex = 0;
	}

	// Consumed either way: the mark answered this activation's question, and it is set again -- or
	// is not -- when this swing ends. Leaving it standing would let one mark admit two swings.
	bStringAdvancePending = false;

	return StringIndex;
}

void ATDCombatCharacter::MarkStringAdvancePending()
{
	bStringAdvancePending = true;

	TD_TIMING_LOG(TEXT("[%.3f] STRING     advance marked on %s (after swing %d)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetName(), StringIndex);
}

void ATDCombatCharacter::ResetString(const TCHAR* Reason)
{
	// Silent when there is nothing to reset: this is called defensively from several paths, and a
	// STRING line per idle no-op would bury the ones that mean something.
	if (StringIndex == 0 && !bStringAdvancePending)
	{
		return;
	}

	TD_TIMING_LOG(TEXT("[%.3f] STRING     reset on %s (%s)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetName(), Reason);

	StringIndex = 0;
	bStringAdvancePending = false;
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
		// Drain never exhausts. Holding a guard runs the bar to zero and leaves it there, which
		// converts holding into risk rather than a countdown: a guard at zero breaks to the next
		// blocked hit. Every other spender still exhausts -- a dodge at 30 empties and locks you out.
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

	// The killer travels with the damage, and death is the one contact that imparts nothing
	// otherwise: knockback sits on the hitstun branch and EnterKnockdown returns early on bDead.
	AActor* Killer = nullptr;
	if (Data.GEModData)
	{
		Killer = Data.GEModData->EffectSpec.GetContext().GetEffectCauser();
	}
	EnterDeath(Killer);
}

void ATDCombatCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// To everyone, not just the owner: a simulated proxy has to know an opponent is dead or
	// exhausted, because that is what its own ragdoll and greyed bar are drawn from.
	DOREPLIFETIME(ATDCombatCharacter, bDead);
	DOREPLIFETIME(ATDCombatCharacter, DeathImpulse);
	DOREPLIFETIME(ATDCombatCharacter, bExhausted);
	DOREPLIFETIME(ATDCombatCharacter, bGuardBroken);
	DOREPLIFETIME(ATDCombatCharacter, bInBlockstun);
	DOREPLIFETIME(ATDCombatCharacter, bInHitstun);
	DOREPLIFETIME(ATDCombatCharacter, HitstunTellSerial);
	DOREPLIFETIME(ATDCombatCharacter, HitstunTellSpanSeconds);
	DOREPLIFETIME(ATDCombatCharacter, BlockstunTellSerial);
	DOREPLIFETIME(ATDCombatCharacter, BlockstunTellSpanSeconds);
	DOREPLIFETIME(ATDCombatCharacter, bKnockedDown);
	DOREPLIFETIME(ATDCombatCharacter, KnockdownType);
	DOREPLIFETIME(ATDCombatCharacter, bInParryLockout);
	DOREPLIFETIME(ATDCombatCharacter, ParryLockoutTellSerial);
	DOREPLIFETIME(ATDCombatCharacter, ParryLockoutTellSpanSeconds);
	DOREPLIFETIME(ATDCombatCharacter, bParryWindowOpen);
	DOREPLIFETIME(ATDCombatCharacter, bInParryRecovery);
	DOREPLIFETIME(ATDCombatCharacter, bInDodgeRecovery);
	DOREPLIFETIME(ATDCombatCharacter, bInParryGrace);
	DOREPLIFETIME(ATDCombatCharacter, StringIndex);
}

void ATDCombatCharacter::EnterDeath(AActor* Killer)
{
	if (bDead)
	{
		return;
	}
	bDead = true;

	// Written before bDead so it is in hand when the OnRep fires on every other machine.
	DeathImpulse = FVector::ZeroVector;
	if (Killer && DeathImpulseStrength > 0.0f)
	{
		FVector Away = GetActorLocation() - Killer->GetActorLocation();
		Away.Z = 0.0f;
		if (Away.Normalize())
		{
			DeathImpulse = (Away + FVector::UpVector * DeathImpulseLift).GetSafeNormal() * DeathImpulseStrength;
		}
	}

	// Death is the single exception to "parry is sacred", on the house: the window closes and no
	// recovery is charged, because dying resets your starting conditions anyway.
	//
	// Explicitly before the cancel below -- the cancel runs GA_Parry's EndAbility, which warns about
	// an open window it may no longer close, and that warning is for a real violation.
	//
	// Unreachable today: nothing damages you through an open window, so only a damage-over-time
	// effect could kill you inside one. Grace is deliberately not torn down here -- it protects
	// nobody once you are dead, and expiring on its own clock is one fewer special case.
	CloseParryWindow(ETDParryCloseReason::Death);

	// Server-only, and outside ApplyDeathState. Cancelling abilities is an authority decision that
	// replicates through GAS on its own; running it again from a client's OnRep would cancel that
	// client's predicted copies out from under a correction that may never come.
	if (AbilitySystem)
	{
		// The tag alone refuses only *new* activations, which is exhaustion's rule and visibly wrong
		// here: a killing blow mid-swing would leave a corpse finishing its attack, hitbox included.
		// Cancelling also clears State.Attacking and State.Attacking.Committed through the normal
		// end path, so death cannot leak tags that would forbid every defensive action on revive.
		AbilitySystem->CancelAllAbilities();
	}

	// Neither does the string: a chain half-thrown by the deceased must not greet the revive.
	// The cancel above already resets it when death interrupted a swing; this covers dying with
	// an advance pending between swings.
	ResetString(TEXT("died"));

	// A buffered press must not survive death: it would fire on revive, an action asked for
	// in a situation that no longer exists. Local input state, so it is meaningless on any
	// machine that is not the one that pressed the button.
	BufferedInput.Clear();

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
	// Logged here rather than in EnterDeath: the Apply*/Clear* pairs run on every machine while
	// Enter/Exit run on the server alone, so a log on the transition is invisible to exactly the
	// client the replicated bool exists for. The server still logs once -- Enter calls this directly.
	TD_TIMING_LOG(TEXT("[%.3f] DEATH      %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetName());

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

		// After StartRagdoll, which is what puts the bodies under simulation -- an impulse before
		// it lands on a kinematic mesh and is discarded silently. Applied on every machine rather
		// than replicated as a result: the corpse diverges per client and nothing queries it, the
		// capsule staying put as the actor transform and the Ragdoll profile ignoring Pawn.
		if (!DeathImpulse.IsNearlyZero())
		{
			if (USkeletalMeshComponent* SkeletalMesh = GetMesh())
			{
				SkeletalMesh->AddImpulse(DeathImpulse, NAME_None, false);
			}
		}
	}
}

void ATDCombatCharacter::ClearDeathState()
{
	// Sited with DEATH's log above, for the same client-visibility reason.
	TD_TIMING_LOG(TEXT("[%.3f] REVIVE     %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetName());

	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_Dead);
	}

	// Before the teleport, deliberately. StopRagdoll reattaches the mesh to the capsule, and
	// moving the actor while physics still drives the mesh in world space would leave the body
	// behind. Order is the whole correctness argument here.
	StopRagdoll();

	// A dummy revives where it was placed rather than wherever its last root motion carried it;
	// without this it stands up displaced. The player is never teleported and revives where they
	// fell.
	ReturnToDebugAutoAttackHome();

	// Falling rather than Walking: the character may have died mid-air, and forcing Walking there
	// leaves it hovering with no gravity. Falling is self-correcting either way -- on the ground the
	// movement component resolves it to Walking next update, in the air the fall resumes.
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

	// Physics silently refuses to simulate without one, leaving the character standing dead with no
	// error anywhere. Warned rather than logged quietly: a mesh swap is how this breaks, and the
	// symptom -- death not looking like death -- reads as a gameplay regression.
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

	// The profile replaces the whole response table, dropping the camera-probe exemption the
	// constructor set -- which is why the spring arm starts colliding with corpses. Re-applied
	// rather than avoided: Ragdoll is what the simulated body wants, just not the last word on
	// ECC_Camera.
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

	// Read before the simulation is torn down and the mesh reattached, which is the only moment
	// the corpse's resting place still exists. Horizontal only: the capsule never moved, so this is
	// how far the impulse actually carried the body.
	const FVector Drift = SkeletalMesh->GetComponentLocation() - GetActorLocation();
	TD_TIMING_LOG(TEXT("[%.3f] DEATH SETTLE  %s  drift=%.0f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetName(), FVector(Drift.X, Drift.Y, 0.0f).Size());

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

void ATDCombatCharacter::DebugResetForFixture()
{
	if (!HasAuthority())
	{
		return;
	}

	TD_TIMING_LOG(TEXT("[%.3f] DEBUG RESET  %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetName());

	if (AbilitySystem)
	{
		AbilitySystem->CancelAllAbilities();
	}
	BufferedInput.Clear();

	// Each state's own exit, in the order the code would reach them. Every one returns early when
	// its state is not live, so a pawn already at rest passes through untouched.
	EndHitstun();
	EndBlockstun();
	EndGuardBreak();
	EndKnockdown();
	ClearParryWindowState();
	EndParryGrace();
	EndParryRecovery();
	EndParryLockout();
	EndDodgeRecovery();

	// Written before the exhaustion exit, as ReviveFromDebug does: EXHAUSTION END prints the bar it
	// ends on, and every other exit from exhaustion is at max. Exiting first prints a mid-regen
	// value on a line whose band is MaxStamina.
	if (AbilitySystem)
	{
		AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), GetMaxHealth());
		AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetStaminaAttribute(), GetMaxStamina());
	}
	ExitExhaustion();
	ClearDeathState();

	// The locks the cancel above does not reach: an ability cancelled outside its own EndAbility
	// releases them, but the on-hit waiver's release has no matching take, so clear both outright.
	SetAbilityMovementLocked(false);
	SetAbilityFacingLocked(false);

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->SetMovementMode(MOVE_Walking);
	}
}

void ATDCombatCharacter::DebugSetStamina(float Value)
{
	if (!HasAuthority() || !AbilitySystem)
	{
		return;
	}
	const float Clamped = FMath::Clamp(Value, 0.0f, GetMaxStamina());
	TD_TIMING_LOG(TEXT("[%.3f] DEBUG STAMINA  %s %.1f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetName(), Clamped);
	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetStaminaAttribute(), Clamped);
}

void ATDCombatCharacter::DebugSetHealth(float Value)
{
	if (!HasAuthority() || !AbilitySystem)
	{
		return;
	}
	const float Clamped = FMath::Clamp(Value, 0.0f, GetMaxHealth());
	TD_TIMING_LOG(TEXT("[%.3f] DEBUG HEALTH  %s %.1f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetName(), Clamped);
	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), Clamped);
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
	// Both edges carry the bar because the rule is that exhaustion begins at 0 and ends at Max
	// rather than on a clock -- the two numbers are the assertion, and a value that is neither says
	// the mechanism moved. Logged in the state pair rather than Enter/Exit for the reason
	// ApplyDeathState gives. Fires only on real transitions: the server guards in
	// HandleStaminaChanged, clients in OnRep on a changed bool.
	TD_TIMING_LOG(TEXT("[%.3f] EXHAUSTED  %s  stamina=%.1f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		GetStamina());

	if (AbilitySystem && ExhaustedTag.IsValid())
	{
		AbilitySystem->AddLooseGameplayTag(ExhaustedTag);
	}
}

void ATDCombatCharacter::ClearExhaustionState()
{
	// Sited with EXHAUSTED's log above, for the same client-visibility reason.
	TD_TIMING_LOG(TEXT("[%.3f] EXHAUSTION END %s  stamina=%.1f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		GetStamina());

	if (AbilitySystem && ExhaustedTag.IsValid())
	{
		AbilitySystem->RemoveLooseGameplayTag(ExhaustedTag);
	}
}

void ATDCombatCharacter::Jump()
{
	// Every permission gate lives in UTDJumpAbility. Restating exhaustion, death, hitstun, the
	// movement lock and the guard's commitment here would be five copies of rules the shared base
	// already enforces, which is the arrangement that lets one be forgotten.
	//
	// Reachable only through GA_Jump, which has answered all of them before calling. Death, the
	// check whose removal looks riskiest, is inert twice over: the base refuses every ability to a
	// corpse, and dying calls DisableMovement, so CanJump() is false regardless.
	Super::Jump();

	// The guard does not survive the jump, and is dropped here rather than on becoming airborne.
	// Tick already drops it on the falling state, which covers walking off a ledge, but that is a
	// frame away and a jump beginning with the shield still up is visible. This one is for the
	// look, the Tick one for the rule.
	//
	// After Super::Jump(), so a jump the movement component itself declines does not cost the guard.
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

	// Landing is a resume opportunity, and the one that is not an ability ending. Going airborne
	// cancels a guard, and the resume that follows is correctly refused for being airborne --
	// nothing then fires on touchdown, so a held button stayed unanswered until the next unrelated
	// ability ended. Requested rather than done inline for the re-entrancy reason; see bResumePending.
	bResumePending = true;
}

UAbilitySystemComponent* ATDCombatCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

void ATDCombatCharacter::BeginPlay()
{
	Super::BeginPlay();

	// First, before anything reads a value that may be a lie. See the function's comment.
	WarnOnStaleInstanceOverrides();

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

	// First resolution; every possession re-runs it. Also the safety net for a pawn that is never
	// possessed at all -- the dummy is not one: its AAIController possesses it after spawn.
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

	// Nothing else can say which ASC a client resolved: the toolset cannot see the client world, and
	// an unseeded fallback reads the same 100/100 on the HUD as the real thing, because the
	// attribute constructor inits to 100. Logged on every resolution path -- BeginPlay, PossessedBy,
	// OnRep_PlayerState -- so a client's swap when its PlayerState arrives is visible in its own log.
	TD_TIMING_LOG(TEXT("[%.3f] ASC RESOLVE %s -> %s (%s)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		*GetNameSafe(OwnerActor),
		OwnerActor == this ? TEXT("owned fallback") : TEXT("PlayerState"));

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
	// Guarded per ASC, not per call and not per character. Actor info is rebound on every possession
	// but seeding must happen once, or a pawn possessed after BeginPlay is granted every ability a
	// second time and stacks a second copy of every DefaultEffect -- for an infinite effect, a
	// permanently doubled magnitude rather than a visible one-off.
	//
	// Where the flag lives is the subtle half. A player's BeginPlay runs before possession, so a
	// single flag on the character would be spent on the fallback ASC and the PlayerState's real one
	// never seeded -- a player with no attributes, while the never-possessed dummy worked perfectly.
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

	const bool bDebugAttacker = bDebugAutoAttack && DebugAutoAttackInputTag.IsValid();

	// Captured before the first swing or dodge, so it is the placed transform rather than wherever
	// root motion has carried us. Taken for every pawn, so any path that sends one home has a real
	// transform to send it to -- an uncaptured home is the world origin.
	DebugAutoAttackHomeTransform = GetActorTransform();

	if (bDebugAttacker)
	{
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

	switch (DebugAutoDefendMode)
	{
	case ETDDebugDefendMode::HoldBlock:
		if (DebugDefendBlockInputTag.IsValid())
		{
			// One press, never released, and that is the entire mode. The press marks the spec
			// InputPressed whether or not this activation succeeds, and GA_Block resumes while its
			// input is held -- so the guard returns after every break, exhaustion and airborne cancel.
			OnAbilityInputPressed(DebugDefendBlockInputTag);

			// Requested explicitly because nothing has *ended* yet, and the resume tick only looks
			// when something has. Without it a guard refused at spawn never retries: a placed pawn
			// can still be settling onto the floor, and a guard cannot be raised airborne, so the
			// fixture would start silently unarmed. The tick clears this once the guard is up.
			bResumePending = true;
		}
		break;

	case ETDDebugDefendMode::PeriodicDodge:
		if (DebugDefendDodgeInputTag.IsValid())
		{
			GetWorldTimerManager().SetTimer(
				DebugAutoDodgeTimerHandle,
				this,
				&ATDCombatCharacter::DebugAutoDodgePress,
				DebugDodgeIntervalSeconds,
				true,
				DebugDodgeIntervalSeconds);
		}
		break;

	case ETDDebugDefendMode::PeriodicParry:
		if (DebugDefendParryInputTag.IsValid())
		{
			GetWorldTimerManager().SetTimer(
				DebugAutoParryTimerHandle,
				this,
				&ATDCombatCharacter::DebugAutoParryCycle,
				DebugParryIntervalSeconds,
				true,
				DebugParryIntervalSeconds);
		}
		break;

	default:
		break;
	}

	// Outside the switch, because it is not a defend mode. A defender has one defensive policy at a
	// time and that exclusivity is deliberate, but the jump is a second *input* rather than a second
	// policy -- and the rule it observes needs a pawn that blocks and jumps at once.
	if (bDebugPeriodicJump && DebugJumpInputTag.IsValid())
	{
		GetWorldTimerManager().SetTimer(
			DebugAutoJumpTimerHandle,
			this,
			&ATDCombatCharacter::DebugAutoJumpPress,
			DebugJumpIntervalSeconds,
			true,
			DebugJumpIntervalSeconds);
	}
}

void ATDCombatCharacter::WarnOnStaleInstanceOverrides() const
{
	const UClass* Class = GetClass();
	const ATDCombatCharacter* CDO = Class ? Cast<ATDCombatCharacter>(Class->GetDefaultObject()) : nullptr;

	// The CDO compares against itself in the editor's own instance of the class; nothing to say.
	if (!CDO || CDO == this)
	{
		return;
	}

	// DefaultAbilities first, because it is the one that disables a character outright. An instance
	// short of an ability refuses nothing and logs nothing -- the input finds nothing to activate,
	// so it reads as an input or binding bug. Compared by content, not count: a swap of equal length
	// is the same silence.
	if (DefaultAbilities != CDO->DefaultAbilities)
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Stale placed-actor override on %s: DefaultAbilities has %d entr%s, the class default has %d. ")
			TEXT("This instance was placed before its Blueprint authored the rest and keeps the old list forever. ")
			TEXT("EditDefaultsOnly cannot be written on an instance -- delete and re-place the actor. ")
			TEXT("See Docs/Combat-Decisions.md."),
			*GetName(),
			DefaultAbilities.Num(),
			DefaultAbilities.Num() == 1 ? TEXT("y") : TEXT("ies"),
			CDO->DefaultAbilities.Num());
	}

	// The debug input tags, and only when a fixture actually depends on one -- an unset tag on a
	// pawn that never uses that mode is not a fault worth a line. Each of these three was None on
	// the attacker dummy while its class carried a real tag.
	if (bDebugAutoAttack && !DebugAutoAttackInputTag.IsValid() && CDO->DebugAutoAttackInputTag.IsValid())
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Stale placed-actor override on %s: DebugAutoAttackInputTag is unset while the class default is %s. The auto-attacker will press nothing."),
			*GetName(), *CDO->DebugAutoAttackInputTag.ToString());
	}

	const bool bWantsBlockTag = (DebugAutoDefendMode == ETDDebugDefendMode::HoldBlock)
		|| bDebugCancelAttackIntoBlock
		|| (DebugAutoDefendMode == ETDDebugDefendMode::PeriodicParry && DebugParryPreBlockSeconds > 0.0f);
	if (bWantsBlockTag && !DebugDefendBlockInputTag.IsValid() && CDO->DebugDefendBlockInputTag.IsValid())
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Stale placed-actor override on %s: DebugDefendBlockInputTag is unset while the class default is %s. No guard will be raised."),
			*GetName(), *CDO->DebugDefendBlockInputTag.ToString());
	}

	const bool bWantsDodgeTag = (DebugAutoDefendMode == ETDDebugDefendMode::PeriodicDodge) || bDebugDodgeAfterHit;
	if (bWantsDodgeTag && !DebugDefendDodgeInputTag.IsValid() && CDO->DebugDefendDodgeInputTag.IsValid())
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Stale placed-actor override on %s: DebugDefendDodgeInputTag is unset while the class default is %s. No dodge will be pressed."),
			*GetName(), *CDO->DebugDefendDodgeInputTag.ToString());
	}

	if (bDebugPeriodicJump && !DebugJumpInputTag.IsValid() && CDO->DebugJumpInputTag.IsValid())
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Stale placed-actor override on %s: DebugJumpInputTag is unset while the class default is %s. No jump will be pressed."),
			*GetName(), *CDO->DebugJumpInputTag.ToString());
	}

	const bool bWantsParryTag = (DebugAutoDefendMode == ETDDebugDefendMode::PeriodicParry);
	if (bWantsParryTag && !DebugDefendParryInputTag.IsValid() && CDO->DebugDefendParryInputTag.IsValid())
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Stale placed-actor override on %s: DebugDefendParryInputTag is unset while the class default is %s. No parry will be pressed."),
			*GetName(), *CDO->DebugDefendParryInputTag.ToString());
	}
}

void ATDCombatCharacter::DebugAutoParryCycle()
{
	if (DebugAutoDefendMode != ETDDebugDefendMode::PeriodicParry)
	{
		return;
	}

	// No pre-block requested: the cycle *is* the tap, exactly as it was before this existed. Every
	// scenario that does not set DebugParryPreBlockSeconds is bit-for-bit unaffected.
	if (DebugParryPreBlockSeconds <= 0.0f || !DebugDefendBlockInputTag.IsValid())
	{
		DebugAutoParryPress();
		return;
	}

	// Spend stamina before parrying, so the reward has somewhere to land. See the knob's comment
	// for why blocking rather than dodging: it is the only spender that authors no displacement.
	OnAbilityInputPressed(DebugDefendBlockInputTag);

	GetWorldTimerManager().SetTimer(
		DebugAutoParryPreBlockTimerHandle,
		this,
		&ATDCombatCharacter::DebugAutoParryDropGuard,
		DebugParryPreBlockSeconds,
		false);
}

void ATDCombatCharacter::DebugAutoParryDropGuard()
{
	OnAbilityInputReleased(DebugDefendBlockInputTag);

	// A frame's grace before the parry, load-bearing rather than defensive. GA_Parry blocks on
	// State.Blocking, which the guard drops only as its ability ends -- pressing in the same frame
	// as the release is refused every cycle, producing a fixture that looks active and never parries.
	// Short enough to stay inside the drained window: regen does not resume for
	// StaminaRegenPauseSeconds after the guard falls.
	GetWorldTimerManager().SetTimer(
		DebugAutoParryPreBlockTimerHandle,
		this,
		&ATDCombatCharacter::DebugAutoParryPress,
		0.05f,
		false);
}

void ATDCombatCharacter::DebugAutoParryPress()
{
	// Deliberately no ReturnToDebugAutoAttackHome() here, unlike the dodger. A parry does not travel,
	// so the pawn never walks out of the exchange, while a teleport between attempts would sever the
	// spacing the hit is resolved at -- contamination the dodge fixture accepts only because a
	// backward dodge genuinely does leave.
	OnAbilityInputPressed(DebugDefendParryInputTag);

	// Tapped rather than held, for the reason the dodge's tap gives: a press that never comes up is
	// never stale, so a refused one would sit in the buffer and fire unscheduled. It matters more
	// here -- GA_Parry refuses to buffer at all, so a stuck press is a permanently held input
	// answering nothing.
	GetWorldTimerManager().SetTimer(
		DebugAutoParryReleaseTimerHandle,
		this,
		&ATDCombatCharacter::DebugAutoParryRelease,
		0.05f,
		false);
}

void ATDCombatCharacter::DebugAutoParryRelease()
{
	OnAbilityInputReleased(DebugDefendParryInputTag);
}

void ATDCombatCharacter::DebugCancelIntoBlockPress()
{
	OnAbilityInputPressed(DebugDefendBlockInputTag);

	// 0.40 rather than the dodge fixture's 0.05, because this press must outlive MinimumBlockSeconds.
	// Releasing inside the guard's commitment window is the feathering case GA_Block defers, so the
	// release would be remembered and applied later -- giving a guard whose duration is set by the
	// floor rather than by this fixture.
	GetWorldTimerManager().SetTimer(
		DebugCancelIntoBlockTimerHandle,
		this,
		&ATDCombatCharacter::DebugCancelIntoBlockRelease,
		0.40f,
		false);
}

void ATDCombatCharacter::DebugCancelIntoBlockRelease()
{
	OnAbilityInputReleased(DebugDefendBlockInputTag);
}

void ATDCombatCharacter::ReturnToDebugAutoAttackHome()
{
	// Guarded on the reset flag, and a player pawn is never teleported. bDebugHomeAtStand homes a
	// dummy with no other fixture armed; the fixture flags cover their own call sites.
	if (!bDebugAutoAttackResetPosition || IsPlayerControlled()
		|| (!bDebugHomeAtStand && !bDebugAutoAttack && !bDebugPeriodicJump && DebugAutoDefendMode == ETDDebugDefendMode::Off))
	{
		return;
	}

	// Death cancels the running attack, which fires the ability-ended reset -- so without this the
	// corpse teleports home mid-ragdoll, dragging the capsule out from under a mesh physics is
	// driving in world space. ReviveFromDebug calls this again once the ragdoll is put away.
	if (bDead)
	{
		return;
	}

	// Traced because "it fires mid-attack and the numbers still look plausible" is otherwise
	// uncheckable. The timestamp identifies the path: the delayed timer lands ResetDelay after the
	// last swing's ABILITY END, while the burst's belt-and-braces call shares a timestamp with the
	// ACTIVATE that follows. Distance moved separates a real reset from a no-op.
	const FVector PreviousLocation = GetActorLocation();
	TD_TIMING_LOG(TEXT("[%.3f] HOME RESET %s  moved=%.1fcm"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetName(),
		FVector::Dist2D(PreviousLocation, DebugAutoAttackHomeTransform.GetLocation()));

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

	// Nearest other living pawn, with no probe radius: this is a test level, and a cutoff here would
	// be a second spacing number nobody authored. Deliberately not "the player pawn" -- the
	// auto-attacker is on the shared base, so a self-attacking player would focus on itself.
	TArray<APawn*> Candidates;
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

		Candidates.Add(Candidate);
	}

	if (Candidates.Num() == 0)
	{
		return;
	}

	APawn* Chosen = nullptr;

	if (bDebugAutoAttackRotateTargets)
	{
		// Sorted by name, so the cycle is identical run to run. Sorting by distance would
		// reshuffle as knockback moves bodies about, which is the very coupling rotation exists
		// to break -- the attacker would simply chase again through a different mechanism.
		// Exclude whatever the last attack went to, then take the nearest of what remains, so the
		// attacker ping-pongs instead of chasing. Chosen over an index cycle deliberately: an
		// index reshuffles when a death or revive changes the candidate list, while "not that one"
		// keeps meaning the same thing. The Num() > 1 guard is what makes a single-target level
		// behave exactly as before rather than refusing to aim at anything.
		APawn* const Previous = DebugLastFocusTarget.Get();
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (APawn* Candidate : Candidates)
		{
			if (Candidate == Previous && Candidates.Num() > 1)
			{
				continue;
			}

			const float DistanceSquared = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				Chosen = Candidate;
			}
		}

		// Ungated like the other fixture traces: rotation failing is invisible by eye until an
		// assertion built on it reads green for the wrong reason, and it took four cycles of
		// guessing to find that the press path was the wrong clock.
		TD_TIMING_LOG(TEXT("[%.3f] ROTATE     excluded '%s' -> chose '%s' (%d candidates)"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			*GetNameSafe(Previous),
			*GetNameSafe(Chosen),
			Candidates.Num());

		DebugLastFocusTarget = Chosen;
	}
	else
	{
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (APawn* Candidate : Candidates)
		{
			const float DistanceSquared = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				Chosen = Candidate;
			}
		}
	}

	if (Chosen)
	{
		AI->SetFocus(Chosen, EAIFocusPriority::Gameplay);
	}
}

void ATDCombatCharacter::HandleDebugAutoAttackEnded(const FAbilityEndedData& EndedData)
{
	// Target rotation advances here, and only here, because **this is the one event that happens
	// exactly once per attack.** The press path is the wrong clock: taps arrive every
	// DebugAutoAttackStringTapIntervalSeconds (0.25 s) while attacks activate at the chain cadence
	// (~0.5 s), so advancing on a press ticks twice per attack and lands back on the body it
	// started from -- the focus flips on every press while every attack in the burst still commits to
	// the same target.
	//
	// Ahead of the taps guard below deliberately: mid-burst is exactly when rotation must happen,
	// and that guard exists for the home reset, which must *not*.
	if (bDebugAutoAttackRotateTargets)
	{
		UpdateDebugFacingFocus(/*bAttacking=*/true);
	}

	// Opted into per attack, and only ahead of the guard below for that reason. A stationary
	// attacker is what a 360-degree volume needs tested -- one whiffing into open space has an
	// open standoff gate and runs its whole authored lunge, so it would otherwise leave its
	// targets behind after a single attack.
	if (bDebugAutoAttackHomeBetweenAttacks)
	{
		ReturnToDebugAutoAttackHome();
	}

	// Taps still owed means the burst is mid-flight, and returning is safe because the *next*
	// swing's end runs this handler again. There is always a second chance.
	if (DebugStringTapsRemaining > 0)
	{
		return;
	}

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

	const float ResetDelay = DebugAutoAttackResetDelaySeconds;

	if (ResetDelay <= 0.0f)
	{
		ReturnToDebugAutoAttackHome();
		return;
	}

	GetWorldTimerManager().SetTimer(
		DebugAutoAttackResetTimerHandle,
		this,
		&ATDCombatCharacter::TryReturnToDebugAutoAttackHome,
		ResetDelay,
		false);
}

void ATDCombatCharacter::TryReturnToDebugAutoAttackHome()
{
	// **Checked here rather than where the timer was set.** A chained swing ends and its successor
	// activates in the same tick, but the end handler runs in the gap *between* them -- so at
	// schedule time nothing is active and the guard would pass, landing the teleport inside the
	// successor's release window. By the time this fires, the successor is running and visible.
	//
	// Re-armed rather than dropped, so the reset still happens once the string finishes: the
	// running swing's own end also re-schedules, and whichever lands first wins harmlessly.
	if (AbilitySystem)
	{
		for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
		{
			const UTDGameplayAbility* Ability = Cast<UTDGameplayAbility>(Spec.Ability);
			if (Spec.IsActive() && Ability && Ability->InputTag == DebugAutoAttackInputTag)
			{
				GetWorldTimerManager().SetTimer(
					DebugAutoAttackResetTimerHandle,
					this,
					&ATDCombatCharacter::TryReturnToDebugAutoAttackHome,
					FMath::Max(0.05f, DebugAutoAttackResetDelaySeconds),
					false);
				return;
			}
		}
	}

	ReturnToDebugAutoAttackHome();
}

void ATDCombatCharacter::DebugAutoAttackPress()
{
	if (!bDebugAutoAttack && DebugStringTapsRemaining <= 0)
	{
		return;
	}

	// A burst's first press does the housekeeping; the taps inside one deliberately do not. A
	// home-teleport mid-string would sever the spacing chain s4 measures, and the focus survives
	// the whole burst on its own. Taps=1 makes every press a first press, which is the
	// pre-string behaviour exactly.
	// A pending delayed reset must not survive into *any* swing, or it would snap the attacker
	// home mid-attack. Cleared on every press rather than only on a burst's first, because a chain
	// press arriving mid-string has a live timer to cancel, which a burst-only clear would miss.
	GetWorldTimerManager().ClearTimer(DebugAutoAttackResetTimerHandle);

	const bool bStartingBurst = DebugStringTapsRemaining <= 0;
	if (bStartingBurst)
	{
		DebugStringTapsRemaining = FMath::Max(1, DebugAutoAttackStringTaps);

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
	}

	OnAbilityInputPressed(DebugAutoAttackInputTag);

	// The pre-commit cancel fixture. Scheduled from the press rather than from any montage event,
	// because the boundary it has to land inside is measured from the press too -- the light's
	// HoldUntilSeconds -- so anything else would be comparing against a different clock.
	if (bDebugCancelAttackIntoBlock && DebugDefendBlockInputTag.IsValid())
	{
		GetWorldTimerManager().SetTimer(
			DebugCancelIntoBlockTimerHandle,
			this,
			&ATDCombatCharacter::DebugCancelIntoBlockPress,
			FMath::Max(DebugCancelAfterPressSeconds, 0.001f),
			false);
	}

	// The burst's next tap, at string cadence -- it lands during the running swing, is refused,
	// buffered, and chains out exactly as a mashing human's would. Re-enters this function with
	// taps remaining, which is what skips the housekeeping above.
	--DebugStringTapsRemaining;
	if (DebugStringTapsRemaining > 0)
	{
		// Its own handle, never DebugAutoAttackTimerHandle -- that one carries the looping
		// interval, and a one-shot written over it would end the fixture after one burst.
		GetWorldTimerManager().SetTimer(
			DebugAutoAttackStringTimerHandle,
			this,
			&ATDCombatCharacter::DebugAutoAttackPress,
			DebugAutoAttackStringTapIntervalSeconds,
			false);
	}

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

void ATDCombatCharacter::DebugAutoDodgePress()
{
	if (DebugAutoDefendMode != ETDDebugDefendMode::PeriodicDodge)
	{
		return;
	}

	// Every dodge starts from the same transform, which is what makes DODGE END's distance a
	// measurement rather than an accumulation. With no movement input the direction is always
	// backward, so without this the dodger reverses out of the attacker's reach within two
	// dodges and everything read afterwards is of a fixture that has left the exchange.
	//
	// On the press rather than on the ability's end, deliberately: the travel has to complete
	// undisturbed to be measurable, and a reset chasing the dodge home would be the very thing
	// Docs/Working-In-Unreal.md warns about -- something touching the mover mid-measurement.
	ReturnToDebugAutoAttackHome();

	OnAbilityInputPressed(DebugDefendDodgeInputTag);

	// Tapped rather than held, and the release edge is the whole reason. Nothing about a dodge is
	// selected by how long the button is down -- unlike the attack, where the hold picks the tier
	// -- but a press that never comes up is never *stale*, so a refused one would sit in the input
	// buffer indefinitely and fire at some later opportunity nobody scheduled. Releasing restores
	// the buffer's ordinary expiry.
	//
	// Fits inside DebugDodgeIntervalSeconds by construction: the interval clamps at 0.1 and this
	// is 0.05, so a release can never land in the following cycle.
	GetWorldTimerManager().SetTimer(
		DebugAutoDodgeReleaseTimerHandle,
		this,
		&ATDCombatCharacter::DebugAutoDodgeRelease,
		0.05f,
		false);
}

void ATDCombatCharacter::DebugAutoDodgeRelease()
{
	OnAbilityInputReleased(DebugDefendDodgeInputTag);
}

void ATDCombatCharacter::DebugAutoJumpPress()
{
	if (!bDebugPeriodicJump)
	{
		return;
	}

	// No ReturnToDebugAutoAttackHome() here, unlike the dodge's press. This fixture's pawn is
	// usually the *defender*, standing where the attacker needs it, and re-homing it every cycle
	// would move the body mid-exchange -- the contamination Docs/Working-In-Unreal.md warns about.
	// A jump also authors no travel of its own, so there is nothing to reset.
	OnAbilityInputPressed(DebugJumpInputTag);

	// Released a frame later for the reason the dodge is: GA_Jump ends on the release, and a press
	// that never comes up would hold the ability open across the whole run -- so the second cycle
	// would find it already active and GAS would refuse it before CanActivateAbility ran, which is
	// exactly the silent-drop shape that cost s5-parry-whiff a session.
	GetWorldTimerManager().SetTimer(
		DebugAutoJumpReleaseTimerHandle,
		this,
		&ATDCombatCharacter::DebugAutoJumpRelease,
		0.05f,
		false);
}

void ATDCombatCharacter::DebugAutoJumpRelease()
{
	OnAbilityInputReleased(DebugJumpInputTag);
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

bool ATDCombatCharacter::TryActivateAbilitiesForInput(const FGameplayTag& InputTag, bool bForwardToActive,
	bool bMarkInputPressed)
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
		if (bMarkInputPressed && (bForwardToActive || !Spec->IsActive()))
		{
			AbilitySystem->AbilitySpecInputPressed(*Spec);
		}

		if (!Spec->IsActive())
		{
			bActivated |= AbilitySystem->TryActivateAbility(Handle);
		}
	}

	// **A refused press still says the button is down.** The spec was marked above whether or not
	// activation took, but only an ability *ending* ever requested the resume tick -- so a press
	// that fails while nothing is running was marked and then never retried. Holding block through
	// a knockdown lockout hit exactly that: the press was refused, the window opened a moment
	// later, and nothing looked again.
	//
	// Requesting unconditionally is safe because the resume tick filters to abilities that opted
	// into bResumeWhileInputHeld and still read InputPressed; a refused attack asks for nothing.
	if (!bActivated && !Handles.IsEmpty())
	{
		bResumePending = true;
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
	// Recorded for every ability press, not just attacks -- the FACING LOCK correlation only
	// fires on attacks anyway, and a filter here would be one more thing to keep in step.
	NoteAimPress();
	// **The physical button, which nothing else in the trace can see.** Every other input line --
	// BUFFER, REFUSED -- describes what the system did with a press, so a *replayed* press and a
	// real one are indistinguishable once they reach an ability. That gap is why "a charged attack
	// when LMB was not held" could be narrowed to "always follows a buffered press fired as still
	// held" and no further: whether the player was really holding was simply not recorded anywhere.
	//
	// Both edges, because the question is always a duration rather than an instant.
	TD_TIMING_LOG(TEXT("[%.3f] INPUT      %s pressed on %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*InputTag.ToString(),
		*GetName());

	// Captured before anything can activate, because activation is what consumes it. A directional
	// dodge is one composite input -- the button and the heading -- so both halves are recorded at
	// the same instant, from the same frame's facing.
	CaptureMoveDirectionForPress();

	// Any new press supersedes a buffered one, whether or not this press succeeds and whatever
	// it was. Pressing something else says you have stopped waiting on the last thing -- and
	// without this a dodge buffered into a lockout could still surface after an attack the
	// player chose instead of it. Pressing the *same* thing again is unremarkable and is only
	// cleared here so it cannot outlive the press that replaced it.
	if (BufferedInput.IsSet())
	{
		if (!BufferedInput.InputTag.MatchesTagExact(InputTag))
		{
			TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s %s: dropped, superseded by %s"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f,
				*GetName(),
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

	// The heading rides with the press. Looking it up when the buffer fires would read whatever
	// the player happens to be holding then -- often nothing, since the lock that caused the
	// buffering is usually still up.
	BufferedInput.MoveAngleDegrees = PressMoveAngleDegrees;
	BufferedInput.bHadMoveInput = bPressHadMoveInput;

	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s %s: stored%s"), Now, *GetName(), *InputTag.ToString(),
		bPressHadMoveInput ? *FString::Printf(TEXT(", heading %.0f deg"), PressMoveAngleDegrees) : TEXT(", neutral"));
}

void ATDCombatCharacter::OnAbilityInputReleased(FGameplayTag InputTag)
{
	// The other physical edge; see the press for why this exists. Pair the two and the true button
	// timeline is readable, which is the only way to tell a lost release from a genuine long hold.
	TD_TIMING_LOG(TEXT("[%.3f] INPUT      %s released on %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*InputTag.ToString(),
		*GetName());

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

	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s %s: released after %.0fms held"),
		Now, *GetName(), *InputTag.ToString(), BufferedInput.HoldSeconds * 1000.0f);
}

bool ATDCombatCharacter::TryChainOutActiveAbility(const FGameplayTag& InputTag)
{
	if (!AbilitySystem)
	{
		return false;
	}

	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		if (!Spec || !Spec->IsActive())
		{
			continue;
		}

		// The primary instance, not Spec->Ability: policy questions read fine off a CDO, but
		// ending an activation needs the object that is actually running it.
		if (UTDGameplayAbility* Instance = Cast<UTDGameplayAbility>(Spec->GetPrimaryInstance()))
		{
			if (Instance->TryChainOutForBufferedPress())
			{
				return true;
			}
		}
	}

	return false;
}

void ATDCombatCharacter::CaptureMoveDirectionForPress()
{
	// LastRequestedMoveInput rather than the movement component's vector, deliberately: the
	// component's is empty for the whole of any ability that locks movement, which is precisely
	// when a cancel-into-dodge needs an answer.
	FVector Input = GetLastRequestedMoveInput();
	Input.Z = 0.0f;

	if (Input.IsNearlyZero())
	{
		PressMoveAngleDegrees = 0.0f;
		bPressHadMoveInput = false;
		return;
	}

	Input.Normalize();

	// Signed angle from *facing*, not from the camera: an ability asking "which way did they ask
	// to go" wants it relative to the body it is going to move.
	const FRotator Facing(0.0f, GetActorRotation().Yaw, 0.0f);
	const float ForwardDot = FVector::DotProduct(Input, Facing.Vector());
	const float RightDot = FVector::DotProduct(Input, FRotationMatrix(Facing).GetUnitAxis(EAxis::Y));

	PressMoveAngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(RightDot, ForwardDot));
	bPressHadMoveInput = true;
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

	// **Acceptance, not staleness.** The deadline is InputBufferSeconds past the *press* and never
	// moves: neither holding the button nor releasing it buys reach. A press outside that span is
	// not a decayed request, it is one made when presses are not accepted -- the same shape as
	// pressing during a knockdown's lockout rather than its input window.
	// Expired once a whole tick past the deadline, so the frame the deadline lands on is inside it.
	if (Now > BufferedInput.ExpiryWorldTime + 0.5f * World->GetDeltaSeconds())
	{
		TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s %s: expired, %.0fms after press"),
			Now, *GetName(), *BufferedInput.InputTag.ToString(), (Now - BufferedInput.PressWorldTime) * 1000.0f);
		BufferedInput.Clear();
		return;
	}

	// The chain-out: a swing in its chain-open span ends for the waiting press, here and not on
	// its own clock -- the *press* is what a chain is, so a swing with no press waiting runs its
	// full recovery and the delay-and-bait game lives in exactly that difference. Ending is
	// synchronous, so the retry below activates the next swing in this same tick; the buffer's
	// ordinary machinery does everything else.
	TryChainOutActiveAbility(BufferedInput.InputTag);

	// Restored before the retry, not after: an ability reads GetPressMoveDirection() during
	// activation, so the heading has to be the buffered one by the time activation runs. This is
	// what makes a late dodge go where it was aimed rather than where the stick is now.
	PressMoveAngleDegrees = BufferedInput.MoveAngleDegrees;
	bPressHadMoveInput = BufferedInput.bHadMoveInput;

	// Retried every frame rather than woken by an event. Every reason an activation can be
	// refused -- a blocking tag, a live instance, an ability ending, the airborne check --
	// would otherwise have to be enumerated and hooked, and missing one fails silently.
	// Polling cannot be incomplete, and there is at most one buffered input to retry.
	// Published before activating, because the ability reads it during ActivateAbility: how long
	// the button has been down for this press, which its ladder resumes counting from. A press
	// still held carries the time since it went down; one already released carries what it held
	// for. Either way the tier is decided by the whole hold, not the part after activation.
	PendingActivationHoldSeconds = BufferedInput.bReleased
		? BufferedInput.HoldSeconds
		: FMath::Max(0.0f, Now - BufferedInput.PressWorldTime);

	// **The hold's end travels with its length.** A press released before anything could answer it
	// has spent its release edge already, and no second one is coming -- an ability that started
	// believing the button was down would hold that belief forever and climb every rung of its
	// ladder on a press the player finished in 58 ms.
	bPendingActivationInputHeld = !BufferedInput.bReleased;

	if (!TryActivateAbilitiesForInput(BufferedInput.InputTag, /*bForwardToActive=*/false,
		/*bMarkInputPressed=*/!BufferedInput.bReleased))
	{
		PendingActivationHoldSeconds = 0.0f;
		bPendingActivationInputHeld = true;
		return;
	}

	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s %s: fired %.0fms late, %.0fms already held%s"),
		Now, *GetName(), *BufferedInput.InputTag.ToString(),
		(Now - BufferedInput.PressWorldTime) * 1000.0f,
		PendingActivationHoldSeconds * 1000.0f,
		BufferedInput.bReleased ? TEXT("") : TEXT(", still held"));

	PendingActivationHoldSeconds = 0.0f;
	bPendingActivationInputHeld = true;
	BufferedInput.Clear();
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
	const bool bWasHoming = bAimAssistHoming;
	AimAssistWedge = InWedge;
	AimAssistImmunityTags = InImmunityTags;
	bAimAssistHoming = bActive && InWedge.IsEnabled();
	bAimAssistDrawDebug = bInDrawDebug;
	if (bAimAssistHoming && !bWasHoming)
	{
		AimAssistOrigin = GetActorLocation();
	}

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
	float& OutBearingDegrees,
	const FVector* TravelOrigin)
{
	const UWorld* World = Attacker ? Attacker->GetWorld() : nullptr;
	if (!World || !Wedge.IsEnabled())
	{
		return nullptr;
	}

	const FVector Origin = Attacker->GetActorLocation();

	// The reach left after the travel from where the swing began; the arc still reads from here.
	FTDAttackHitbox Remaining = Wedge;
	if (TravelOrigin)
	{
		Remaining.MaxReachCm = FMath::Max(0.0f, Wedge.MaxReachCm - FVector::Dist2D(Origin, *TravelOrigin));
		if (!Remaining.IsEnabled())
		{
			OutBearingDegrees = 0.0f;
			return nullptr;
		}
	}

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(Remaining.GetBroadPhaseRadiusCm()),
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

		if (!Remaining.OverlapsCapsule(Origin, AimYawDegrees, TargetCentre, TargetRadius, Capsule->GetScaledCapsuleHalfHeight()))
		{
			continue;
		}

		float Bearing = 0.0f;
		float HalfArc = 0.0f;
		if (!Remaining.GetBearingToCapsule(Origin, AimYawDegrees, TargetCentre, TargetRadius, Bearing, HalfArc))
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
	const AActor* Target = FindAimAssistTarget(this, AimYaw, AimAssistWedge, AimAssistImmunityTags, Bearing,
		&AimAssistOrigin);
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

void ATDCombatCharacter::DebugGetUpPress()
{
	FGameplayTag Tag;
	float HoldSeconds = 0.05f;
	const TCHAR* Mode = TEXT("wait");
	switch (DebugGetUpMode)
	{
	case ETDDebugGetUpMode::AttackGetUp: Tag = DebugAutoAttackInputTag;  Mode = TEXT("attack"); break;
	case ETDDebugGetUpMode::DodgeGetUp:  Tag = DebugDefendDodgeInputTag; Mode = TEXT("dodge");  break;
	case ETDDebugGetUpMode::StandGetUp:  Tag = DebugJumpInputTag;        Mode = TEXT("stand");  break;
	case ETDDebugGetUpMode::BlockGetUp:
		Tag = DebugDefendBlockInputTag;
		Mode = TEXT("block");
		// Held through the rise and a little past the stand, so the guard is up when it matters.
		HoldSeconds = KnockdownRiseSeconds + 0.25f;
		break;
	default:
		return;
	}

	if (!Tag.IsValid())
	{
		UE_LOG(LogTDCombatTiming, Warning, TEXT("%s: DebugGetUpMode %s has no input tag set on this pawn."), *GetName(), Mode);
		return;
	}

	TD_TIMING_LOG(TEXT("[%.3f] DEBUG GETUP  %s mode=%s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, *GetName(), Mode);

	DebugGetUpHeldTag = Tag;
	OnAbilityInputPressed(Tag);
	GetWorldTimerManager().SetTimer(DebugGetUpReleaseTimerHandle, this, &ATDCombatCharacter::DebugGetUpRelease, HoldSeconds, false);
}

void ATDCombatCharacter::DebugGetUpRelease()
{
	if (DebugGetUpHeldTag.IsValid())
	{
		OnAbilityInputReleased(DebugGetUpHeldTag);
	}
}
