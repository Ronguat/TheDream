// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Notifies/AnimNotify_ParryGesture.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "Combat/TDCombatCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

void UAnimNotify_ParryGesture::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	// **Ungated, unlike Release Window's edge logging.** This line is the only evidence that the
	// marker was ever placed -- a montage's notifies cannot be read back off the asset through the
	// toolset, so placement is verifiable at runtime and nowhere else. Gating it behind the timing
	// trace would mean the one observation that distinguishes "placed" from "forgotten" is missing
	// from exactly the logs taken when nobody suspected a problem. It fires once per parry.
	//
	// pos= is the montage playhead and is what the checker asserts against PARRY WINDOW's span:
	// the gesture must read *inside* the live window, or the character catches the blow after the
	// parry has already closed.
	{
		UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
		UAnimMontage* Montage = AnimInstance ? AnimInstance->GetCurrentActiveMontage() : nullptr;
		const FAnimMontageInstance* Instance = (AnimInstance && Montage) ? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
		const UWorld* World = MeshComp->GetWorld();

		UE_LOG(LogTDCombatTiming, Log, TEXT("[%.3f] PARRY GESTURE %s  pos=%.4f rate=%.3f"),
			World ? World->GetTimeSeconds() : -1.0f,
			*GetNameSafe(Owner),
			(AnimInstance && Montage) ? AnimInstance->Montage_GetPosition(Montage) : -1.0f,
			Instance ? Instance->GetPlayRate() : -1.0f);
	}

	// **The rate switch happens here rather than in GA_Parry, and that is the point.** A successful
	// parry ends the ability at the instant it catches -- 0 ms in, in the worst case -- so anything
	// waiting inside the ability would be gone before this marker arrived, and the recovery segment
	// would play at the window's rate. The designer ruled the authored recovery rate is *always*
	// the one used (2026-08-19), so the switch has to live somewhere that outlives the ability.
	//
	// The rate itself was derived at activation and parked on the character; nothing is recomputed
	// here, and no gameplay event is needed to carry it.
	if (ATDCombatCharacter* Character = Cast<ATDCombatCharacter>(Owner))
	{
		const float RecoveryRate = Character->GetPendingParryMontageRecoveryRate();
		UAnimInstance* AnimInstance = MeshComp->GetAnimInstance();
		UAnimMontage* Montage = AnimInstance ? AnimInstance->GetCurrentActiveMontage() : nullptr;

		// Guarded on the montage the marker actually sits in, not merely on there being one: this
		// class is placed on exactly one asset today, but a second would otherwise retime whatever
		// happened to be playing. Same hazard Release Window's payload exists to prevent.
		if (AnimInstance && Montage && Montage == Animation && RecoveryRate > 0.0f)
		{
			AnimInstance->Montage_SetPlayRate(Montage, RecoveryRate);

			UE_LOG(LogTDCombatTiming, Log, TEXT("[%.3f] PARRY RATE    %s  recoveryRate=%.3f"),
				MeshComp->GetWorld() ? MeshComp->GetWorld()->GetTimeSeconds() : -1.0f,
				*GetNameSafe(Owner), RecoveryRate);
		}
	}
}

#if WITH_EDITOR
FString UAnimNotify_ParryGesture::GetNotifyName_Implementation() const
{
	return TEXT("Parry Gesture");
}
#endif
