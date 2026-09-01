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
	bReleaseWindowClosed = false;
	bInRecovery = false;
	// InstancedPerActor reuses one instance for every swing, and IsChainOutOpen refuses while this
	// is set -- so it clears per activation, or a parry ends chaining for the session rather than
	// for its own string. The melee base clears it in the ActivateAbility this class bypasses.
	bParried = false;
	RecoveryStartedAt = 0.0f;
	AppliedAttackTag = FGameplayTag();
	// Same InstancedPerActor hazard: a tier montage left set would make the next swing read its
	// positions off the previous swing's clip, and every one of them would be wrong.
	ActiveTierMontage = nullptr;
	ActiveTierReleaseStart = 0.0f;

	UWorld* World = GetWorld();
	if (!World || Branches.Num() == 0)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ActivationWorldTime = World->GetTimeSeconds();

	// Which swing of the string this is, asked of the character because the string outlives any one
	// activation. Resolved before anything reads a montage or derives a rate. With no StringSwings
	// authored this is always 0 and the mechanism is inert by construction.
	CurrentSwingIndex = 0;
	if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		CurrentSwingIndex = CombatCharacter->ResolveStringSwingIndexForActivation(GetSwingCount());
	}

	// The shared windup runs at whatever rate the *fastest* branch needs; slower branches are made
	// slower by the coil holding them back. Handed to the montage as it starts rather than set
	// afterwards, so there is no window in which the windup runs at the wrong speed.
	const float WindupRate = ComputeWindupPlayRate();

	if (!StartAttackMontage(WindupSection, WindupRate))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// applied should match wanted. 1.000 means the montage task did not honour the rate it was
	// given, and the whole windup is running at the wrong speed. **Names its avatar**, abilities
	// being InstancedPerActor: without it the line is unattributable in any fixture where two
	// combatants attack, and an assertion about one character's swings counts the other's.
	TD_TIMING_LOG(TEXT("[%.3f] ACTIVATE   %s swing=%d pos=%.4f windupRate wanted=%.3f applied=%.3f"),
		World->GetTimeSeconds(), *GetNameSafe(GetAvatarActorFromActorInfo()),
		CurrentSwingIndex, GetMontagePosition(), WindupRate,
		ActualMontageRate(GetCurrentActorInfo(), GetActiveAttackMontage()));

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
	// **The span is structural; the duration inside it is a feel choice**, which is why this clamps
	// the authored value rather than returning the boundary outright. What is *not* free is running
	// past the boundary: the base lunge must finish before the branch lunge starts, or a light has
	// two Override root motion sources live at equal priority, where which wins is an
	// implementation detail rather than a design. Authoring it longer requests the whole span.
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

		// Every escalation swaps, not just the first: light -> heavy and heavy -> charged are two
		// blends, and the second departs from whatever the first left on the slot. Ahead of the
		// coil, so EnterCoil sees the montage this branch will actually run on.
		if (const FTDTierAnimation* Tier = FindTierAnimation(CurrentSwingIndex, NextIndex))
		{
			StartTierMontage(*Tier);
		}

		// Leaving the first branch behind is exactly the moment the attack stops being a
		// light, which is the moment it earns a tell.
		if (!bCoiling)
		{
			EnterCoil();
		}

		// **Homing widens with the ladder, and that is why it does not leak the tier.** The widening
		// happens in the same block that enters the coil -- the designed tell -- so a defender who
		// could read a longer-reaching snap has already been told this is no longer a light. Before
		// this boundary every tier homes on branch 0's wedge, the only span that has to be
		// indistinguishable.
		//
		// It rests on wedges being non-decreasing in reach. A shrinking wedge breaks nothing, the
		// body having already turned. **A later branch left at MaxReachCm 0 is the sharper case**:
		// that is *disabled* rather than narrow, so homing switches off mid-hold.
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

	// Facing slows here, and only here. The coil is the tell and also the last window in which the
	// attack can be aimed, so this caps how far a held attack may be redirected once a defender has
	// had time to react. It cannot touch the aim guarantee, discharged by the first 150 ms at the
	// full rate that every tier has already run. The character owns the rate, the ability reports
	// the phase. Cleared in EndAbility, where every exit converges.
	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->SetAbilityCoiling(true);
	}

	// **A tier montage was already rated at the swap to reach its notify no sooner than the last
	// checkpoint**, so deriving a second rate here would fight the first. The facing clamp above
	// still applies: it is an aim guarantee rather than a tell, and nothing else caps redirection.
	if (ActiveTierMontage)
	{
		TD_TIMING_LOG(TEXT("[%.3f] COIL START pos=%.4f (rated at the tier swap)"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, GetMontagePosition());
		return;
	}

	// Measured, not assumed. The checkpoint timer fires a frame or two late, so the montage is
	// always a little past where the maths would have put it; deriving the rate from the assumed
	// position compounds that error across the coil until it overruns the release window.
	const float CurrentPosition = GetMontagePosition();
	const float CoilDistance = GetSwingCoilEndSeconds(CurrentSwingIndex) - CurrentPosition;
	const float CoilDuration = Branches.Last().HoldUntilSeconds - GetElapsedSeconds();

	if (CurrentPosition < 0.0f || CoilDistance <= 0.0f || CoilDuration <= 0.0f)
	{
		// Nowhere to creep to, or no time to do it in. Carrying on at the windup rate is a poor
		// swing; stopping would be far worse, so there is no zero-rate branch. Ungated: a coil that
		// never runs means a held attack races into its own release window and reads as nothing.
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

	// A heavy or charged commit ends the string then and there -- heavy never chains into light. At
	// the commit rather than the ability's end, so a dodge-cancel of the *following* windup cannot
	// resurrect a string the escalation already closed. Data-driven: a branch that authors
	// bChainsIntoString keeps the string alive instead.
	if (!Branch.bChainsIntoString)
	{
		if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
		{
			CombatCharacter->ResetString(TEXT("non-chaining commit"));
		}
	}

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
	// the last moment anything may change where this attack points. One correction, then facing is
	// locked and nothing tracks -- which is what keeps it from being homing. Past this instant a
	// defender's movement has to be able to make a committed attack whiff.
	if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		CombatCharacter->SetAimAssistHoming(FTDAttackHitbox::MakeDisabled(), FGameplayTagContainer(), false, false);
	}

	ApplyAimAssist(BuildAimAssistWedge(SelectedBranchIndex));

	// Facing freezes here, instantly. The hitbox is defined in the attacker's frame, so a swing
	// free to track the camera mid-release would carry its own arc around and the authored coverage
	// would mean nothing. Commit is right on design grounds too: already the boundary past which
	// nothing can be cancelled, so this makes commitment spatial as well as temporal, and leaves
	// the whole windup steerable.
	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->SetAbilityFacingLocked(true);
	}

	// Branch-specific travel begins here and not one frame sooner. The windup is shared and
	// identical across tiers by design, so displacement during it must be too -- a charged pulling
	// further forward than a light would be a tell from the press, the property the ladder exists
	// to deny. See FTDAttackBranch::LungeDistanceCm. The duration is authored per branch rather
	// than derived from the release window, which would let a hitbox-liveness number set how a
	// movement burst feels; see FTDAttackBranch::LungeDurationSeconds.
	StartLunge(
		GetSwingLungeDistanceCm(CurrentSwingIndex, SelectedBranchIndex),
		GetSwingLungeDurationSeconds(CurrentSwingIndex, SelectedBranchIndex),
		Branch.LungeStrengthCurve,
		LungeStandoffCm);

	// The window's own length is only knowable once the notify fires, so the ability waits
	// for it rather than duplicating the timeline.
	if (UAbilityTask_WaitGameplayEvent* WaitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TDTags::Event_Melee_WindowBegin, nullptr, true, true))
	{
		WaitTask->EventReceived.AddDynamic(this, &UTDChargedAttackAbility::HandleReleaseWindowBegan);
		WaitTask->ReadyForActivation();
	}

	// And the closing edge, because the release rate must come back off. Subscribed here rather
	// than inside HandleReleaseWindowBegan so both edges arm at the same moment -- a window narrow
	// enough to open and close inside one frame would otherwise miss its own end.
	if (UAbilityTask_WaitGameplayEvent* EndTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TDTags::Event_Melee_WindowEnd, nullptr, true, true))
	{
		EndTask->EventReceived.AddDynamic(this, &UTDChargedAttackAbility::HandleReleaseWindowEnded);
		EndTask->ReadyForActivation();
	}

	// Carry the montage from wherever it actually is into the strike, arriving exactly on
	// this branch's ReleaseAtSeconds. For the fastest branch this works out to the windup
	// rate it was already running, so the light never changes pace at all.
	const float CurrentPosition = GetMontagePosition();
	const float Distance = GetSwingReleaseStartSeconds(CurrentSwingIndex) - CurrentPosition;
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

