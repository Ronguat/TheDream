// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Notifies/AnimNotifyState_MeleeWindow.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

namespace
{
	/**
	 *  Logs where the montage playhead actually is when a release window edge fires.
	 *
	 *  Gated at the top rather than at the log call so the playhead reads are skipped too;
	 *  this runs on every attack of every combatant.
	 */
	void LogWindowEdge(USkeletalMeshComponent* MeshComp, const TCHAR* Edge)
	{
		if (!TDShouldTraceCombatTiming())
		{
			return;
		}

		UAnimInstance* AnimInstance = MeshComp ? MeshComp->GetAnimInstance() : nullptr;
		UAnimMontage* Montage = AnimInstance ? AnimInstance->GetCurrentActiveMontage() : nullptr;
		const FAnimMontageInstance* Instance = (AnimInstance && Montage) ? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
		const UWorld* World = MeshComp ? MeshComp->GetWorld() : nullptr;

		UE_LOG(LogTDCombatTiming, Log, TEXT("[%.3f] RELEASE %s  %s pos=%.4f rate=%.3f"),
			World ? World->GetTimeSeconds() : -1.0f,
			Edge,
			*GetNameSafe(MeshComp ? MeshComp->GetOwner() : nullptr),
			(AnimInstance && Montage) ? AnimInstance->Montage_GetPosition(Montage) : -1.0f,
			Instance ? Instance->GetPlayRate() : -1.0f);
	}
}

namespace
{
	void SendMeleeWindowEvent(USkeletalMeshComponent* MeshComp, const FGameplayTag& EventTag, float WindowLength, const UAnimSequenceBase* SourceAnimation)
	{
		if (!MeshComp)
		{
			return;
		}

		AActor* Owner = MeshComp->GetOwner();
		if (!Owner)
		{
			return;
		}

		FGameplayEventData Payload;
		Payload.EventTag = EventTag;
		Payload.Instigator = Owner;

		// Which animation this window belongs to. The event is broadcast to the whole ASC, so
		// without it a second montage carrying this notify would open every listening trace --
		// including one belonging to an attack that is not the montage playing. Correct while
		// exactly one montage carries the notify, and silently wrong the moment a second does,
		// which is what Attack Swap creates. UAbilityTask_MeleeTrace compares this against the
		// montage its own ability is playing.
		Payload.OptionalObject = SourceAnimation;

		// The notify is the only thing that knows how long this window is -- a montage's
		// notifies cannot be read back off the asset. Passing it along means the ability
		// can stretch the window to the duration it wants without anyone maintaining a
		// second copy of the timeline.
		Payload.EventMagnitude = WindowLength;

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, EventTag, Payload);
	}
}

void UAnimNotifyState_MeleeWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	LogWindowEdge(MeshComp, TEXT("BEGIN"));

	SendMeleeWindowEvent(MeshComp, TDTags::Event_Melee_WindowBegin, TotalDuration, Animation);
}

void UAnimNotifyState_MeleeWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	LogWindowEdge(MeshComp, TEXT("END  "));

	SendMeleeWindowEvent(MeshComp, TDTags::Event_Melee_WindowEnd, 0.0f, Animation);
}

#if WITH_EDITOR
FString UAnimNotifyState_MeleeWindow::GetNotifyName_Implementation() const
{
	return TEXT("Release Window");
}
#endif
