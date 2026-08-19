// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Notifies/AnimNotify_ParryGesture.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
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

	FGameplayEventData Payload;
	Payload.EventTag = TDTags::Event_Parry_Gesture;
	Payload.Instigator = Owner;

	// Which animation carried the marker, for the reason Release Window passes the same thing: the
	// event goes to the whole ASC, so a second montage carrying this notify would otherwise be
	// indistinguishable from the parry's own. GA_Parry compares this against the montage it is
	// playing and ignores anything else.
	Payload.OptionalObject = Animation;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, TDTags::Event_Parry_Gesture, Payload);
}

#if WITH_EDITOR
FString UAnimNotify_ParryGesture::GetNotifyName_Implementation() const
{
	return TEXT("Parry Gesture");
}
#endif