float UTDChargedAttackAbility::GetTraceWindowSeconds() const
{
	return GetSwingReleaseSeconds(CurrentSwingIndex, SelectedBranchIndex);
}

void UTDChargedAttackAbility::HandleTraceWindowClosed()
{
	CloseReleaseWindow();
}

void UTDChargedAttackAbility::HandleReleaseWindowEnded(FGameplayEventData Payload)
{
	if (!IsWindowForThisAttack(Payload))
	{
		return;
	}

	CloseReleaseWindow();
}

void UTDChargedAttackAbility::CloseReleaseWindow()
{
	// The trace task's deadline and the closing notify both arrive, in that order, and only
	// the first may run: recovery derives its rate from where the montage is *now*, so a
	// second pass would re-derive it from a later position and stretch the punish window.
	if (bReleaseWindowClosed)
	{
		return;
	}
	bReleaseWindowClosed = true;

	// Off the release rate and onto the recovery rate. Without this the release rate stays applied
	// for the rest of the montage, which sent recovery through at 3.28x and -- above about 2.8x --
	// left less montage-time remaining than the blend-out needed, terminating the montage the
	// instant the rate was applied. The rate is derived from the authored RecoverySeconds, so what
	// a designer sets is the punish window itself; a string position beyond the first authors its
	// own on the swing, heavy and charged keep the branch's wherever they convert.
	const float RecoveryFrom = GetMontagePosition();
	float TargetSeconds = Branches.IsValidIndex(SelectedBranchIndex)
		? Branches[SelectedBranchIndex].RecoverySeconds
		: 0.0f;
	if (SelectedBranchIndex == 0 && StringSwings.IsValidIndex(CurrentSwingIndex - 1))
	{
		TargetSeconds = StringSwings[CurrentSwingIndex - 1].RecoverySeconds;
	}

	// Recovery is the span the chain-out may open inside; note it before deriving anything, so a
	// chain press already waiting in the buffer can leave on the very next buffer tick.
	bInRecovery = true;
	RecoveryStartedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

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

	// **Facing is deliberately *not* released here; the lock runs to EndAbility.** Recovery is the
	// window an attack is supposed to be punishable in, and being committed to a direction through
	// it is that commitment expressed spatially. It costs the aim guarantee nothing, which lives
	// entirely between the press and the commit checkpoint. What it does change is that a chained
	// attack starts its windup with whatever gap accumulated during the previous attack -- fine,
	// the windup being sized to close the full 180 degree ceiling.

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

	// Redundant with the lock applied at commit, and kept deliberately, ahead of every early return
	// below. The invariant worth defending is "facing is frozen whenever a hitbox is live", and
	// this is the only place that can assert it as it becomes true rather than inferring it.
	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->SetAbilityFacingLocked(true);
	}

	// ReleaseStartSeconds is hand-copied from the notify's placement, so it can silently
	// drift if the montage is re-authored. This is the only moment the truth is available --
	// checked against the *swing's* value, since each swing's montage authors its own.
	const float ExpectedStart = GetSwingReleaseStartSeconds(CurrentSwingIndex);
	if (ActualStart >= 0.0f && FMath::Abs(ActualStart - ExpectedStart) > TDReleaseStartTolerance)
	{
		// Ungated: this is hand-copied data having silently drifted, and every rate the
		// ability derives is computed from it.
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Release Window opened at %.4f but swing %d's ReleaseStartSeconds is %.4f. Update it to match the notify."),
			ActualStart, CurrentSwingIndex, ExpectedStart);
	}

	// The notify reports its own length, so the window can be stretched to the authored
	// duration without anyone maintaining a copy of the timeline.
	const float WindowLength = Payload.EventMagnitude;
	const float ReleaseSeconds = GetSwingReleaseSeconds(CurrentSwingIndex, SelectedBranchIndex);
	if (WindowLength <= 0.0f || ReleaseSeconds <= 0.0f)
	{
		return;
	}

	const float ReleaseRate = FMath::Max(WindowLength / ReleaseSeconds, TDMinPlayRate);
	SetMontagePlayRate(ReleaseRate);

	TD_TIMING_LOG(TEXT("[%.3f] RELEASE    pos=%.4f windowLen=%.4f rate=%.3f (want %.3fs)"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, ActualStart, WindowLength, ReleaseRate, ReleaseSeconds);
}

