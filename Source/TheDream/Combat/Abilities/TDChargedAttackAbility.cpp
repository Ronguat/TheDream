// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDChargedAttackAbility.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "Core/TheDreamCharacter.h"
#include "Combat/TDCombatCharacter.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	/** Play rate floor. Zero would stop the montage, which banks time and then spends it in one frame. */
	constexpr float TDMinPlayRate = 0.01f;

	/** How far the notify may sit from ReleaseStartSeconds before it is worth complaining about. */
	constexpr float TDReleaseStartTolerance = 0.03f;

	/** The rate actually in force on the montage, so a rate that failed to apply is visible. */
	float ActualMontageRate(const FGameplayAbilityActorInfo* ActorInfo, UAnimMontage* Montage)
	{
		UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* Instance = (AnimInstance && Montage) ? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
		return Instance ? Instance->GetPlayRate() : -1.0f;
	}
}

void UTDChargedAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	// Deliberately not Super: the base class starts tracing immediately, whereas the
	// branch -- and therefore the trace radius -- is unknown until the attack commits.
	UTDGameplayAbility::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	SelectedBranchIndex = 0;
	bAttackCommitted = false;
	bInputHeld = true;
	bCoiling = false;
	AppliedAttackTag = FGameplayTag();

	UWorld* World = GetWorld();
	if (!World || Branches.Num() == 0)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActivationWorldTime = World->GetTimeSeconds();

	// The shared windup runs at whatever rate the *fastest* branch needs. Slower branches
	// are made slower by the coil holding them back, not by this being slow.
	//
	// Handed to the montage as it starts rather than set immediately afterwards, so there
	// is no window -- however brief -- in which the windup is running at the wrong speed.
	const float WindupRate = ComputeWindupPlayRate();

	if (!StartAttackMontage(WindupSection, WindupRate))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// applied should match wanted. If it reads 1.000 the montage task did not honour the
	// rate it was given, and the whole windup is running at the wrong speed.
	TD_TIMING_LOG(TEXT("[%.3f] ACTIVATE   pos=%.4f windupRate wanted=%.3f applied=%.3f"),
		World->GetTimeSeconds(), GetMontagePosition(), WindupRate,
		ActualMontageRate(GetCurrentActorInfo(), AttackMontage));

	// The base lunge, shared by every tier and identical in wall-clock length whichever branch
	// this turns out to be -- it ends at the first branch's boundary, which is the last instant
	// before the ladder can be told apart. Deliberately not called via the parent's
	// ActivateAbility, which this class does not run because it would start the trace too early.
	StartLunge(LungeDistanceCm, GetBaseLungeDurationSeconds(), LungeStrengthCurve, LungeStandoffCm);

	// Homing runs for the base lunge's span, using branch 0's wedge -- every tier shares the
	// windup, and a light is what releasing right here would produce. Stopped at commit.
	if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		CombatCharacter->SetAimAssistHoming(BuildAimAssistWedge(0), TargetImmunityTags, true, bDrawDebugTrace);
	}

	ScheduleCheckpoint(Branches[0].HoldUntilSeconds);
}

float UTDChargedAttackAbility::GetBaseLungeDurationSeconds() const
{
	// **The span is structural; the duration inside it is a feel choice.** This used to return the
	// boundary outright, which made the light's input timing set how long the approach burst lasted
	// -- and those answer different questions, exactly as ReleaseSeconds did for the branch lunge.
	// See FTDAttackBranch::LungeDurationSeconds for the account; the same mistake was here twice.
	//
	// What is *not* free is running past the boundary. The base lunge must be finished before the
	// branch lunge starts, or a light would have two Override root motion sources live at once at
	// equal priority, where which one wins is an implementation detail rather than a design. So the
	// authored value is clamped to the span rather than trusted, and authoring it longer is simply
	// a request for the whole span.
	if (Branches.Num() == 0)
	{
		return Super::GetBaseLungeDurationSeconds();
	}

	return FMath::Min(Super::GetBaseLungeDurationSeconds(), Branches[0].HoldUntilSeconds);
}

void UTDChargedAttackAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	// Deliberately does not commit anything. The windup runs its preset length either
	// way; the pending checkpoint reads this and decides whether to escalate or commit.
	// Letting go early is what selects the branch, not what fires it.
	bInputHeld = false;
}

void UTDChargedAttackAbility::ScheduleCheckpoint(float DelaySeconds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Measured against activation rather than chained, so a late timer cannot push every
	// subsequent checkpoint further out.
	const float Remaining = DelaySeconds - GetElapsedSeconds();
	if (Remaining <= 0.0f)
	{
		HandleCheckpoint();
		return;
	}

	World->GetTimerManager().SetTimer(
		CheckpointTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]() { HandleCheckpoint(); }),
		Remaining,
		false);
}

void UTDChargedAttackAbility::HandleCheckpoint()
{
	if (bAttackCommitted)
	{
		return;
	}

	const int32 NextIndex = SelectedBranchIndex + 1;

	// Still holding escalates to the next branch. The deepest branch has nothing to
	// escalate to, so holding forever commits it anyway -- an attack that could be held
	// indefinitely would be free of risk.
	if (bInputHeld && Branches.IsValidIndex(NextIndex))
	{
		SelectedBranchIndex = NextIndex;

		// Leaving the first branch behind is exactly the moment the attack stops being a
		// light, which is the moment it earns a tell.
		if (!bCoiling)
		{
			EnterCoil();
		}

		// **Homing widens with the ladder, and that is why it does not leak the tier.** The
		// widening happens in the same block that enters the coil -- the designed tell -- so a
		// defender who could read a longer-reaching snap has already been told this is no longer a
		// light. Before this boundary every tier homes on branch 0's wedge, which is the only span
		// that has to be indistinguishable.
		//
		// It rests on wedges being non-decreasing in reach across the ladder, which is a designer's
		// commitment rather than something enforced here (2026-08-14, the user's call). A shrinking
		// wedge does not break anything: the body has already turned, so the worst case is that it
		// stops tracking or re-picks, reading as a slightly misleading rotation before the lunge.
		// **A later branch left at MaxReachCm 0 is the sharper case** -- that is *disabled* rather
		// than narrow, so homing switches off mid-hold and the body stops tracking partway through.
		if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
		{
			CombatCharacter->SetAimAssistHoming(BuildAimAssistWedge(NextIndex), TargetImmunityTags, true, bDrawDebugTrace);
		}

		TD_TIMING_LOG(TEXT("[%.3f] ESCALATE   -> branch %d  pos=%.4f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, NextIndex, GetMontagePosition());

		ScheduleCheckpoint(Branches[NextIndex].HoldUntilSeconds);
		return;
	}

	CommitAttack();
}

void UTDChargedAttackAbility::EnterCoil()
{
	bCoiling = true;

	// Facing slows here, and only here. The coil is the tell, and it is also the last window in
	// which the attack can be aimed -- so this is a cap on how far a held attack may be
	// redirected once a defender has had time to react to it, not a visual flourish. It cannot
	// touch the aim guarantee: that is discharged by the first 150 ms at the full rate, which
	// every tier has already run before reaching this line. The character owns the rate; the
	// ability only reports the phase. Cleared in EndAbility, where every exit converges.
	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->SetAbilityCoiling(true);
	}

	// Measured, not assumed. The checkpoint timer fires a frame or two late, so the
	// montage is always a little past where the maths would have put it; deriving the
	// rate from the assumed position compounds that error across the whole coil until it
	// overruns the release window.
	const float CurrentPosition = GetMontagePosition();
	const float CoilDistance = CoilEndSeconds - CurrentPosition;
	const float CoilDuration = Branches.Last().HoldUntilSeconds - GetElapsedSeconds();

	if (CurrentPosition < 0.0f || CoilDistance <= 0.0f || CoilDuration <= 0.0f)
	{
		// Nowhere to creep to, or no time to do it in. Carrying on at the windup rate is
		// a poor swing; stopping would be far worse, so there is no zero-rate branch here.
		// Ungated: a coil that never runs means a held attack races into its own release
		// window, which reads as an attack that simply does nothing.
		UE_LOG(LogTDCombatTiming, Warning, TEXT("Coil skipped: pos=%.4f distance=%.4f duration=%.4f"),
			CurrentPosition, CoilDistance, CoilDuration);
		return;
	}

	const float CoilRate = FMath::Max(CoilDistance / CoilDuration, TDMinPlayRate);
	SetMontagePlayRate(CoilRate);

	TD_TIMING_LOG(TEXT("[%.3f] COIL START pos=%.4f rate=%.3f (derived)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, CurrentPosition, CoilRate);
}

void UTDChargedAttackAbility::CommitAttack()
{
	if (bAttackCommitted || !Branches.IsValidIndex(SelectedBranchIndex))
	{
		return;
	}
	bAttackCommitted = true;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckpointTimerHandle);
	}

	const FTDAttackBranch& Branch = Branches[SelectedBranchIndex];

	// First of the two slide samples. Taken before facing is frozen below, so it records where the
	// target was when the attacker still had authority over its own aim -- the release sample then
	// says how much of that was spent sliding rather than swinging.
	LogTargetGeometry(TEXT("commit "));

	// Past this point the attack can no longer be cancelled into a defensive action. Applied
	// before anything else here so nothing can observe a committed attack that is not marked.
	if (CommittedTag.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->AddLooseGameplayTag(CommittedTag);
		}
	}

	// Surfaces the chosen attack on the debug HUD and lets other systems react to it.
	// Reading this tag back is the intended way to confirm which branch was thrown.
	if (Branch.AttackTag.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			AppliedAttackTag = Branch.AttackTag;
			ASC->AddLooseGameplayTag(AppliedAttackTag);
		}
	}

	// Hitboxes are per branch, so the task can only start once the branch is known. Starting
	// it here is also what guarantees a listener exists before the window opens -- which
	// is why CoilEndSeconds must stay below ReleaseStartSeconds.
	StartMeleeTrace(GetAttackHitboxes());

	// Target Lock's rotational half, and the order matters: it must run *before* the freeze below,
	// because it is the last moment anything may change where this attack points. One correction,
	// then facing is locked and nothing tracks -- which is what keeps it from being homing.
	// Homing stops here. Past this instant tracking would be the homing this design rejects: a
	// defender's movement has to be able to make a committed attack whiff.
	if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		CombatCharacter->SetAimAssistHoming(FTDAttackHitbox::MakeDisabled(), FGameplayTagContainer(), false, false);
	}

	ApplyAimAssist(BuildAimAssistWedge(SelectedBranchIndex));

	// Facing freezes here, instantly. The hitbox is defined in the attacker's frame, so a swing
	// free to track the camera mid-release would carry its own arc around with it and the
	// authored coverage would mean nothing.
	//
	// Commit is the right moment on design grounds as well as geometric ones: it is already the
	// boundary past which nothing can be cancelled, so this makes commitment spatial as well as
	// temporal, and it deliberately leaves the whole windup steerable.
	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->SetAbilityFacingLocked(true);
	}

	// Branch-specific travel begins here and not one frame sooner. The windup is shared and
	// identical across tiers by design, so displacement during it must be too -- a charged that
	// pulled further forward than a light would be a tell from the press, which is the property
	// the whole ladder is built to deny. See FTDAttackBranch::LungeDistanceCm.
	//
	// The duration is authored per branch as of 2026-08-13, where it used to run to the end of the
	// release window. That derivation was elegant and wrong: it let ReleaseSeconds -- a hitbox
	// liveness number -- set how a movement burst feels. See FTDAttackBranch::LungeDurationSeconds.
	StartLunge(Branch.LungeDistanceCm, Branch.LungeDurationSeconds, Branch.LungeStrengthCurve, LungeStandoffCm);

	// The window's own length is only knowable once the notify fires, so the ability waits
	// for it rather than duplicating the timeline.
	if (UAbilityTask_WaitGameplayEvent* WaitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TDTags::Event_Melee_WindowBegin, nullptr, true, true))
	{
		WaitTask->EventReceived.AddDynamic(this, &UTDChargedAttackAbility::HandleReleaseWindowBegan);
		WaitTask->ReadyForActivation();
	}

	// And the closing edge, because the release rate must come back off. Subscribed here
	// rather than inside HandleReleaseWindowBegan so both edges are armed at the same moment
	// -- a window narrow enough to open and close inside one frame would otherwise miss its
	// own end and leave the rate applied, which is the bug this pair exists to fix.
	if (UAbilityTask_WaitGameplayEvent* EndTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TDTags::Event_Melee_WindowEnd, nullptr, true, true))
	{
		EndTask->EventReceived.AddDynamic(this, &UTDChargedAttackAbility::HandleReleaseWindowEnded);
		EndTask->ReadyForActivation();
	}

	// Carry the montage from wherever it actually is into the strike, arriving exactly on
	// this branch's ReleaseAtSeconds. For the fastest branch this works out to the windup
	// rate it was already running, so the light never changes pace at all.
	const float CurrentPosition = GetMontagePosition();
	const float Distance = ReleaseStartSeconds - CurrentPosition;
	const float Remaining = Branch.ReleaseAtSeconds - GetElapsedSeconds();
	const float CommitRate = (Distance > 0.0f && Remaining > 0.0f)
		? FMath::Max(Distance / Remaining, TDMinPlayRate)
		: 1.0f;

	SetMontagePlayRate(CommitRate);

	TD_TIMING_LOG(TEXT("[%.3f] COMMIT     branch %d (%s) pos=%.4f rate=%.3f targetRelease=%.3f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, SelectedBranchIndex,
		*Branch.AttackTag.ToString(), CurrentPosition, CommitRate, Branch.ReleaseAtSeconds);

	// Only used once a branch earns a distinct release animation.
	if (Branch.MontageSection != NAME_None)
	{
		MontageJumpToSection(Branch.MontageSection);
	}
}

