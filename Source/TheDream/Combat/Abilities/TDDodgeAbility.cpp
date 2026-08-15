// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDDodgeAbility.h"
#include "Combat/TDCombatCharacter.h"
#include "Core/TheDreamCharacter.h"
#include "Combat/TDCombatDebug.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	/** Montage section per direction. The montage must use these exact names. */
	FName SectionForDirection(ETDDodgeDirection Direction)
	{
		switch (Direction)
		{
		case ETDDodgeDirection::Fw: return FName("Fw");
		case ETDDodgeDirection::FR: return FName("FR");
		case ETDDodgeDirection::R:  return FName("R");
		case ETDDodgeDirection::BR: return FName("BR");
		case ETDDodgeDirection::Bw: return FName("Bw");
		case ETDDodgeDirection::BL: return FName("BL");
		case ETDDodgeDirection::L:  return FName("L");
		case ETDDodgeDirection::FL: return FName("FL");
		}
		return NAME_None;
	}

	/**
	 *  Eight 45-degree sectors, centred on the cardinals so straight input never lands on a
	 *  boundary. Enum order is clockwise from forward, which is what makes this a lookup
	 *  rather than a second switch.
	 */
	ETDDodgeDirection DirectionForAngle(float SignedDegrees)
	{
		const int32 Sector = FMath::RoundToInt(SignedDegrees / 45.0f);
		const int32 Index = ((Sector % 8) + 8) % 8;
		return static_cast<ETDDodgeDirection>(Index);
	}
}

void UTDDodgeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// The stamina cost is already paid -- Super applied EffectOnStart. There is deliberately
	// no commit check here: a dodge is never refused for want of stamina, it empties the bar
	// and exhausts you. See Docs/Combat-Decisions.md, "Costs are paid, not required".
	UWorld* World = GetWorld();
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	DodgeDirection = ResolveDodgeDirection();

	// Facing freezes for the whole dodge, so the direction resolved a line ago is the direction
	// travelled. Root motion carries the character along the montage's authored path *relative to
	// its facing*, so a character free to turn mid-dodge steers the dodge itself.
	//
	// **This became necessary rather than merely correct on 2026-08-12**, when
	// bAllowPhysicsRotationDuringAnimRootMotion was enabled to fix attacks: until then the engine
	// suppressed rotation during any root-motion montage, and the dodge inherited a committed
	// direction it had never asked for. Play found the difference immediately -- steerable dodges
	// read as too much control for a move that costs half the stamina bar.
	//
	// Set after ResolveDodgeDirection deliberately; that call reads facing, and locking first
	// would only freeze the same value it is about to use.
	if (ATheDreamCharacter* Character = Cast<ATheDreamCharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->SetAbilityFacingLocked(true);
	}

	if (const AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		DodgeStartLocation = Avatar->GetActorLocation();
	}

	// **Authored displacement, as of 2026-08-13**, replacing the montage's root motion and the
	// eight per-direction scales that existed to correct it. The clips disagreed by 90.6 cm about
	// how far a dodge carries, so every one of those scales was a measurement of an animator's
	// choice; now all eight directions travel DodgeTargetDistanceCm because that is the number.
	//
	// **The eight yaw offsets are the enum's own order, not a table.** ETDDodgeDirection runs
	// Fw, FR, R, BR, Bw, BL, L, FL -- clockwise at 45 degrees a step -- so the offset is the
	// index times 45, and a direction cannot be given the wrong angle without being in the wrong
	// place in the compass.
	//
	// Standoff is deliberately 0: Target Lock's gate belongs to attacks. An evade has to be able
	// to travel *past* people, and gating it on pawns would break dodging through a crowd.
	StartLunge(
		DodgeTargetDistanceCm,
		DodgeSeconds,
		/*StrengthCurve=*/nullptr,
		/*StandoffCm=*/0.0f,
		static_cast<uint8>(DodgeDirection) * 45.0f);

	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->DebugStatusLine = FString::Printf(TEXT("Dodge %s  %.2fs  invulnerable"),
			*SectionForDirection(DodgeDirection).ToString(), DodgeSeconds);
	}

	// No timer: the i-frames are the ability. EndAbility takes the tag off, and since
	// DodgeSeconds is what ends the ability, the two cannot drift apart.
	if (IFrameTag.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->AddLooseGameplayTag(IFrameTag);
			bIFramesActive = true;
		}
	}

	// Read *after* Super::ActivateAbility, which is where EffectOnStart charged the cost, so this
	// is the bar the dodge left behind rather than the one it found. Parity with BLOCK cost's
	// remaining=: the two together are what make an unattended stamina ledger readable, and a
	// dodge from full must read exactly 50. -1 marks "no combatant", which cannot be a real bar.
	const ATDCombatCharacter* Combatant = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo());
	const float StaminaRemaining = Combatant ? Combatant->GetStamina() : -1.0f;

	if (DodgeMontage)
	{
		const FName Section = SectionForDirection(DodgeDirection);
		const int32 SectionIndex = DodgeMontage->GetSectionIndex(Section);

		// Derived from the *section*, not the montage: an eight-section montage's total
		// length is eight rolls, and dividing by that would play each one at a crawl.
		float PlayRate = 1.0f;
		if (SectionIndex != INDEX_NONE)
		{
			const float SectionLength = DodgeMontage->GetSectionLength(SectionIndex);
			if (SectionLength > 0.0f && DodgeSeconds > 0.0f)
			{
				PlayRate = FMath::Max(SectionLength / DodgeSeconds, 0.01f);
			}
		}
		else
		{
			// Ungated: a mistyped section name plays the wrong roll at the wrong speed,
			// which reads as a tuning problem rather than the authoring error it is.
			UE_LOG(LogTDCombatTiming, Warning,
				TEXT("Dodge montage %s has no section '%s'; playing from the start at rate 1."),
				*DodgeMontage->GetName(), *Section.ToString());
		}

		// Scale 1.0, and it is inert: the eight source clips have bEnableRootMotion switched off,
		// so the montage carries no root motion for this to scale. Displacement is authored below.
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, DodgeMontage, PlayRate, Section, /*bStopWhenAbilityEnds=*/true, /*AnimRootMotionTranslationScale=*/1.0f);

		// Deliberately not ending the ability on completion. DodgeSeconds is the authority,
		// and a montage whose sections chain into the next one would otherwise decide the
		// dodge's length by how the asset happens to be linked.
		MontageTask->OnInterrupted.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->OnCancelled.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->ReadyForActivation();

		// rate x DodgeSeconds should equal the section's authored length. If it does not,
		// the section resolved to something other than the roll that was intended.
		TD_TIMING_LOG(TEXT("[%.3f] DODGE      dir=%s section=%s sectionLen=%.3f rate=%.3f want=%.3fs remaining=%.1f"),
			World->GetTimeSeconds(), *UEnum::GetValueAsString(DodgeDirection), *Section.ToString(),
			(SectionIndex != INDEX_NONE) ? DodgeMontage->GetSectionLength(SectionIndex) : -1.0f,
			PlayRate, DodgeSeconds, StaminaRemaining);
	}
	else
	{
		// Traced even with no montage. The montage is optional and is currently unset, so
		// gating the only record that a dodge happened on having one made the ability
		// invisible to the trace exactly while it was the thing under test.
		TD_TIMING_LOG(TEXT("[%.3f] DODGE      dir=%s want=%.3fs remaining=%.1f (no montage)"),
			World->GetTimeSeconds(), *UEnum::GetValueAsString(DodgeDirection), DodgeSeconds, StaminaRemaining);
	}

	World->GetTimerManager().SetTimer(
		DodgeTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]() { HandleDodgeFinished(); }),
		DodgeSeconds,
		false);
}