float UTDChargedAttackAbility::ComputeWindupPlayRate() const
{
	const float SwingReleaseStart = GetSwingReleaseStartSeconds(CurrentSwingIndex);
	if (Branches.Num() == 0 || Branches[0].ReleaseAtSeconds <= 0.0f || SwingReleaseStart <= 0.0f)
	{
		return 1.0f;
	}

	return FMath::Max(SwingReleaseStart / Branches[0].ReleaseAtSeconds, TDMinPlayRate);
}

float UTDChargedAttackAbility::GetElapsedSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() - ActivationWorldTime : 0.0f;
}

float UTDChargedAttackAbility::GetAttackDamage() const
{
	// A string position beyond the first authors branch 0's values on its swing; heavy and
	// charged keep the branch's own wherever in the string they convert, because the tier is
	// what they are and the swing is merely the clip they coiled out of.
	if (SelectedBranchIndex == 0 && StringSwings.IsValidIndex(CurrentSwingIndex - 1))
	{
		return StringSwings[CurrentSwingIndex - 1].Damage;
	}
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].Damage : Damage;
}

float UTDChargedAttackAbility::GetAttackStaminaDamage() const
{
	// Falls back to the ability's own value on an invalid index, exactly as damage does. Note the
	// asymmetry with hitboxes, which fall back on an *empty array* too: a stamina damage of zero is
	// a legitimate authored value meaning "this can never break a guard", so it is not unset.
	if (SelectedBranchIndex == 0 && StringSwings.IsValidIndex(CurrentSwingIndex - 1))
	{
		return StringSwings[CurrentSwingIndex - 1].StaminaDamage;
	}
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].StaminaDamage : StaminaDamage;
}