bool UTDChargedAttackAbility::IsWindowForThisAttack(const FGameplayEventData& Payload) const
{
	// The notify broadcasts to the whole ASC and carries no ownership, so without this any
	// montage carrying a Release Window would drive this attack's play rate. Harmless while
	// exactly one montage carries the notify; silently wrong the moment a second does, which
	// is what Attack Swap's content pass creates. UAbilityTask_MeleeTrace guards the same way.
	//
	// Null AttackMontage means accept any, which is the behaviour before this guard existed.
	if (!AttackMontage)
	{
		return true;
	}

	if (Payload.OptionalObject == AttackMontage)
	{
		return true;
	}

	// Ungated: a mismatch here means the attack never applies its release rate and never
	// takes it off again -- both silent, and the second is the bug this guard shipped with.
	UE_LOG(LogTDCombatTiming, Warning,
		TEXT("Release Window from '%s' ignored; this attack is playing '%s'."),
		Payload.OptionalObject ? *Payload.OptionalObject->GetName() : TEXT("<none>"),
		*AttackMontage->GetName());

	return false;
}

void UTDChargedAttackAbility::HandleReleaseWindowEnded(FGameplayEventData Payload)
{
	if (!IsWindowForThisAttack(Payload))
	{
		return;
	}

	// Off the release rate and onto the recovery rate. Without this the rate derived for the
	// release stays applied for the rest of the montage, which is what sent recovery through
	// at 3.28x and -- above about 2.8x -- left less montage-time remaining than the blend-out
	// needed, so the montage terminated itself the instant the rate was applied.
	//
	// The rate is derived from the branch's authored RecoverySeconds rather than authored
	// directly, so what a designer sets is the punish window itself.
	const float RecoveryFrom = GetMontagePosition();
	const float TargetSeconds = Branches.IsValidIndex(SelectedBranchIndex)
		? Branches[SelectedBranchIndex].RecoverySeconds
		: 0.0f;

	const float RecoveryRate = ComputeRecoveryPlayRate(RecoveryFrom, TargetSeconds);
	if (RecoveryRate > 0.0f)
	{
		SetMontagePlayRate(RecoveryRate);
	}
	else
	{
		// Ungated, and deliberately alongside the coil and notify-drift warnings: all three are
		// authored data having quietly stopped fitting the clip. This one leaves recovery running
		// at whatever the release left applied, so the punish window silently stops being the
		// authored one rather than visibly breaking.
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Recovery has no montage left: window closed at %.4f, blend-out begins at %.4f. ")
			TEXT("RecoverySeconds %.3f cannot be honoured -- the clip's tail is too short."),
			RecoveryFrom, GetBlendOutStartSeconds(1.0f), TargetSeconds);
	}

	// Facing is deliberately *not* released here, and this used to be where it happened, on the
	// argument that recovery is not part of the commitment. Play disagreed on 2026-08-12: control
	// snapping back the instant the hitbox expired read as unnatural, and recovery is exactly the
	// window an attack is supposed to be punishable in -- being committed to a direction through
	// it is the same commitment, expressed spatially. The lock now runs to EndAbility.
	//
	// It costs nothing to the aim guarantee, which lives entirely between the press and the
	// commit checkpoint and is untouched by where the lock ends. What it does change is that a
	// chained attack starts its windup with whatever gap accumulated during the whole previous
	// attack rather than a mostly-closed one -- which is fine, because the windup is sized to
	// close the full 180 degree ceiling, and it puts a combo's redirection exactly where a player
	// can see it happen.

	TD_TIMING_LOG(TEXT("[%.3f] RELEASE OFF  pos=%.4f rate=%.3f (want %.3fs to blendOut %.4f)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, RecoveryFrom, RecoveryRate,
		TargetSeconds, GetBlendOutStartSeconds(RecoveryRate));
}

