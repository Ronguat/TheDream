// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Abilities/TDMeleeAttackAbility.h"
#include "Combat/Tasks/AbilityTask_MeleeTrace.h"
#include "Combat/Attributes/TDAttributeSet.h"
#include "Combat/TDGameplayTags.h"
#include "Combat/TDCombatDebug.h"
#include "Core/TheDreamCharacter.h"
#include "Combat/TDCombatCharacter.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Combat/Tasks/AbilityTask_FacingLunge.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

void UTDMeleeAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// Reset per activation: these instances are InstancedPerActor and therefore reused, so a parry
	// suffered by a previous swing would otherwise forbid this one from chaining.
	bParried = false;

	StartMeleeTrace(GetAttackHitboxes());

	// Starts on the same frame as the montage, and is shared by every tier -- per-tier travel
	// begins at the commit checkpoint.
	StartLunge(LungeDistanceCm, GetBaseLungeDurationSeconds(), LungeStrengthCurve, LungeStandoffCm);

	// A plain swing has no derived timing, so it plays at the montage's authored speed.
	if (!StartAttackMontage(NAME_None, 1.0f))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
	}
}

void UTDMeleeAttackAbility::LogTargetGeometry(const TCHAR* Phase) const
{
	if (!TDShouldTraceCombatTiming())
	{
		return;
	}

	const AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !World)
	{
		return;
	}

	// Generous on purpose: the interesting samples are the ones just outside the wedge, because
	// those are the misses being investigated.
	static constexpr float ProbeRadiusCm = 1000.0f;

	const FVector Origin = Avatar->GetActorLocation();

	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(ProbeRadiusCm),
		FCollisionQueryParams(SCENE_QUERY_STAT(TDTargetGeometry), /*bTraceComplex=*/false, Avatar));

	const AActor* Nearest = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		const AActor* Candidate = Overlap.GetActor();
		if (!Candidate || Candidate == Avatar || !Candidate->IsA<APawn>())
		{
			continue;
		}

		const float Distance = FVector::Dist2D(Candidate->GetActorLocation(), Origin);
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = Candidate;
		}
	}

	if (!Nearest)
	{
		TD_TIMING_LOG(TEXT("[%.3f] TARGET     %s  no pawn within %.0f cm"),
			World->GetTimeSeconds(), Phase, ProbeRadiusCm);
		return;
	}

	// Signed and relative to the attacker's facing, so the *difference* between the commit sample
	// and the release sample is the slide -- the number this exists for.
	const FVector Delta = Nearest->GetActorLocation() - Origin;
	const float BearingDegrees = FMath::FindDeltaAngleDegrees(
		Avatar->GetActorRotation().Yaw,
		FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));

	TD_TIMING_LOG(TEXT("[%.3f] TARGET     %s  '%s' dist=%.1f bearing=%+.1f"),
		World->GetTimeSeconds(),
		Phase,
		*Nearest->GetName(),
		NearestDistance,
		BearingDegrees);
}

void UTDMeleeAttackAbility::ApplyAimAssist(const FTDAttackHitbox& AssistWedge)
{
	ATheDreamCharacter* Avatar = GetFacingCharacter();
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !World || !AssistWedge.IsEnabled())
	{
		return;
	}

	// Measured from the camera, not the body: the assist aids the attacker's input, and the input
	// is the camera.
	const float AimYaw = Avatar->GetAimYawDegrees();

	float Bearing = 0.0f;
	const AActor* Target = ATDCombatCharacter::FindAimAssistTarget(Avatar, AimYaw, AssistWedge, TargetImmunityTags, Bearing);
	if (!Target)
	{
		TD_TIMING_LOG(TEXT("[%.3f] AIM ASSIST no candidate in wedge (reach=%.0f arc=%.0f)"),
			World->GetTimeSeconds(), AssistWedge.MaxReachCm, AssistWedge.ArcDegrees);
		return;
	}

	// **All the way onto the target, not to the edge of a tolerance.** The wedge is the margin of
	// error, and it is aimed rather than corrected into. Usually a no-op in practice: homing has
	// been closing this gap for the whole base lunge, so what lands here is the residual.
	const FVector Delta = Target->GetActorLocation() - Avatar->GetActorLocation();
	const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	const float Correction = FMath::FindDeltaAngleDegrees(Avatar->GetActorRotation().Yaw, TargetYaw);

	FRotator NewRotation = Avatar->GetActorRotation();
	NewRotation.Yaw = TargetYaw;
	Avatar->SetActorRotation(NewRotation);

	TD_TIMING_LOG(TEXT("[%.3f] AIM ASSIST '%s' aimBearing=%+.1f -> turned %+.1f"),
		World->GetTimeSeconds(), *Target->GetName(), Bearing, Correction);
}

void UTDMeleeAttackAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// The single funnel the header describes: unconditional and idempotent, because every exit
	// converges here. The coil flag rides along -- a coil cancelled before its commit checkpoint
	// would otherwise leave the character turning at the coil rate forever.
	if (ATheDreamCharacter* Character = GetFacingCharacter())
	{
		Character->SetAbilityFacingLocked(false);
		Character->SetAbilityCoiling(false);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

ATheDreamCharacter* UTDMeleeAttackAbility::GetFacingCharacter() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo ? Cast<ATheDreamCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
}

UAbilityTask_MeleeTrace* UTDMeleeAttackAbility::StartMeleeTrace(const TArray<FTDAttackHitbox>& InHitboxes)
{
	// The active montage is passed so the hitboxes only go live on *this* attack's Release Window.
	// The events reach the whole ASC and carry no ownership, so without it a second montage
	// carrying the notify would open every listening trace. The filter follows the swing rather
	// than the authored field, because per-swing montages exist.
	UAbilityTask_MeleeTrace* TraceTask = UAbilityTask_MeleeTrace::MeleeTrace(
		this,
		InHitboxes,
		bDrawDebugTrace,
		GetActiveAttackMontage());
	TraceTask->OnHit.AddDynamic(this, &UTDMeleeAttackAbility::HandleTraceHit);
	TraceTask->ReadyForActivation();

	return TraceTask;
}

bool UTDMeleeAttackAbility::StartAttackMontage(FName StartSection, float PlayRate)
{
	// Resolved once for the whole function: which montage this activation plays is the swing's
	// business (see GetActiveAttackMontage), and every log, guard and task below must agree on it.
	UAnimMontage* ActiveMontage = GetActiveAttackMontage();
	if (!ActiveMontage)
	{
		return false;
	}

	// Section and notify tables are invisible to the MCP reflection layer but plain C++ here. A
	// section that ends before the montage does, with nothing chained after it, ends the montage
	// there -- naturally, so the task reports OnBlendOut rather than OnInterrupted, which is
	// indistinguishable from a normal finish without this. The notify dump is also the authored
	// truth that ReleaseStartSeconds duplicates by hand, so a drift is visible on the same screen.
	if (TDShouldTraceCombatTiming() && ActiveMontage)
	{
		TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    '%s' sections=%d  length=%.4f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			*ActiveMontage->GetName(),
			ActiveMontage->CompositeSections.Num(),
			ActiveMontage->GetPlayLength());

		for (int32 Index = 0; Index < ActiveMontage->CompositeSections.Num(); ++Index)
		{
			const FCompositeSection& Composite = ActiveMontage->CompositeSections[Index];
			TD_TIMING_LOG(TEXT("[%.3f] MONTAGE      [%d] '%s' start=%.4f nextSection='%s'"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
				Index,
				*Composite.SectionName.ToString(),
				Composite.GetTime(),
				*Composite.NextSectionName.ToString());
		}

		for (int32 Index = 0; Index < ActiveMontage->Notifies.Num(); ++Index)
		{
			const FAnimNotifyEvent& Event = ActiveMontage->Notifies[Index];
			TD_TIMING_LOG(TEXT("[%.3f] MONTAGE      notify '%s' trigger=%.4f duration=%.4f"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
				*Event.NotifyName.ToString(),
				Event.GetTriggerTime(),
				Event.GetDuration());
		}
	}

	// **An attack montage must play an in-place clip, because Lunge drives the movement.**
	//
	// Animation root motion overrides Velocity and allows no other root motion sources
	// (CharacterMovementComponent::PerformMovement), so while a root-motion montage plays every
	// UAbilityTask_ApplyRootMotion* source is ignored outright. **Scaling the animation to zero
	// does not help and fails silently**: the montage still reports HasAnimRootMotion(), so the
	// source stays ignored and the zeroed animation velocity wins -- the character stands still for
	// the whole swing. So the clip carries no root motion (AM_Attack plays the library's _IP
	// variant, not _RM) and nothing here scales anything. This warning is the enforcement: the
	// dependency is content-side and a repointed segment would break every lunge in silence.
	if (ActiveMontage->HasRootMotion() && LungeDistanceCm > 0.0f)
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("'%s' has root motion, so it will suppress every authored lunge on this attack. ")
			TEXT("Point its segment at an in-place (_IP) clip."),
			*ActiveMontage->GetName());
	}

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, ActiveMontage, PlayRate, StartSection,
		/*bStopWhenAbilityEnds=*/true, /*AnimRootMotionTranslationScale=*/1.0f);
	// Four separate wrappers rather than two shared handlers, so the trace can say which delegate
	// ended the attack. They have different causes and different fixes.
	MontageTask->OnCompleted.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageCompleted);
	MontageTask->OnBlendOut.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageBlendedOut);
	MontageTask->OnInterrupted.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UTDMeleeAttackAbility::HandleMontageCancelled);
	MontageTask->ReadyForActivation();

	return true;
}