float UTDChargedAttackAbility::GetAttackBlockstunSeconds() const
{
	// Falls back on an invalid index like the two above, and zero is likewise a legitimate authored
	// value -- "blocking this costs nothing but stamina" -- rather than a signal that it is unset.
	if (SelectedBranchIndex == 0 && StringSwings.IsValidIndex(CurrentSwingIndex - 1))
	{
		return StringSwings[CurrentSwingIndex - 1].BlockstunSeconds;
	}
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].BlockstunSeconds : BlockstunSeconds;
}

float UTDChargedAttackAbility::GetAttackHitstunSeconds() const
{
	// Same resolution as the three above: swing value for a mid-string light, branch value for
	// the tiers, ability fallback on an invalid index. Zero everywhere means no hitstun.
	if (SelectedBranchIndex == 0 && StringSwings.IsValidIndex(CurrentSwingIndex - 1))
	{
		return StringSwings[CurrentSwingIndex - 1].HitstunSeconds;
	}
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].HitstunSeconds : HitstunSeconds;
}

ETDKnockdownType UTDChargedAttackAbility::GetAttackKnockdownType() const
{
	// Same resolution as the stun values above: swing value for a mid-string light, branch value
	// for the tiers, ability fallback on an invalid index. None everywhere means hitstun instead,
	// which is what every attack did before knockdown existed.
	if (SelectedBranchIndex == 0 && StringSwings.IsValidIndex(CurrentSwingIndex - 1))
	{
		return StringSwings[CurrentSwingIndex - 1].KnockdownType;
	}
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].KnockdownType : KnockdownType;
}


float UTDChargedAttackAbility::GetAttackParryLockoutSeconds() const
{
	// Same resolution as the stun values and the type: swing value for a mid-string light, branch
	// value for the tiers, ability fallback on an invalid index. **Authored at every level** -- no
	// level of this ladder computes the number.
	if (SelectedBranchIndex == 0 && StringSwings.IsValidIndex(CurrentSwingIndex - 1))
	{
		return StringSwings[CurrentSwingIndex - 1].ParryLockoutSeconds;
	}
	return Branches.IsValidIndex(SelectedBranchIndex) ? Branches[SelectedBranchIndex].ParryLockoutSeconds : ParryLockoutSeconds;
}

