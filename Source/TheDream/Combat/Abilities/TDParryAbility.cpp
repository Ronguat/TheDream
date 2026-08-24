// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDParryAbility.h"
#include "Combat/TDCombatCharacter.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "Combat/Notifies/AnimNotify_ParryGesture.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

namespace
{
	/**
	 *  Play rates are derived from authored durations, so a degenerate authored value -- a zero
	 *  window, a marker on frame 0 -- can produce a rate of zero or worse, which freezes the montage
	 *  rather than failing visibly. Same clamp and same value as the attack ladder's.
	 */
	constexpr float TDMinParryPlayRate = 0.01f;
}

UTDParryAbility::UTDParryAbility()
{
	// LocalPredicted like every other ability here, and the parry needs it most: the window is
	// 300 ms and a round trip spends a meaningful fraction of it, so an unpredicted parry would open
	// after the read that justified it. What the server decides is whether a hit was negated, and
	// that stays authority-side in the attacker's hit path.
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	// **Planted, unlike the guard.** The split is action versus state: actions own their
	// displacement, while a state you carry leaves you mobile, which is why GA_Block is the one
	// defensive ability that does not take this. A parrier free to drift while the attacker is
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
	// The base applies EffectOnStart, takes the movement lock this ability asked for above, and runs
	// the refusals shared by every ability.
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Pushed to the character rather than tracked here, because the window has to be readable by
	// *someone else's* ability: the attacker's HandleTraceHit asks whether it is open, and it has a
	// character pointer, not an ability one.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		Character->OpenParryWindow(ParryWindowSeconds, ParryWhiffRecoverySeconds);
	}

	PlayParryMontage();
}

void UTDParryAbility::PlayParryMontage()
{
	// **The parry works with no montage, and nothing here may become load-bearing**: every early
	// return below leaves a fully functional parry behind it.
	if (!ParryMontage)
	{
		return;
	}

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return;
	}

	const float Length = ParryMontage->GetPlayLength();
	if (Length <= KINDA_SMALL_NUMBER || ParryWindowSeconds <= 0.0f)
	{
		return;
	}

	const float GestureTime = FindGestureTime();

	// Plain division: this segment is nowhere near the montage's end, so the blend-out boundary that
	// complicates the recovery rate below does not reach it.
	float WindowRate = Length / (ParryWindowSeconds + ParryWhiffRecoverySeconds);

	if (GestureTime > KINDA_SMALL_NUMBER)
	{
		WindowRate = FMath::Max(GestureTime / ParryWindowSeconds, TDMinParryPlayRate);
	}
	else
	{
		// Ungated. A missing marker is an authoring omission no readable property can reveal --
		// notify placement is invisible to every tool we have -- and its symptom in play is merely
		// "the parry animation looks a bit off", which nobody reports as a fault. The fallback is
		// legible rather than correct: one rate across the whole span.
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("%s has no Parry Gesture notify; playing the whole clip across %.3fs at one rate. "
			     "The window/recovery split cannot be honoured without the marker."),
			*ParryMontage->GetName(), ParryWindowSeconds + ParryWhiffRecoverySeconds);
	}

	// **Both rates are fully determined here, which is what lets the recovery rate outlive this
	// ability.** A catch ends GA_Parry at the instant it lands, so anything waiting for the marker
	// at runtime would be gone before it arrived and the tail would play at the window's rate.
	// Nothing here needs the playhead: the marker's trigger time *is* the position it will be at.
	//
	// **The blend-out boundary is why this is not (Length - Gesture) / Recovery.** UE begins the
	// automatic blend-out before the montage's end, so a naive rate runs the segment long. Same
	// arithmetic UTDChargedAttackAbility::ComputeRecoveryPlayRate derives; only the boundary differs.
	float RecoveryRate = -1.0f;
	if (GestureTime > KINDA_SMALL_NUMBER && ParryWhiffRecoverySeconds > 0.0f)
	{
		const float TriggerTime = ParryMontage->BlendOutTriggerTime;
		if (TriggerTime >= 0.0f)
		{
			// Explicit trigger: a fixed position, so it is rate-immune and the rate follows directly.
			// AM_Parry sets one precisely so the clip's recovery tail is not blended away.
			const float ToBoundary = (Length - TriggerTime) - GestureTime;
			RecoveryRate = (ToBoundary <= KINDA_SMALL_NUMBER)
				? TDMinParryPlayRate
				: FMath::Max(ToBoundary / ParryWhiffRecoverySeconds, TDMinParryPlayRate);
		}
		else
		{
			// Rate-dependent boundary: the blend begins BlendTime*R before the end, so it cancels
			// out of the position but not out of the time. R = Remaining / (Target + BlendTime).
			const float BlendTime = ParryMontage->BlendOut.GetBlendTime();
			const float Remaining = Length - GestureTime;
			RecoveryRate = FMath::Max(Remaining / (ParryWhiffRecoverySeconds + BlendTime), TDMinParryPlayRate);
		}
	}

	// Parked on the character because that is what the notify can reach, and what survives this
	// ability ending. See ATDCombatCharacter::GetPendingParryMontageRecoveryRate.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		Character->SetPendingParryMontageRecoveryRate(RecoveryRate);
	}

	// **Played straight through the AnimInstance rather than through PlayMontageAndWait.** A
	// successful parry frees the parrier at the instant of the catch, which can be 0 ms in, and the
	// clip is expected to play on and be overridden by whatever they do next. The ability task ends
	// its montage when the ability ends, which would snap the gesture away at the exact moment it
	// succeeded. Nothing here needs the task's callbacks either: the parry's length is authored,
	// never taken from the clip.
	AnimInstance->Montage_Play(ParryMontage, WindowRate);

	TD_TIMING_LOG(TEXT("[%.3f] PARRY MONTAGE %s  len=%.3f gesture=%.4f windowRate=%.3f recoveryRate=%.3f window=%.3f recovery=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*ParryMontage->GetName(), Length, GestureTime, WindowRate, RecoveryRate,
		ParryWindowSeconds, ParryWhiffRecoverySeconds);
}

float UTDParryAbility::FindGestureTime() const
{
	if (!ParryMontage)
	{
		return -1.0f;
	}

	// Notifies are readable here even though the MCP toolset cannot see them; that limit is the
	// toolset's, not the engine's.
	for (const FAnimNotifyEvent& Event : ParryMontage->Notifies)
	{
		if (Event.Notify && Event.Notify->IsA<UAnimNotify_ParryGesture>())
		{
			return Event.GetTriggerTime();
		}
	}

	return -1.0f;
}

void UTDParryAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		// **This deliberately does not close the window** -- "parry is sacred". An ability ending
		// with its window still open means something cancelled a committed parry, which the design
		// says can never happen: the only ways out are expiry, a catch, and death, and death closes
		// the window itself before cancelling.
		//
		// **The window is left running rather than torn down**, the protection living on the
		// character and surviving its ability perfectly well. Tick expires it on schedule and bills
		// the whiff as usual, so the parry still resolves honestly; what is lost is the lockout, the
		// lesser harm and the one that fails visibly.
		//
		// Ungated, because this is the sacredness violation detector and there is nothing else.
		if (Character->IsParryWindowOpen())
		{
			UE_LOG(LogTDCombatTiming, Warning,
				TEXT("GA_Parry ended on %s with its window still open -- something cancelled a "
				     "committed parry, which 'parry is sacred' forbids. The window is left running."),
				*GetNameSafe(Character));
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UTDParryAbility::ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const
{
	return false;
}