void UTDMeleeAttackAbility::HandleTraceHit(const FHitResult& Hit)
{
	// Damage is authority-only state; clients see it arrive by attribute replication.
	// DamageEffectClass is deliberately *not* checked here: it gates damage, a consequence of the
	// hit rather than the hit itself, and the lunge stops on the hit, so an ability with no damage
	// effect configured must still stop rather than slide onward.
	if (!HasAuthority(&CurrentActivationInfo))
	{
		return;
	}

	AActor* HitActor = Hit.GetActor();
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC)
	{
		// Hit something that cannot take damage, such as a wall. It does not stop the lunge either:
		// geometry is not a target, and a lunge into a wall is already handled by the movement
		// component sliding along it.
		return;
	}

	// I-frames, floor invincibility, parry and block are all resolved here rather than on the
	// defender, this being the only place that knows a hit was resolved at all -- a defender cannot
	// refuse damage it never sees. Their order is the precedence: intangibility is a stronger claim
	// than negation, negation stronger than absorption.
	//
	// **A dodged attack runs on, lunge included**, which is why the stop below sits after this
	// check. Stopping the attacker dead would hand them the spacing as compensation for being read.
	if (!TargetImmunityTags.IsEmpty() && TargetASC->HasAnyMatchingGameplayTags(TargetImmunityTags))
	{
		return;
	}

	// A body on the ground is untouchable until *any* rise begins, auto or chosen; from that frame
	// each get-up option prices its own vulnerability. **The rise-begin frame resolves to the
	// defender**: IsKnockdownInvulnerable() goes false the instant BeginKnockdownRise runs, and a
	// hit arriving that same frame has already been refused by the check above it in tick order.
	//
	// Deliberately *not* an i-frame tag: borrowing State.Dodging would make every attack's
	// TargetImmunityTags decide knockdown's invincibility as a side effect of tuning the dodge.
	if (const ATDCombatCharacter* Downed = Cast<ATDCombatCharacter>(HitActor))
	{
		if (Downed->IsKnockdownInvulnerable())
		{
			return;
		}
	}

	// **In practice none of the three defences can co-occur**: GA_Parry refuses activation while
	// State.Dodging or State.Blocking is present, so this ordering describes what happens if one of
	// those guarantees breaks rather than a case anyone can reach today. The dedup that makes the
	// rest of this release window inert against the parrier is already done: ResolveHits adds every
	// geometrically-valid candidate to ActorsHitThisWindow *before* broadcasting.
	if (ATDCombatCharacter* Parrier = Cast<ATDCombatCharacter>(HitActor))
	{
		// **Grace is checked here and nowhere else, which is what "self-contained" means.** It
		// grants nothing this branch does not already grant and prevents nothing anywhere; see
		// ParryGraceSeconds.
		if (Parrier->IsParryWindowOpen() || Parrier->IsInParryGrace())
		{
			// **The attacker is planted, exactly as on a connecting hit, and that is the entire
			// reward.** Stopping the lunge manufactures the whiff at zero centimetres, so the
			// attacker rides their own attack into recovery -- already the punish window, and
			// already scaled to the tier they chose.
			StopLunge();

			// Everything the hit would have done is simply not done: no damage, no stamina damage,
			// no blockstun, no hitstun, no knockback. The early return is the whole negation.
			bParried = true;

			// This one flag stops two things: IsChainOutOpen reads it to forbid skipping recovery, and
			// EndAbility reads it to kill the string outright, so the attacker's next press starts a
			// fresh swing 0. The string dies *there* rather than here, because that is where the link
			// window is opened -- resetting at contact and falling through would re-open it.

			// **Read before anything ends**, because the swing that authored it is about to stop
			// existing.
			const float LockoutSeconds = GetAttackParryLockoutSeconds();
			ATDCombatCharacter* ParriedAttacker = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo());

			Parrier->NotifyParrySuccess(GetAvatarActorFromActorInfo());

			// **The catch ends the swing through the ordinary funnel.** Facing, lunge, homing and
			// tags all clean up exactly as on a natural end, and the hitbox goes dead for
			// *everyone*, so a caught swing cannot keep killing bystanders on its way out.
			EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, /*bWasCancelled=*/true);

			// **After EndAbility, and the order is load-bearing.** The lockout takes the movement
			// lock; the ending attack *releases* the movement lock it took at activation. Locking
			// first would have the attack's own teardown hand movement straight back, leaving an
			// attacker refused every ability while free to walk.
			if (ParriedAttacker)
			{
				ParriedAttacker->EnterParryLockout(LockoutSeconds);
			}
			return;
		}
	}

	// **A blocked attack stops its lunge, unlike a dodged one**: the defender is still standing
	// there with a body in the way, so sliding through them would be the attacker walking into a
	// guard and out the other side.
	if (ATDCombatCharacter* Defender = Cast<ATDCombatCharacter>(HitActor))
	{
		const AActor* Avatar = GetAvatarActorFromActorInfo();
		if (Defender->IsBlocking() && Avatar && Defender->IsGuardFacing(Avatar->GetActorLocation()))
		{
			StopLunge();

			// Stamina instead of health, and the guard break falls out of the bar reaching zero
			// inside this call rather than being decided here. That keeps "what breaks a guard" a
			// property of the stamina economy rather than a rule each attack has to restate.
			Defender->ApplyStaminaDamage(GetAttackStaminaDamage());

			// Logged before the blockstun it causes, so the trace reads cause then effect -- the
			// tell for a break, "BLOCKED with no BLOCKSTUN beside it", is read downward.
			TD_TIMING_LOG(TEXT("[%.3f] BLOCKED    %s by %s  staminaDamage=%.0f  remaining=%.1f"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
				*GetNameSafe(Avatar),
				*Defender->GetName(),
				GetAttackStaminaDamage(),
				Defender->GetStamina());

			// **Blockstun only if the guard survived, which is why this reads the defender back
			// instead of predicting.** ApplyStaminaDamage above may have broken the guard, and a
			// break already refuses every ability for longer than any blockstun, so applying both
			// would stack two lockouts for one hit. A broken guard is not a successful block.
			if (!Defender->IsGuardBroken())
			{
				Defender->EnterBlockstun(GetAttackBlockstunSeconds());
			}

			// The blocked spacing reset. Applied whether or not this hit broke the guard -- the
			// contact was blocked either way. Zero spacing is none.
			ApplyKnockbackToTarget(Defender, /*bBlocked=*/true);
			return;
		}
	}

	// A viable target was struck, so the lunge is finished -- keyed to the hit rather than to the
	// damage landing. The two are the same instant on the server today but are different events,
	// and tying movement to the slower would eventually read as a slide. The standoff gate cannot
	// cover this: it *pauses* while a body is in the way and resumes when one is not, so a target
	// dying mid-attack loses its capsule and the attacker slides through. See StopLunge.
	StopLunge();

	if (!DamageEffectClass)
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(DamageEffectClass, GetAbilityLevel());
	if (!SpecHandle.IsValid())
	{
		return;
	}

	// Designers tune damage as a positive number; the effect subtracts, so send it negative.
	SpecHandle.Data->SetSetByCallerMagnitude(TDTags::Data_Damage, -GetAttackDamage());

	if (UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo())
	{
		SourceASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);

		// Read back off the target after the effect lands, exactly as ApplyStaminaDamage reads the
		// bar: the attribute set clamps, so the clamped result is the only truthful ledger entry.
		TD_TIMING_LOG(TEXT("[%.3f] DAMAGED    %s by %s  damage=%.0f  health=%.1f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			*GetNameSafe(HitActor),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			GetAttackDamage(),
			TargetASC->GetNumericAttribute(UTDAttributeSet::GetHealthAttribute()));
	}

	// The hit's effect on the victim beyond the bar. After the damage deliberately, so a killing
	// blow resolves death first -- every call below no-ops on the dead.
	//
	// **Knockdown and hitstun are alternatives, not layers.** A swing that authored a grade floors
	// the victim instead of stunning them and carries them radially instead of pushing along the
	// facing axis, which leaves that swing's HitstunSeconds with exactly one remaining job: keying
	// the *attacker's* movement return through the waiver below.
	if (ATDCombatCharacter* Victim = Cast<ATDCombatCharacter>(HitActor))
	{
		if (GetAttackKnockdownGrade() != ETDKnockdownGrade::None)
		{
			Victim->EnterKnockdown(GetAttackKnockdownGrade(), GetAvatarActorFromActorInfo());
		}
		else
		{
			Victim->EnterHitstun(GetAttackHitstunSeconds());

			// **Every clean hit turns the victim's body toward its attacker**, knockdown or not.
			// Knockdown's own entry starts the same turn, which is why this sits on the hitstun
			// branch alone.
			Victim->BeginForcedFacing(GetAvatarActorFromActorInfo());

			ApplyKnockbackToTarget(Victim, /*bBlocked=*/false);
		}
	}

	// **The on-hit waiver: punishment attaches to failure, and a hit is not failure.** Recovery
	// stays the punish window where it was derived -- against whiffs and against blocks -- but was
	// never meant to pin an attacker who *connected*.
	//
	// Two halves, deliberately not the same length. Defensive activations open **instantly**, by
	// dropping the commitment marker: from here the attacker may block, dodge or parry out of their
	// own recovery. Offense is untouched, since the chain rules already govern it, and this must not
	// become a second way to chain.
	ReleaseCommitmentTag();

	if (ATDCombatCharacter* Attacker = Cast<ATDCombatCharacter>(GetAvatarActorFromActorInfo()))
	{
		// **Movement returns later than defense, and the delay is derived rather than chosen.**
		// Earlier lets the attacker walk in and erode the authored spacing the fixed-destination
		// knockback just paid for; later is dead freedom, since by then the victim is out of
		// hitstun. So it is exactly contact plus this swing's own hitstun.
		Attacker->BeginOnHitMovementWaiver(GetAttackHitstunSeconds());

		// Held intent resumes on the frame the hit lands: a guard held through the read comes up now.
		Attacker->RequestResumePass();
	}
}

