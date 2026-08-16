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

	StartMeleeTrace(GetAttackHitboxes());

	// The base lunge starts on the same frame as the montage, deliberately: it *is* the
	// responsiveness of the press, and a frame of stillness first is exactly what it exists to
	// remove. Shared by every tier -- per-tier travel begins at the commit checkpoint.
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

	// Generous on purpose. This is a diagnostic, and a probe sized to the wedge could only ever
	// confirm what the wedge already decided -- the interesting samples are the ones just outside
	// it, because those are the misses being investigated.
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

	// Bearing is signed and relative to the attacker's facing, so the *difference* between the
	// commit sample and the release sample is the slide -- which is the number this exists for.
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

	// Measured from the camera, not the body. **The assist aids the attacker's input and the input
	// is the camera; damage stays in the actor frame because a defender has to be able to trust
	// what the body is doing.** They coincide unless homing has already turned the body, which is
	// precisely when the distinction starts mattering.
	const float AimYaw = Avatar->GetAimYawDegrees();

	float Bearing = 0.0f;
	const AActor* Target = ATDCombatCharacter::FindAimAssistTarget(Avatar, AimYaw, AssistWedge, TargetImmunityTags, Bearing);
	if (!Target)
	{
		TD_TIMING_LOG(TEXT("[%.3f] AIM ASSIST no candidate in wedge (reach=%.0f arc=%.0f)"),
			World->GetTimeSeconds(), AssistWedge.MaxReachCm, AssistWedge.ArcDegrees);
		return;
	}

	// **All the way onto the target, not to the edge of a tolerance.** A deadzone lived here for
	// one revision and was deleted: it existed to protect leading a moving target, and that is not
	// a technique this game has -- the hitbox opens 50 ms after commit into a 60 degree arc, so a
	// walking target crosses about 7 degrees of a +/-36 window. It bought nothing and cost the
	// correction range, since pairing a 10 degree deadzone with a 10 degree half-arc collapsed the
	// whole thing to the subtended term and gave least help at the ranges that needed most.
	//
	// **The wedge is the margin of error, and it is aimed rather than corrected into.** Aim inside
	// it, space inside reach, and the hit follows short of a defensive action.
	//
	// Usually a no-op in practice: homing has been closing this gap for the whole base lunge, so
	// what lands here is the residual. It is what makes a wide wedge affordable without a pop.
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
	// The single funnel the header doc describes: unconditional and idempotent, because every
	// exit converges here. The coil flag rides along -- a coil cancelled before its commit
	// checkpoint would otherwise leave the character turning at the coil rate forever.
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
	// carrying the notify would open every listening trace -- and with per-swing montages that is
	// no longer hypothetical, so the filter must follow the swing rather than the authored field.
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

	// A montage's section table is invisible to the MCP reflection layer (compositeSections
	// cannot be read), so it has never been checked from outside. It is plain C++ here. A
	// section that ends before the montage does, with nothing chained after it, ends the
	// montage there -- naturally, so the task reports OnBlendOut rather than OnInterrupted,
	// which is indistinguishable from a normal finish without this.
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

		// Notify placement is not readable through the MCP layer either, so until now the only
		// way to learn where a Release Window sits was to play an attack and read the edge it
		// fired at -- a round trip per marker adjustment. It is plain C++ here. This is also the
		// authored truth that ReleaseStartSeconds duplicates by hand, so a drift between them is
		// visible on the same screen rather than needing the warning to catch it.
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
	// Not a preference. CharacterMovementComponent, in PerformMovement: "Animation root motion
	// overrides Velocity and currently doesn't allow any other root motion sources" -- so while a
	// root-motion montage plays, every UAbilityTask_ApplyRootMotion* source is ignored outright.
	//
	// Scaling the animation to zero does **not** hand movement to the lunge, which is the trap
	// worth stating because it is the obvious thing to try and it fails silently: the montage
	// still reports HasAnimRootMotion(), so the source is still ignored and the zeroed animation
	// velocity wins. The character stands perfectly still for the whole swing. Measured on
	// 2026-08-12 as exactly zero displacement across a dozen charged attacks.
	//
	// So the clip carries no root motion (AM_Attack plays the library's _IP variant, not _RM) and
	// nothing here scales anything. The warning below is the enforcement, because the dependency
	// is content-side and a repointed segment would otherwise break every lunge in silence.
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
	// Bound to four separate wrappers rather than two shared handlers, so the trace can say
	// which delegate ended the attack. They have different causes and different fixes.
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
	//
	// DamageEffectClass is deliberately *not* checked here any more. It gates damage, which is a
	// consequence of the hit, not the hit itself -- and the lunge now stops on the hit, so an
	// ability with no damage effect configured must still stop rather than slide onward.
	if (!HasAuthority(&CurrentActivationInfo))
	{
		return;
	}

	AActor* HitActor = Hit.GetActor();
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC)
	{
		// Hit something that cannot take damage, such as a wall. It does not stop the lunge either:
		// geometry is not a target, this project does not track hits against it, and a lunge into a
		// wall is already handled by the movement component sliding along it.
		return;
	}

	// I-frames. Checked here rather than on the defender because this is the only place
	// that knows a hit was resolved at all -- a dodge cannot refuse damage it never sees.
	//
	// **A dodged attack runs on, lunge included**, which is why the stop below sits after this
	// check rather than before it. An evade is supposed to make the swing sail past; stopping the
	// attacker dead would hand them the spacing as compensation for being read, and turn a
	// successful defensive read into a positional reward for the person who was beaten.
	if (!TargetImmunityTags.IsEmpty() && TargetASC->HasAnyMatchingGameplayTags(TargetImmunityTags))
	{
		return;
	}

	// Block, resolved here for the same reason i-frames are: this is the only place that knows a
	// hit happened at all, and a defender cannot refuse damage it never sees.
	//
	// **Ordered after i-frames and before the lunge stop, and both halves of that matter.** A dodge
	// beats a block if somehow both are live, because intangibility is the stronger claim. And a
	// blocked attack *does* stop its lunge, unlike a dodged one: the defender is still standing
	// there with a body in the way, so sliding through them would be the attacker walking into a
	// guard and out the other side.
	if (ATDCombatCharacter* Defender = Cast<ATDCombatCharacter>(HitActor))
	{
		const AActor* Avatar = GetAvatarActorFromActorInfo();
		if (Defender->IsBlocking() && Avatar && Defender->IsGuardFacing(Avatar->GetActorLocation()))
		{
			StopLunge();

			// Stamina instead of health, and the guard break falls out of the bar reaching zero
			// inside this call rather than being decided here. That keeps "what breaks a guard"
			// a property of the stamina economy, where drain and damage can be told apart, and
			// not a rule each attack has to restate.
			Defender->ApplyStaminaDamage(GetAttackStaminaDamage());

			// Logged before the blockstun it causes, so the trace reads cause then effect. It ran the
			// other way for one session and inverted the tell Docs/Debug-Instruments.md gives for a
			// break -- "BLOCKED with no BLOCKSTUN beside it" is read downward.
			TD_TIMING_LOG(TEXT("[%.3f] BLOCKED    %s by %s  staminaDamage=%.0f  remaining=%.1f"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
				*GetNameSafe(Avatar),
				*Defender->GetName(),
				GetAttackStaminaDamage(),
				Defender->GetStamina());

			// **Blockstun only if the guard survived, and the order is the reason this reads the
			// defender back instead of predicting.** ApplyStaminaDamage above may have broken the
			// guard, and a break already refuses every ability for longer than any blockstun -- so
			// applying both would stack two lockouts for one hit, and the shorter would expire
			// invisibly inside the longer. A broken guard is not a successful block.
			if (!Defender->IsGuardBroken())
			{
				Defender->EnterBlockstun(GetAttackBlockstunSeconds());
			}

			// The blocked spacing reset: same full centring as a clean hit, notably less ground
			// conceded. Applied whether or not this hit broke the guard -- the contact was blocked
			// either way, and one rule beats a special case nobody asked for. Zero spacing is none.
			ApplyKnockbackToTarget(Defender, /*bBlocked=*/true);
			return;
		}
	}

	// A viable target was struck, so the lunge is finished -- keyed to the hit rather than to the
	// damage landing. The two are the same instant on the server today, but they are different
	// events: the hit is detected here, while damage has to travel through effect application. Tying
	// movement to the slower of the two is what would eventually read as a slide.
	//
	// The standoff gate cannot cover this case. It *pauses* while a body is in the way and resumes
	// when one is not, so a target that dies mid-attack loses its capsule and the attacker slides
	// through the space it occupied. See UTDGameplayAbility::StopLunge.
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
		// bar -- the attribute set clamps, so the clamped result is the only truthful ledger entry.
		// This was the last resource change with no trace at all: BLOCKED prints the stamina
		// ledger, while a hit on health printed nothing between full and DEATH, so an unattended
		// run could count the silences but never audit the amounts (found 2026-08-15, when three
		// guard-down hits moved a defender 100 -> 55 and the log never said so).
		TD_TIMING_LOG(TEXT("[%.3f] DAMAGED    %s by %s  damage=%.0f  health=%.1f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			*GetNameSafe(HitActor),
			*GetNameSafe(GetAvatarActorFromActorInfo()),
			GetAttackDamage(),
			TargetASC->GetNumericAttribute(UTDAttributeSet::GetHealthAttribute()));
	}

	// The hit's effect on the victim beyond the bar: hitstun, then the spacing reset. After the
	// damage deliberately, so a killing blow resolves death first -- both calls no-op on the dead,
	// whose ragdoll is under physics no root motion source could move anyway. Both are inert at
	// their C++ defaults (0 hitstun, 0 spacing); the CDO is what arms them.
	if (ATDCombatCharacter* Victim = Cast<ATDCombatCharacter>(HitActor))
	{
		Victim->EnterHitstun(GetAttackHitstunSeconds());
		ApplyKnockbackToTarget(Victim, /*bBlocked=*/false);
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
	// destination sits on is planted, which is what makes "the same spot every time" a truth
	// rather than an aspiration.
	const FVector AttackerLoc = Avatar->GetActorLocation();
	FVector Facing = Avatar->GetActorForwardVector();
	Facing.Z = 0.0f;
	if (!Facing.Normalize())
	{
		return;
	}

	const FVector ToTarget = Target->GetActorLocation() - AttackerLoc;
	const float CurrentAlongCm = FVector::DotProduct(ToTarget, Facing);
	const float LateralCm = FVector::DotProduct(ToTarget, FVector::CrossProduct(FVector::UpVector, Facing));

	// **Never inward.** A contact beyond the authored spacing keeps its distance and is only
	// centred -- vacuum blocks are a known artifact class, and pulling a defender toward the sword
	// is not something either spacing means. Deleting this max() is the whole change if a pull-in
	// is ever wanted.
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
	// Where the blend starts, and which section was playing when it did. With
	// blendOutTriggerTime at -1 the natural end-blend cannot begin before
	// (length - blendTime), so a position well short of that means something else ended the
	// section -- and the section name is the only thing that can say what.
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
