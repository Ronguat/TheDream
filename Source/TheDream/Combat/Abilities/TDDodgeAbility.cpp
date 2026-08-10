// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDDodgeAbility.h"
#include "Combat/TDCombatCharacter.h"
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

		UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, DodgeMontage, PlayRate, Section);

		// Deliberately not ending the ability on completion. DodgeSeconds is the authority,
		// and a montage whose sections chain into the next one would otherwise decide the
		// dodge's length by how the asset happens to be linked.
		MontageTask->OnInterrupted.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->OnCancelled.AddDynamic(this, &UTDDodgeAbility::HandleDodgeFinished);
		MontageTask->ReadyForActivation();

		// rate x DodgeSeconds should equal the section's authored length. If it does not,
		// the section resolved to something other than the roll that was intended.
		TD_TIMING_LOG(TEXT("[%.3f] DODGE      dir=%s section=%s sectionLen=%.3f rate=%.3f want=%.3fs"),
			World->GetTimeSeconds(), *UEnum::GetValueAsString(DodgeDirection), *Section.ToString(),
			(SectionIndex != INDEX_NONE) ? DodgeMontage->GetSectionLength(SectionIndex) : -1.0f,
			PlayRate, DodgeSeconds);
	}
	else
	{
		// Traced even with no montage. The montage is optional and is currently unset, so
		// gating the only record that a dodge happened on having one made the ability
		// invisible to the trace exactly while it was the thing under test.
		TD_TIMING_LOG(TEXT("[%.3f] DODGE      dir=%s want=%.3fs (no montage)"),
			World->GetTimeSeconds(), *UEnum::GetValueAsString(DodgeDirection), DodgeSeconds);
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