void UTDMeleeAttackAbility::ApplyKnockbackToTarget(ATDCombatCharacter* Target, bool bBlocked)
{
	const float SpacingCm = GetKnockbackSpacingCm(bBlocked);
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Target || !Avatar || SpacingCm <= 0.0f)
	{
		return;
	}

	// The attacker's frame is stable here by prior design: hits resolve only inside the release
	// window, facing froze at commit, and the lunge stopped on this very hit -- so the axis the
	// destination sits on is planted, which is what makes "the same spot every time" true.
	const FVector AttackerLoc = Avatar->GetActorLocation();
	FVector Facing = UsesRadialKnockback()
		? Target->GetActorLocation() - AttackerLoc
		: Avatar->GetActorForwardVector();
	Facing.Z = 0.0f;
	if (!Facing.Normalize())
	{
		// Radial with no bearing -- co-located -- falls back to the facing axis.
		Facing = Avatar->GetActorForwardVector();
		Facing.Z = 0.0f;
		if (!Facing.Normalize())
		{
			return;
		}
	}

	const FVector ToTarget = Target->GetActorLocation() - AttackerLoc;
	const float CurrentAlongCm = FVector::DotProduct(ToTarget, Facing);
	const float LateralCm = FVector::DotProduct(ToTarget, FVector::CrossProduct(FVector::UpVector, Facing));

	// **Never inward.** A contact beyond the authored spacing keeps its distance and is only
	// centred; pulling a defender toward the sword is not something either spacing means. Deleting
	// this max() is the whole change if a pull-in is ever wanted.
	const float FinalSpacingCm = FMath::Max(SpacingCm, CurrentAlongCm);

	FVector Destination = AttackerLoc + Facing * FinalSpacingCm;
	Destination.Z = Target->GetActorLocation().Z;

	TD_TIMING_LOG(TEXT("[%.3f] KNOCKBACK  %s by %s  spacing=%.0f (authored %.0f)  centred=%.1fcm%s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		*Target->GetName(),
		*GetNameSafe(Avatar),
		FinalSpacingCm,
		SpacingCm,
		LateralCm,
		bBlocked ? TEXT(" (blocked)") : TEXT(""));

	Target->ReceiveKnockback(Destination, KnockbackDurationSeconds, KnockbackTimeMappingCurve);
}