float UTDChargedAttackAbility::GetKnockbackSpacingCm(bool bBlocked) const
{
	// The carve-out is the *clean-hit* one: the ender, the heavies and the charged all carry a
	// knockdown type, so EnterKnockdown's radial carry replaces spacing on a different axis and
	// their clean hits must not also take it. A blocked contact knocks nothing down, so the
	// carve-out does not apply to it and every tier takes BlockedSpacingCm.
	if (!bBlocked && !IsNonFinalStringLight())
	{
		return 0.0f;
	}
	return Super::GetKnockbackSpacingCm(bBlocked);
}

bool UTDChargedAttackAbility::IsNonFinalStringLight() const
{
	return Branches.IsValidIndex(SelectedBranchIndex)
		&& SelectedBranchIndex == 0
		&& Branches[SelectedBranchIndex].bChainsIntoString
		&& HasSuccessorSwing(CurrentSwingIndex);
}

float UTDChargedAttackAbility::GetSwingReleaseStartSeconds(int32 SwingIndex) const
{
	// The swapped-in clip's own notify position. Without this the commit rate would carry the
	// montage toward a position belonging to a clip that stopped playing at the escalation.
	if (ActiveTierMontage)
	{
		return ActiveTierReleaseStart;
	}

	return StringSwings.IsValidIndex(SwingIndex - 1)
		? StringSwings[SwingIndex - 1].ReleaseStartSeconds
		: ReleaseStartSeconds;
}

const FTDTierAnimation* UTDChargedAttackAbility::FindTierAnimation(int32 SwingIndex, int32 BranchIndex) const
{
	if (BranchIndex <= 0)
	{
		return nullptr;
	}

	const TArray<FTDTierAnimation>& Sockets = StringSwings.IsValidIndex(SwingIndex - 1)
		? StringSwings[SwingIndex - 1].TierAnimations
		: TierAnimations;

	const int32 Slot = BranchIndex - 1;
	if (!Sockets.IsValidIndex(Slot) || !Sockets[Slot].Montage)
	{
		return nullptr;
	}

	return &Sockets[Slot];
}

void UTDChargedAttackAbility::StartTierMontage(const FTDTierAnimation& Tier)
{
	// The outgoing montage is about to be interrupted by the incoming one taking its slot. Its
	// task must be silenced first: OnInterrupted ends the ability, and this interruption is ours.
	SilenceMontageTask();

	ActiveTierMontage = Tier.Montage;
	ActiveTierReleaseStart = Tier.ReleaseStartSeconds;

	// **The clip must not reach its own Release Window before the deepest branch can commit**, or
	// the notify fires while the attack is still escalating and the commit's rate correction has
	// nothing left to correct -- the window is already open and the release lands early.
	//
	// So the rate carries the entry point to the notify no sooner than the release of **the
	// deepest branch this clip is still the one playing** -- which is not always the deepest
	// branch. A branch with its own socket replaces this clip at its escalation, so this clip
	// only has to last until then; pacing it for a release it will never serve makes the commit
	// sprint across the distance it held back, at nine times rate on a well-fitted heavy.
	//
	// Walking to the last unsocketed branch also keeps the inert case exactly as it was: with no
	// socket anywhere the walk reaches the end and the target is the deepest branch's release.
	//
	// **Clamped to 1, never above**: a clip with runway to spare plays at its authored speed and
	// this is inert, which is what a fitted clip should do. Only a short one is held.
	int32 LastBranchOnThisClip = SelectedBranchIndex;
	while (Branches.IsValidIndex(LastBranchOnThisClip + 1)
		&& !FindTierAnimation(CurrentSwingIndex, LastBranchOnThisClip + 1))
	{
		++LastBranchOnThisClip;
	}

	const float Runway = Tier.ReleaseStartSeconds - Tier.EntrySeconds;
	const float UntilRelease = Branches[LastBranchOnThisClip].ReleaseAtSeconds - GetElapsedSeconds();
	const float HoldRate = (Runway > 0.0f && UntilRelease > 0.0f)
		? FMath::Clamp(Runway / UntilRelease, TDMinPlayRate, 1.0f)
		: 1.0f;

	if (!StartAttackMontage(NAME_None, HoldRate, Tier.EntrySeconds))
	{
		// The swap failed, so the slot still holds the outgoing clip -- but its task is silenced
		// and nothing will end the ability. Ungated: this is the shape that reads as a hung swing.
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("Tier montage '%s' refused; swing %d branch %d has no animation running."),
			*GetNameSafe(Tier.Montage), CurrentSwingIndex, SelectedBranchIndex);
		ActiveTierMontage = nullptr;
		ActiveTierReleaseStart = 0.0f;
		return;
	}

	// rate=1.000 means the clip had runway to spare and nothing is holding it; anything below is
	// the shortfall, and how far below says how short the clip is for the window it must cover.
	// `paces=` is the branch whose release it was rated for, which differs from the branch that
	// swapped it in exactly when the next tier has no socket of its own.
	TD_TIMING_LOG(TEXT("[%.3f] TIER SWAP  branch %d '%s' entry=%.4f release=%.4f rate=%.3f paces=%d"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f, SelectedBranchIndex,
		*GetNameSafe(Tier.Montage), Tier.EntrySeconds, Tier.ReleaseStartSeconds, HoldRate,
		LastBranchOnThisClip);
}