void UTDChargedAttackAbility::HandleReleaseWindowBegan(FGameplayEventData Payload)
{
	if (!IsWindowForThisAttack(Payload))
	{
		return;
	}

	if (!Branches.IsValidIndex(SelectedBranchIndex))
	{
		return;
	}

	const FTDAttackBranch& Branch = Branches[SelectedBranchIndex];
	const float ActualStart = GetMontagePosition();

	// Second slide sample. The bearing's change since commit is how far the attacker travelled
	// around a target its wedge could not follow, and it is the number Target Lock is judged on.
	LogTargetGeometry(TEXT("release"));

	// Redundant with the lock applied at commit, and kept deliberately, ahead of every early
	// return below. The invariant worth defending is "facing is frozen whenever a hitbox is
	// live", and this is the only place that can assert it at the moment it becomes true rather
	// than inferring it from an earlier call having happened.
	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->SetAbilityFacingLocked(true);
	}

	// ReleaseStartSeconds is hand-copied from the notify's placement, so it can silently
	// drift if the montage is re-authored. This is the only moment the truth is available.
	if (ActualStart >= 0.0f && FMath::Abs(ActualStart - ReleaseStartSeconds) > TDReleaseStartTolerance)
	{
		// Ungated: this is hand-copied data having silently drifted, and every rate the
		// ability derives is computed from it.
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Release Window opened at %.4f but ReleaseStartSeconds is %.4f. Update it to match the notify."),
			ActualStart, ReleaseStartSeconds);
	}

	// The notify reports its own length, so the window can be stretched to the authored
	// duration without anyone maintaining a copy of the timeline.
	const float WindowLength = Payload.EventMagnitude;
	if (WindowLength <= 0.0f || Branch.ReleaseSeconds <= 0.0f)
	{
		return;
	}

	const float ReleaseRate = FMath::Max(WindowLength / Branch.ReleaseSeconds, TDMinPlayRate);
	SetMontagePlayRate(ReleaseRate);

	TD_TIMING_LOG(TEXT("[%.3f] RELEASE    pos=%.4f windowLen=%.4f rate=%.3f (want %.3fs)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, ActualStart, WindowLength, ReleaseRate, Branch.ReleaseSeconds);
}

float UTDChargedAttackAbility::ComputeWindupPlayRate() const
{
	if (Branches.Num() == 0 || Branches[0].ReleaseAtSeconds <= 0.0f || ReleaseStartSeconds <= 0.0f)
	{
		return 1.0f;
	}

	return FMath::Max(ReleaseStartSeconds / Branches[0].ReleaseAtSeconds, TDMinPlayRate);
}

float UTDChargedAttackAbility::GetBlendOutStartSeconds(float PlayRate) const
{
	if (!AttackMontage)
	{
		return -1.0f;
	}

	const float Length = AttackMontage->GetPlayLength();
	const float TriggerTime = AttackMontage->BlendOutTriggerTime;

	// A trigger time is an authored montage position, measured back from the end, and it does
	// not care how fast the montage is playing.
	if (TriggerTime >= 0.0f)
	{
		return Length - TriggerTime;
	}

	// **Without one, the boundary moves with the play rate**, which is the whole subtlety here.
	// A negative trigger means "blend so it finishes as the montage does", and the engine tests
	// that in *time* rather than position: it begins blending once the remaining montage would
	// take less than the blend's duration to play. Halve the rate and the blend starts half as
	// far from the end.
	//
	// Found in play on 2026-08-12 and worth stating rather than re-deriving. Treating this as
	// the fixed position `Length - BlendTime` is right only at rate 1.0, so the error hides at
	// the rates a first test happens to produce: at 0.94 recovery ran 6% long and looked like
	// jitter; at 0.50, authored for a charged, it ran 49% long -- 0.744s against an authored
	// 0.500s.
	return Length - AttackMontage->BlendOut.GetBlendTime() * FMath::Max(PlayRate, TDMinPlayRate);
}

float UTDChargedAttackAbility::ComputeRecoveryPlayRate(float FromPosition, float TargetSeconds) const
{
	if (!AttackMontage || FromPosition < 0.0f || TargetSeconds <= 0.0f)
	{
		return -1.0f;
	}

	const float Length = AttackMontage->GetPlayLength();
	const float Remaining = Length - FromPosition;
	if (Remaining <= KINDA_SMALL_NUMBER)
	{
		return -1.0f;
	}

	const float TriggerTime = AttackMontage->BlendOutTriggerTime;
	if (TriggerTime >= 0.0f)
	{
		// Fixed boundary: cover the montage up to it in the authored time.
		const float ToBoundary = (Length - TriggerTime) - FromPosition;
		return (ToBoundary <= KINDA_SMALL_NUMBER)
			? -1.0f
			: FMath::Max(ToBoundary / TargetSeconds, TDMinPlayRate);
	}

	// Rate-dependent boundary. Solving for the rate R that makes recovery last exactly
	// TargetSeconds, given that the blend begins BlendTime*R before the montage's end:
	//
	//     (Length - BlendTime*R - FromPosition) / R = TargetSeconds
	//  => Length - FromPosition = R * (TargetSeconds + BlendTime)
	//  => R = (Length - FromPosition) / (TargetSeconds + BlendTime)
	//
	// The blend cancels out of the position but not out of the time, which is exactly why the
	// naive form is wrong and why it is wrong by more the slower the recovery is authored.
	const float BlendTime = AttackMontage->BlendOut.GetBlendTime();
	return FMath::Max(Remaining / (TargetSeconds + BlendTime), TDMinPlayRate);
}

float UTDChargedAttackAbility::GetElapsedSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() - ActivationWorldTime : 0.0f;
}