void UTDMeleeAttackAbility::HandleMontageCompleted()
{
	TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    OnCompleted"), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	HandleMontageFinished();
}

void UTDMeleeAttackAbility::HandleMontageBlendedOut()
{
	// Where the blend starts, and which section was playing when it did. With blendOutTriggerTime
	// at -1 the natural end-blend cannot begin before (length - blendTime), so a position well
	// short of that means something else ended the section -- and the section name is the only
	// thing that can say what.
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	UAnimMontage* ActiveMontage = GetActiveAttackMontage();

	const float Position = (AnimInstance && ActiveMontage) ? AnimInstance->Montage_GetPosition(ActiveMontage) : -1.0f;
	const FName Section = (AnimInstance && ActiveMontage) ? AnimInstance->Montage_GetCurrentSection(ActiveMontage) : NAME_None;
	const bool bPlaying = (AnimInstance && ActiveMontage) ? AnimInstance->Montage_IsPlaying(ActiveMontage) : false;

	TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    OnBlendOut  pos=%.4f section=%s playing=%d montageLen=%.4f"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
		Position,
		*Section.ToString(),
		bPlaying ? 1 : 0,
		ActiveMontage ? ActiveMontage->GetPlayLength() : -1.0f);

	HandleMontageFinished();
}

void UTDMeleeAttackAbility::HandleMontageCancelled()
{
	TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    OnCancelled"), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	HandleMontageInterrupted();
}