ETDDodgeDirection UTDDodgeAbility::ResolveDodgeDirection() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return ETDDodgeDirection::Bw;
	}

	// The frame's input, not the current velocity: velocity still carries the previous
	// direction for a moment after the stick is released, which would send a dodge the way
	// the player has just stopped going.
	FVector Input = Movement->GetLastInputVector();
	Input.Z = 0.0f;

	if (Input.IsNearlyZero())
	{
		// Standing still dodges backward. A neutral dodge that goes nowhere reads as a
		// flinch, and backward is the direction that buys spacing.
		return ETDDodgeDirection::Bw;
	}

	Input.Normalize();

	const FRotator Facing(0.0f, Character->GetActorRotation().Yaw, 0.0f);
	const float ForwardDot = FVector::DotProduct(Input, Facing.Vector());
	const float RightDot = FVector::DotProduct(Input, FRotationMatrix(Facing).GetUnitAxis(EAxis::Y));

	// Signed angle from facing: 0 is forward, +90 right, -90 left, 180 back.
	return DirectionForAngle(FMath::RadiansToDegrees(FMath::Atan2(RightDot, ForwardDot)));
}

void UTDDodgeAbility::EndIFrames()
{
	if (!bIFramesActive)
	{
		return;
	}
	bIFramesActive = false;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(IFrameTag);
	}
}

void UTDDodgeAbility::HandleDodgeFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UTDDodgeAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DodgeTimerHandle);
	}

	// Facing comes back here, where every exit converges -- the duration timer, a cancel, and the
	// CancelAllAbilities that death fires. Idempotent, so running twice costs nothing, and the
	// abnormal paths cannot be missed. A stranded lock is a character who can never turn again.
	if (ATheDreamCharacter* Character = Cast<ATheDreamCharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->SetAbilityFacingLocked(false);
	}

	// Horizontal only: vertical travel is gravity and ground, not the dodge, and including it
	// would make every dodge down a slope read as longer. Reported here rather than on the
	// timer so a cancelled dodge still reports what it managed -- a dodge cut short by a
	// cancel travelling nearly its full distance would be worth knowing about.
	//
	// Guarded on the start location being set, and clearing it, because EndAbility can run
	// more than once -- the same reason EndIFrames is idempotent. A measurement that
	// double-reports is worse than none: the duplicates look like real samples and quietly
	// weight any average taken over them.
	if (!DodgeStartLocation.IsZero())
	{
		if (const AActor* Avatar = GetAvatarActorFromActorInfo())
		{
			const FVector Delta = Avatar->GetActorLocation() - DodgeStartLocation;

			// **Logged as a vector in the avatar's own frame, not as a magnitude.** It printed
			// Delta.Size2D() until 2026-08-13, and that is how a dodge travelling ninety degrees
			// from its intended direction produced a line indistinguishable from a perfect one --
			// the instrument answered "how far", correctly, while the question was "which way".
			//
			// Every number this system was ever tuned on had the same hole: MeasuredTravelCm was
			// captured the same way, so the eight scales corrected distances nobody had checked the
			// direction of. Right is +Y and forward is +X, so a left dodge should read fwd~0
			// right~-405, and any other shape is the bug announcing itself.
			const FVector Local = Avatar->GetActorTransform().InverseTransformVectorNoScale(Delta);

			TD_TIMING_LOG(TEXT("[%.3f] DODGE END  dir=%s fwd=%+.1f right=%+.1f up=%+.1f dist=%.1fuu yaw=%.0f%s"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f,
				*UEnum::GetValueAsString(DodgeDirection),
				Local.X,
				Local.Y,
				Local.Z,
				Delta.Size2D(),
				static_cast<uint8>(DodgeDirection) * 45.0f,
				bWasCancelled ? TEXT(" (cancelled)") : TEXT(""));
		}

		DodgeStartLocation = FVector::ZeroVector;
	}

	// Must come off even when the dodge is cancelled, or a cancelled dodge leaves the
	// character permanently invulnerable -- the defensive equivalent of a stuck State tag.
	EndIFrames();

	// Cleared here for the same reason: a status line that outlives its ability describes
	// something that is no longer happening, which is worse than showing nothing.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->DebugStatusLine.Reset();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