float UTDChargedAttackAbility::GetMontagePosition() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	return (AnimInstance && AttackMontage) ? AnimInstance->Montage_GetPosition(AttackMontage) : -1.0f;
}

void UTDChargedAttackAbility::SetMontagePlayRate(float PlayRate) const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	if (AnimInstance && AttackMontage && AnimInstance->Montage_IsPlaying(AttackMontage))
	{
		AnimInstance->Montage_SetPlayRate(AttackMontage, FMath::Max(PlayRate, TDMinPlayRate));
	}
}

float UTDChargedAttackAbility::GetAttackDamage() const
{
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].Damage : Damage;
}

float UTDChargedAttackAbility::GetAttackStaminaDamage() const
{
	// Falls back to the ability's own value on an invalid index, exactly as damage does. Note the
	// asymmetry with hitboxes, which fall back on an *empty array* as well: a stamina damage of
	// zero is a legitimate authored value meaning "this can never break a guard", so it must not
	// be treated as unset.
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].StaminaDamage : StaminaDamage;
}

float UTDChargedAttackAbility::GetAttackBlockstunSeconds() const
{
	// Falls back on an invalid index like the two above, and zero is likewise a legitimate authored
	// value -- "blocking this costs nothing but stamina" -- rather than a signal that it is unset.
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].BlockstunSeconds : BlockstunSeconds;
}