float UTDChargedAttackAbility::GetSwingCoilEndSeconds(int32 SwingIndex) const
{
	return StringSwings.IsValidIndex(SwingIndex - 1)
		? StringSwings[SwingIndex - 1].CoilEndSeconds
		: CoilEndSeconds;
}

const TArray<FTDAttackHitbox>& UTDChargedAttackAbility::GetSwingHitboxes(int32 SwingIndex, int32 BranchIndex) const
{
	// Swing, then branch, then the ability's own set. Each rung falls through on *empty* rather
	// than on a flag, so authoring nothing is how a swing says "same as the tier" -- and the last
	// rung exists because a silently undamaging attack is worse than a wrong-sized one.
	if (StringSwings.IsValidIndex(SwingIndex - 1) && StringSwings[SwingIndex - 1].Hitboxes.Num() > 0)
	{
		return StringSwings[SwingIndex - 1].Hitboxes;
	}

	if (Branches.IsValidIndex(BranchIndex) && Branches[BranchIndex].Hitboxes.Num() > 0)
	{
		return Branches[BranchIndex].Hitboxes;
	}

	return Hitboxes;
}

float UTDChargedAttackAbility::GetSwingReleaseSeconds(int32 SwingIndex, int32 BranchIndex) const
{
	if (StringSwings.IsValidIndex(SwingIndex - 1) && StringSwings[SwingIndex - 1].ReleaseSeconds > 0.0f)
	{
		return StringSwings[SwingIndex - 1].ReleaseSeconds;
	}

	return Branches.IsValidIndex(BranchIndex) ? Branches[BranchIndex].ReleaseSeconds : 0.0f;
}

float UTDChargedAttackAbility::GetSwingLungeDistanceCm(int32 SwingIndex, int32 BranchIndex) const
{
	if (StringSwings.IsValidIndex(SwingIndex - 1) && StringSwings[SwingIndex - 1].LungeDistanceCm > 0.0f)
	{
		return StringSwings[SwingIndex - 1].LungeDistanceCm;
	}

	return Branches.IsValidIndex(BranchIndex) ? Branches[BranchIndex].LungeDistanceCm : 0.0f;
}

float UTDChargedAttackAbility::GetSwingLungeDurationSeconds(int32 SwingIndex, int32 BranchIndex) const
{
	if (StringSwings.IsValidIndex(SwingIndex - 1) && StringSwings[SwingIndex - 1].LungeDurationSeconds > 0.0f)
	{
		return StringSwings[SwingIndex - 1].LungeDurationSeconds;
	}

	return Branches.IsValidIndex(BranchIndex) ? Branches[BranchIndex].LungeDurationSeconds : 0.0f;
}

UAnimMontage* UTDChargedAttackAbility::GetActiveAttackMontage() const
{
	// A swapped-in tier montage is what is actually on the slot, so it is what every position,
	// rate and notify question is about. Checked before the swing, never instead of it.
	if (ActiveTierMontage)
	{
		return ActiveTierMontage;
	}

	// A swing whose montage was left unset falls back to the first hit's rather than to nothing --
	// the same reasoning as the hitbox fallback: a silently unplayable swing is the failure this
	// project keeps a trap list for, and the warning below is the tell rather than a crash.
	if (StringSwings.IsValidIndex(CurrentSwingIndex - 1))
	{
		if (UAnimMontage* SwingMontage = StringSwings[CurrentSwingIndex - 1].Montage)
		{
			return SwingMontage;
		}

		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("String swing %d has no montage; falling back to the first hit's. Its timings will be wrong."),
			CurrentSwingIndex);
	}
	return AttackMontage;
}

