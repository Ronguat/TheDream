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

	// Resolved once, before the direction is, because a kip-up does not consult held input at all.
	// Read off the *character's* type rather than stored on the ability, so neither flag can be
	// left armed on the next ordinary dodge.
	bIsKnockdownGetUp = false;
	bIsKnockdownKipUp = false;
	if (const ATDCombatCharacter* Downed = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		bIsKnockdownGetUp = Downed->IsKnockedDown();
		bIsKnockdownKipUp = bIsKnockdownGetUp && Downed->GetKnockdownType() == ETDKnockdownType::Hard;
	}

	DodgeDirection = bIsKnockdownKipUp ? ETDDodgeDirection::Bw : ResolveDodgeDirection();

	// Facing freezes for the whole dodge, so the direction resolved a line ago is the direction
	// travelled -- a character free to turn mid-dodge steers the dodge itself. The engine no longer
	// suppresses rotation during root-motion montages (bAllowPhysicsRotationDuringAnimRootMotion),
	// so anything wanting a committed direction has to say so. Set after ResolveDodgeDirection
	// deliberately; that call reads facing, and locking first would freeze the value it is about
	// to use.
	// **The get-up roll turns to face where it travels; nothing else in the project does.** It is
	// one forward clip where an ordinary dodge has eight directional sections, so the body has
	// to come round to its heading -- turned rather than snapped, because the turn is the motion
	// an opponent reads the heading from. StartLunge takes the direction at activation, so the
	// travel does not follow the body round.
	const float TravelYawOffset = static_cast<uint8>(DodgeDirection) * 45.0f;
	const bool bRollTurnsToTravel = bIsKnockdownGetUp && !bIsKnockdownKipUp;

	if (ATheDreamCharacter* Character = Cast<ATheDreamCharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->SetAbilityFacingLocked(true);
	}

	if (const AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		DodgeStartLocation = Avatar->GetActorLocation();
	}

	// Authored displacement: all eight directions travel DodgeTargetDistanceCm.
	//
	// **The eight yaw offsets are the enum's own order, not a table.** ETDDodgeDirection runs Fw,
	// FR, R, BR, Bw, BL, L, FL -- clockwise at 45 degrees a step -- so the offset is the index
	// times 45, and a direction cannot be given the wrong angle without being in the wrong place in
	// the compass. Standoff is deliberately 0: Target Lock's gate belongs to attacks, an evade has
	// to travel *past* people, and gating it on pawns would break dodging through a crowd.
	//
	// **Kip-up: the same ability, stationary.** From a hard knockdown the directional dodge is
	// removed and this replaces it -- i-framed and full-cost like any dodge, but travelling nothing
	// and ignoring held direction, so hard's narrow input window cannot also buy repositioning. It
	// is the one exception to authored displacement: the kip-up clip keeps its own root motion,
	// which suppresses the authored source, so the zero passed here is moot.
	const bool bKipUp = bIsKnockdownKipUp;

	StartLunge(
		bKipUp ? 0.0f : DodgeTargetDistanceCm,
		DodgeSeconds,
		/*StrengthCurve=*/nullptr,
		/*StandoffCm=*/0.0f,
		bKipUp ? 0.0f : TravelYawOffset,
		bRollTurnsToTravel ? RollTurnRateDegrees : 0.0f);
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

	// **Which clip, decided before which section.** A get-up brings its own single-segment montage
	// -- the kip-up on hard, the roll on normal -- and neither has the eight directional sections
	// DodgeMontage is built around, so they play from the start with no section name. Falling back
	// to DodgeMontage when a get-up clip is unset is deliberate: a missing asset should read as the
	// wrong animation, not as a dodge that does not come out.
	UAnimMontage* MontageToPlay = DodgeMontage;
	bool bUseSection = true;
	if (bIsKnockdownKipUp && KipUpMontage)
	{
		MontageToPlay = KipUpMontage;
		bUseSection = false;
	}
	else if (bIsKnockdownGetUp && KnockdownRollMontage)
	{
		MontageToPlay = KnockdownRollMontage;
		bUseSection = false;
	}

	if (MontageToPlay)
	{
		const FName Section = bUseSection ? SectionForDirection(DodgeDirection) : NAME_None;
		const int32 SectionIndex = bUseSection ? MontageToPlay->GetSectionIndex(Section) : INDEX_NONE;

		// Derived from the *section* where there is one, because an eight-section montage's total
		// length is eight rolls and dividing by that would play each at a crawl. A get-up montage
		// has one segment, so there is no boundary to fit to and KnockdownRollSeconds marks where
		// the roll ends by hand -- the same fit, applied where the asset carries no seam.
		// **Both paths fit a portion rather than the whole**: a section boundary marks where the
		// clip ends, not where the dash does. A zero-length section leaves FitLength zero and
		// falls through to the warning below.
		float PlayRate = 1.0f;
		const float RollPortion = (bIsKnockdownGetUp && !bIsKnockdownKipUp && KnockdownRollSeconds > 0.0f)
			? FMath::Min(KnockdownRollSeconds, MontageToPlay->GetPlayLength())
			: MontageToPlay->GetPlayLength();
		const float SectionLength = (bUseSection && SectionIndex != INDEX_NONE)
			? MontageToPlay->GetSectionLength(SectionIndex)
			: 0.0f;
		const float DashPortion = (DodgeClipSeconds > 0.0f)
			? FMath::Min(DodgeClipSeconds, SectionLength)
			: SectionLength;
		const float FitLength = bUseSection ? DashPortion : RollPortion;
		if (FitLength > 0.0f && DodgeSeconds > 0.0f)
		{
			PlayRate = FMath::Max(FitLength / DodgeSeconds, 0.01f);
		}
		else if (bUseSection)
		{
			// Ungated: a mistyped section name plays the wrong roll at the wrong speed,
			// which reads as a tuning problem rather than the authoring error it is.
			UE_LOG(LogTDCombatTiming, Warning,
				TEXT("Dodge montage %s has no section '%s'; playing from the start at rate 1."),
				*MontageToPlay->GetName(), *Section.ToString());
		}

		// Scale 1.0. Inert for the standing rolls and the knockdown roll, whose clips have
		// bEnableRootMotion off so there is nothing to scale -- displacement is authored below.
		// **The kip-up is the exception**: its clip carries root motion by ruling, which suppresses
		// the authored source entirely, so the clip is the travel and StartLunge's zero is moot.
		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, MontageToPlay, PlayRate, Section, /*bStopWhenAbilityEnds=*/true, /*AnimRootMotionTranslationScale=*/1.0f);

		// Deliberately not ending the ability on completion. DodgeSeconds is the authority,
		// and a montage whose sections chain into the next one would otherwise decide the
		// dodge's length by how the asset happens to be linked.
		MontageTask->OnInterrupted.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->OnCancelled.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->ReadyForActivation();

		// rate x DodgeSeconds should equal the fitted length. If it does not, the section resolved
		// to something other than the roll that was intended.
		TD_TIMING_LOG(TEXT("[%.3f] DODGE      %s dir=%s section=%s clip=%s fitLen=%.3f rate=%.3f want=%.3fs remaining=%.1f"),
			World->GetTimeSeconds(), *GetNameSafe(GetAvatarActorFromActorInfo()), *UEnum::GetValueAsString(DodgeDirection), *Section.ToString(),
			*MontageToPlay->GetName(), FitLength, PlayRate, DodgeSeconds, StaminaRemaining);
	}
	else
	{
		// Traced even with no montage. The montage is optional and is currently unset, so
		// gating the only record that a dodge happened on having one made the ability
		// invisible to the trace exactly while it was the thing under test.
		TD_TIMING_LOG(TEXT("[%.3f] DODGE      %s dir=%s want=%.3fs remaining=%.1f (no montage)"),
			World->GetTimeSeconds(), *GetNameSafe(GetAvatarActorFromActorInfo()), *UEnum::GetValueAsString(DodgeDirection), DodgeSeconds, StaminaRemaining);
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
	const ATDCombatCharacter* Character = ActorInfo ? Cast<ATDCombatCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character)
	{
		return ETDDodgeDirection::Bw;
	}

	// **The heading captured when the button went down, not the movement component's vector.** That
	// vector is empty for the whole of any ability that locks movement, so reading it makes every
	// dodge cancelling an attack resolve to the standing-still default. Press-time also settles the
	// buffered case the way the player means it: release the key inside the buffer window and the
	// dodge still goes where you aimed it, the heading having been one half of a composite input
	// rather than something looked up later.
	float AngleDegrees = 0.0f;
	if (!Character->GetPressMoveDirection(AngleDegrees))
	{
		// Standing still dodges backward. A neutral dodge that goes nowhere reads as a
		// flinch, and backward is the direction that buys spacing.
		return ETDDodgeDirection::Bw;
	}

	// Signed angle from facing: 0 is forward, +90 right, -90 left, 180 back.
	return DirectionForAngle(AngleDegrees);
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

	// Horizontal only: vertical travel is gravity and ground, not the dodge, and including it would
	// make every dodge down a slope read as longer. Reported here rather than on the timer so a
	// cancelled dodge still reports what it managed.
	//
	// Guarded on the start location being set, and clearing it, because EndAbility can run more than
	// once -- the same reason EndIFrames is idempotent. A measurement that double-reports is worse
	// than none: the duplicates look like real samples and quietly weight any average over them.
	if (!DodgeStartLocation.IsZero())
	{
		if (const AActor* Avatar = GetAvatarActorFromActorInfo())
		{
			const FVector Delta = Avatar->GetActorLocation() - DodgeStartLocation;

			// **Logged as a vector in the avatar's own frame, not as a magnitude.** A magnitude answers
			// "how far", correctly, while the question is "which way" -- a dodge travelling ninety
			// degrees off its intended direction reads identically to a perfect one. Right is +Y and
			// forward is +X, so a left dodge should read fwd~0 right~-405, and any other shape is the
			// bug announcing itself.
			const FVector Local = Avatar->GetActorTransform().InverseTransformVectorNoScale(Delta);

			TD_TIMING_LOG(TEXT("[%.3f] DODGE END  %s dir=%s fwd=%+.1f right=%+.1f up=%+.1f dist=%.1fuu yaw=%.0f%s"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f,
				*GetNameSafe(GetAvatarActorFromActorInfo()),
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

	// **The post-dodge gap, and it is derived rather than felt.** Without it a *predictive* dodge
	// chains straight into a parry whose window covers the charged's arrival: the dodge eats the
	// fast layer on a guess, and the parry covers the slow one for free, so the vise's late jaw
	// unscrews and the charged stops collecting on a wrong read. The constraint is that dodge-end
	// plus this gap plus the parry window must overshoot 750 for the worst-timed predictive dodge.
	//
	// Expressed as a tag rather than a bespoke timestamp consulted in CanActivateAbility, so the
	// refusal is visible where every other refusal is. Its own tag rather than State.ParryRecovery,
	// which commits the character outright where this takes nothing but the parry.
	//
	// Applied on *every* exit including a cancel, deliberately: a dodge cut short still bought its
	// i-frames, and letting a cancel skip the gap would make cancelling the cheap route to the
	// chain this forbids. The dodge itself needs no cover, GA_Parry already refusing to activate
	// while State.Dodging is present.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->ApplyDodgeRecovery(DodgeRecoverySeconds);
	}

	// Cleared here for the same reason: a status line that outlives its ability describes
	// something that is no longer happening, which is worse than showing nothing.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->DebugStatusLine.Reset();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

const TCHAR* UTDDodgeAbility::GetKnockdownRiseLabel(const ATDCombatCharacter* Character) const
{
	// Two labels from one ability, which is why the base asks rather than storing a constant: the
	// scenarios assert kip-up travel is about zero and dodge travel is not, and they need to know
	// which one they are looking at.
	return (Character && Character->GetKnockdownType() == ETDKnockdownType::Hard)
		? TEXT("kipup")
		: TEXT("dodge");
}