const TArray<FTDAttackHitbox>& UTDChargedAttackAbility::GetAttackHitboxes() const
{
	// An authored-but-empty branch falls back to the ability's own set rather than to nothing.
	// The alternative is an attack that silently deals no damage, which is the exact failure this
	// project keeps a trap list for -- and it was reachable the moment the old per-branch
	// TraceRadius was removed, since every existing branch deserialises with an empty array.
	if (Branches.IsValidIndex(SelectedBranchIndex) && Branches[SelectedBranchIndex].Hitboxes.Num() > 0)
	{
		return Branches[SelectedBranchIndex].Hitboxes;
	}

	return Hitboxes;
}

FTDAttackHitbox UTDChargedAttackAbility::BuildAimAssistWedge(int32 BranchIndex) const
{
	if (!Branches.IsValidIndex(BranchIndex))
	{
		return FTDAttackHitbox::MakeDisabled();
	}

	const FTDAttackBranch& Branch = Branches[BranchIndex];

	// The branch's own damage reach, resolved the same way GetAttackHitboxes resolves it -- an
	// authored-but-empty branch falls back to the ability's set. Taken as the *furthest* of the
	// branch's volumes, because reach here means "how far this attack can strike", and a bash plus
	// a sweep is one attack with one maximum.
	const TArray<FTDAttackHitbox>& ResolvedHitboxes =
		Branch.Hitboxes.Num() > 0 ? Branch.Hitboxes : Hitboxes;

	float FurthestReachCm = 0.0f;
	for (const FTDAttackHitbox& Hitbox : ResolvedHitboxes)
	{
		FurthestReachCm = FMath::Max(FurthestReachCm, Hitbox.MaxReachCm);
	}

	// Travel plus reach is exactly the furthest a body can be and still be struck: the base lunge
	// runs before the tiers diverge, the branch lunge from commit, and the hitbox extends further
	// still. The margin on top is the only judgement in the sum -- see AimAssistMarginCm for why
	// assist must reach *past* what it can hit rather than exactly as far.
	const float ReachCm = LungeDistanceCm + Branch.LungeDistanceCm + FurthestReachCm + AimAssistMarginCm;

	return Branch.AimAssistWedge.ToHitbox(ReachCm);
}

void UTDChargedAttackAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckpointTimerHandle);
	}

	// Cleared again here, unconditionally and idempotently, because an attack cancelled during its
	// windup never reaches commit -- and this is the one place every exit converges. Same contract
	// as the facing lock: a stranded debug wedge would paint itself on the world forever.
	if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		CombatCharacter->SetAimAssistHoming(FTDAttackHitbox::MakeDisabled(), FGameplayTagContainer(), false, false);
	}

	// When the ability ends relative to the release window's close is not otherwise
	// observable, and it decides whether HandleReleaseWindowEnded can run at all: ending
	// first destroys the task waiting for that edge.
	TD_TIMING_LOG(TEXT("[%.3f] ABILITY END  pos=%.4f elapsed=%.3f%s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		GetMontagePosition(),
		GetElapsedSeconds(),
		bWasCancelled ? TEXT(" (cancelled)") : TEXT(""));

	// A cancelled attack would otherwise leave the montage crawling at the coil rate.
	SetMontagePlayRate(1.0f);

	// Must come off even on cancellation, or the character reads as mid-attack forever --
	// and a leaked CommittedTag would silently forbid every future defensive action.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (AppliedAttackTag.IsValid())
		{
			ASC->RemoveLooseGameplayTag(AppliedAttackTag);
		}
		// Guarded on bAttackCommitted, not just on the tag being set: an attack cancelled
		// during its windup never applied it, and removing an absent loose tag decrements
		// a count that is already zero.
		if (bAttackCommitted && CommittedTag.IsValid())
		{
			ASC->RemoveLooseGameplayTag(CommittedTag);
		}
	}
	AppliedAttackTag = FGameplayTag();

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