void UTDChargedAttackAbility::ReleaseCommitmentTag()
{
	// Guarded on the attack having actually committed, so a hit landing before the checkpoint --
	// which nothing produces today -- cannot remove a tag that was never added. Removing an absent
	// loose tag is harmless; the guard is here so the *intent* reads correctly.
	if (!bAttackCommitted || !CommittedTag.IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(CommittedTag);
	}

	TD_TIMING_LOG(TEXT("[%.3f] WAIVER     %s dropped %s on contact"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*GetNameSafe(GetAvatarActorFromActorInfo()),
		*CommittedTag.ToString());
}

bool UTDChargedAttackAbility::IsChainOutOpen() const
{
	// **A parried attack cannot chain.** One condition, and it is the whole of what a parry does to
	// the attacker beyond planting them: recovery is the punish window, and chaining out of it is
	// precisely the escape that would hand the reward back. A parried light would otherwise race
	// its own chain and arrive again before the punish it just earned could land.
	if (bParried)
	{
		return false;
	}

	if (!bInRecovery || !IsNonFinalStringLight())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() >= RecoveryStartedAt + ChainOpenAfterRecoverySeconds;
}

bool UTDChargedAttackAbility::TryChainOutForBufferedPress()
{
	if (!IsChainOutOpen())
	{
		return false;
	}

	// Chaining *skips the rest of recovery* -- that is the entire mechanism behind "lights have a
	// quite long recovery, but can be chained which skips it". The early end runs the ordinary
	// EndAbility funnel, so facing, tags, homing and the lunge clean up exactly as on a natural
	// end, and EndAbility itself opens the link window for the activation the buffer fires next.
	TD_TIMING_LOG(TEXT("[%.3f] STRING     chain out of swing %d, %.0fms into recovery"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		CurrentSwingIndex,
		GetWorld() ? (GetWorld()->GetTimeSeconds() - RecoveryStartedAt) * 1000.0f : 0.0f);

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	return true;
}

const TArray<FTDAttackHitbox>& UTDChargedAttackAbility::GetAttackHitboxes() const
{
	// An authored-but-empty branch falls back to the ability's own set rather than to nothing. The
	// alternative is an attack that silently deals no damage -- reachable the moment the old
	// per-branch TraceRadius was removed, since every existing branch deserialises empty.
	return GetSwingHitboxes(CurrentSwingIndex, SelectedBranchIndex);
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
	const TArray<FTDAttackHitbox>& ResolvedHitboxes = GetSwingHitboxes(CurrentSwingIndex, BranchIndex);

	float FurthestReachCm = 0.0f;
	for (const FTDAttackHitbox& Hitbox : ResolvedHitboxes)
	{
		FurthestReachCm = FMath::Max(FurthestReachCm, Hitbox.MaxReachCm);
	}

	// Travel plus reach is exactly the furthest a body can be and still be struck: the base lunge
	// runs before the tiers diverge, the branch lunge from commit, the hitbox extends further still.
	// The margin on top is the only judgement in the sum -- see AimAssistMarginCm.
	const float ReachCm = LungeDistanceCm + GetSwingLungeDistanceCm(CurrentSwingIndex, BranchIndex)
		+ FurthestReachCm + AimAssistMarginCm;

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

	// The string's fate at this swing's end, decided where every exit converges. A cancelled swing
	// -- a defensive cancel of the windup, death, a montage interrupt -- kills the string outright.
	// A *completed* non-final string light opens the link window instead, whether the end was
	// natural recovery or the chain-out's early exit. Heavy and charged already reset at commit.
	if (ATDCombatCharacter* CombatCharacter = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (bWasCancelled)
		{
			CombatCharacter->ResetString(TEXT("swing cancelled"));
		}
		else if (bParried)
		{
			// **A parried swing takes the string with it.** Gated here rather than at the parry
			// itself, because this is where the link window is *opened*: resetting at contact and
			// falling through to the branch below would re-open the window the reset just closed.
			CombatCharacter->ResetString(TEXT("parried"));
		}
		else if (bAttackCommitted && IsNonFinalStringLight())
		{
			CombatCharacter->OpenStringLinkWindow(StringLinkWindowSeconds);
		}
	}

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
