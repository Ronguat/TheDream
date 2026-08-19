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
	 *  Play rates are derived from authored durations, so a degenerate authored value (a zero
	 *  window, a marker on frame 0) can produce a rate of zero or worse -- which freezes the
	 *  montage rather than failing visibly. Clamped for the same reason the attack ladder clamps
	 *  its own, and to the same value.
	 */
	constexpr float TDMinParryPlayRate = 0.01f;
}

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
		Character->OpenParryWindow(ParryWindowSeconds, ParryWhiffRecoverySeconds);
	}

	PlayParryMontage();
}

void UTDParryAbility::PlayParryMontage()
{
	// **The parry works with no montage, and that is deliberate rather than tolerated.** It shipped
	// felt-not-seen exactly as blockstun did, so nothing here may become load-bearing: every early
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

	// The window segment's rate. Plain division: this segment is nowhere near the montage's end, so
	// the blend-out boundary that complicates the recovery rate below does not reach it.
	float WindowRate = Length / (ParryWindowSeconds + ParryWhiffRecoverySeconds);

	if (GestureTime > KINDA_SMALL_NUMBER)
	{
		WindowRate = FMath::Max(GestureTime / ParryWindowSeconds, TDMinParryPlayRate);
	}
	else
	{
		// Ungated. A missing marker is an authoring omission that no readable property can reveal
		// -- notify placement is invisible to every tool we have -- and its symptom in play is
		// merely "the parry animation looks a bit off", which nobody would report as a fault. The
		// fallback below is legible rather than correct: one rate across the whole span, so the
		// clip at least starts and ends where the mechanic does.
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("%s has no Parry Gesture notify; playing the whole clip across %.3fs at one rate. "
			     "The window/recovery split cannot be honoured without the marker."),
			*ParryMontage->GetName(), ParryWindowSeconds + ParryWhiffRecoverySeconds);
	}

	// **Both rates are fully determined right here, which is what lets the recovery rate outlive
	// this ability** -- the designer's ruling that the authored recovery rate is *always* the one
	// used (2026-08-19). A catch ends GA_Parry at the instant it lands, so anything that waited for
	// the marker at runtime would be gone before the marker arrived, and the tail would play at the
	// window's rate instead. Nothing here needs the playhead: the marker's trigger time *is* the
	// position it will be at.
	//
	// **The blend-out boundary is why this is not (Length - Gesture) / Recovery.** UE begins the
	// automatic blend-out before the montage's end, so a naive rate runs the segment long -- by 6%
	// at rate 0.94 and by 49% at 0.50 when this was found on the attack ladder in 2026-08-12. Same
	// arithmetic UTDChargedAttackAbility::ComputeRecoveryPlayRate derives at length; only the
	// choice of boundary differs.
	float RecoveryRate = -1.0f;
	if (GestureTime > KINDA_SMALL_NUMBER && ParryWhiffRecoverySeconds > 0.0f)
	{
		const float TriggerTime = ParryMontage->BlendOutTriggerTime;
		if (TriggerTime >= 0.0f)
		{
			// Explicit trigger: a fixed position, so it is rate-immune and the rate follows
			// directly. AM_Parry sets one precisely so the clip's recovery tail is not blended away.
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

	// **Played straight through the AnimInstance rather than through PlayMontageAndWait, and that
	// is the designer's ruling made mechanical** (2026-08-19): a successful parry frees the parrier
	// at the instant of the catch, which can be 0 ms in, and the clip is expected to play on and be
	// overridden by whatever they do next -- "which they almost always will". The ability task ends
	// its montage when the ability ends, which would snap the gesture away at the exact moment it
	// succeeded -- the one outcome that should read as complete. Nothing here needs the task's
	// callbacks either: the parry's length is authored, never taken from the clip.
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

	// Notifies are readable here even though the MCP toolset cannot see them; the limit recorded in
	// Docs/Working-In-Unreal.md is the toolset's, not the engine's.
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
	// **Closing the window here is what makes every exit path safe**, and it is the same argument
	// the movement lock's release makes one level up: an ability that is cancelled -- hit out of,
	// guard-broken, killed -- must not leave a hit-negation window standing. The character's close
	// is idempotent, so the ordinary expiry having already run is not a special case.
	//
	// The whiff recovery is charged by the close itself rather than from here, so that a window
	// which ends without catching anything pays the same price however it ended. See
	// ATDCombatCharacter::CloseParryWindow.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr))
	{
		// ***This used to close the window and now deliberately does not*** -- "parry is sacred"
		// (the designer, 2026-08-19). An ability ending with its window still open means something
		// cancelled a committed parry, which the design says can never happen: the only ways out
		// are expiry, a catch, and death, and death closes the window itself before cancelling.
		//
		// **The window is left running rather than torn down**, because the protection lives on the
		// character and survives its ability perfectly well. Tick expires it on schedule and bills
		// the whiff as usual, so the parry still resolves honestly -- what is lost is the jail,
		// which is the lesser harm and the one that fails visibly.
		//
		// Ungated, because this is the sacredness violation detector and there is nothing else.
		// If it ever fires, something new is cancelling abilities without routing through the parry
		// check -- Knockdown and ability effects are the candidates on the roster.
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
