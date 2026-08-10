// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Notifies/AnimNotifyState_MeleeWindow.h"
#include "Combat/TDGameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	void SendMeleeWindowEvent(USkeletalMeshComponent* MeshComp, const FGameplayTag& EventTag)
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

		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, EventTag, Payload);
	}
}

void UAnimNotifyState_MeleeWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	SendMeleeWindowEvent(MeshComp, TDTags::Event_Melee_WindowBegin);
}

void UAnimNotifyState_MeleeWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	SendMeleeWindowEvent(MeshComp, TDTags::Event_Melee_WindowEnd);
}

#if WITH_EDITOR
FString UAnimNotifyState_MeleeWindow::GetNotifyName_Implementation() const
{
	return TEXT("Release Window");
}
#endif