void UTDMeleeAttackAbility::HandleMontageFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UTDMeleeAttackAbility::HandleMontageInterrupted()
{
	TD_TIMING_LOG(TEXT("[%.3f] MONTAGE    OnInterrupted"), GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f);
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

bool UTDMeleeAttackAbility::IsWindowForThisAttack(const FGameplayEventData& Payload) const
{
	// The notify broadcasts to the whole ASC and carries no ownership, so without this any montage
	// carrying a Release Window would drive this attack's play rate -- harmless while exactly one
	// montage carries the notify, silently wrong the moment a second does. UAbilityTask_MeleeTrace
	// guards the same way. Null means accept any. The comparison is against the *active* montage:
	// with per-swing montages, filtering on the authored first-hit field would reject every window
	// swing 2 onward fires, silently and as no damage.
	const UAnimMontage* ActiveMontage = GetActiveAttackMontage();
	if (!ActiveMontage)
	{
		return true;
	}

	if (Payload.OptionalObject == ActiveMontage)
	{
		return true;
	}

	// Ungated: a mismatch here means the attack never applies its release rate and never
	// takes it off again -- both silent, and the second is the bug this guard shipped with.
	UE_LOG(LogTDCombatTiming, Warning,
		TEXT("Release Window from '%s' ignored; this attack is playing '%s'."),
		Payload.OptionalObject ? *Payload.OptionalObject->GetName() : TEXT("<none>"),
		*ActiveMontage->GetName());

	return false;
}

float UTDMeleeAttackAbility::GetBlendOutStartSeconds(float PlayRate) const
{
	const UAnimMontage* ActiveMontage = GetActiveAttackMontage();
	if (!ActiveMontage)
	{
		return -1.0f;
	}

	const float Length = ActiveMontage->GetPlayLength();
	const float TriggerTime = ActiveMontage->BlendOutTriggerTime;

	// A trigger time is an authored montage position, measured back from the end, and it does
	// not care how fast the montage is playing.
	if (TriggerTime >= 0.0f)
	{
		return Length - TriggerTime;
	}

	// **Without one, the boundary moves with the play rate**, the whole subtlety here. A negative
	// trigger means "blend so it finishes as the montage does", and the engine tests that in *time*
	// rather than position: it blends once the remaining montage would take less than the blend's
	// duration to play. Halve the rate and the blend starts half as far from the end.
	//
	// **Treating this as the fixed position `Length - BlendTime` is right only at rate 1.0**, so the
	// error hides at rates near it and grows as recovery is authored slower -- a few percent at
	// 0.94, about half again at 0.50.
	return Length - ActiveMontage->BlendOut.GetBlendTime() * FMath::Max(PlayRate, TDMinPlayRate);
}

float UTDMeleeAttackAbility::ComputeRecoveryPlayRate(float FromPosition, float TargetSeconds) const
{
	const UAnimMontage* ActiveMontage = GetActiveAttackMontage();
	if (!ActiveMontage || FromPosition < 0.0f || TargetSeconds <= 0.0f)
	{
		return -1.0f;
	}

	const float Length = ActiveMontage->GetPlayLength();
	const float Remaining = Length - FromPosition;
	if (Remaining <= KINDA_SMALL_NUMBER)
	{
		return -1.0f;
	}

	const float TriggerTime = ActiveMontage->BlendOutTriggerTime;
	if (TriggerTime >= 0.0f)
	{
		// Fixed boundary: cover the montage up to it in the authored time.
		const float ToBoundary = (Length - TriggerTime) - FromPosition;
		return (ToBoundary <= KINDA_SMALL_NUMBER)
			? -1.0f
			: FMath::Max(ToBoundary / TargetSeconds, TDMinPlayRate);
	}

	// Rate-dependent boundary. Solving for the rate R that makes recovery last exactly
	// TargetSeconds, the blend beginning BlendTime*R before the montage's end:
	//
	//     (Length - BlendTime*R - FromPosition) / R = TargetSeconds
	//  => Length - FromPosition = R * (TargetSeconds + BlendTime)
	//  => R = (Length - FromPosition) / (TargetSeconds + BlendTime)
	//
	// The blend cancels out of the position but not the time, which is why the naive form is wrong
	// and wrong by more the slower recovery is authored.
	const float BlendTime = ActiveMontage->BlendOut.GetBlendTime();
	return FMath::Max(Remaining / (TargetSeconds + BlendTime), TDMinPlayRate);
}

float UTDMeleeAttackAbility::GetMontagePosition() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	UAnimMontage* ActiveMontage = GetActiveAttackMontage();
	return (AnimInstance && ActiveMontage) ? AnimInstance->Montage_GetPosition(ActiveMontage) : -1.0f;
}

void UTDMeleeAttackAbility::SetMontagePlayRate(float PlayRate) const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	UAnimMontage* ActiveMontage = GetActiveAttackMontage();
	if (AnimInstance && ActiveMontage && AnimInstance->Montage_IsPlaying(ActiveMontage))
	{
		AnimInstance->Montage_SetPlayRate(ActiveMontage, FMath::Max(PlayRate, TDMinPlayRate));
	}
}

