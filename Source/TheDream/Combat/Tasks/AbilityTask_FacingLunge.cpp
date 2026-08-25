// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/Tasks/AbilityTask_FacingLunge.h"
#include "Combat/Tasks/TDRootMotionSource_FacingForce.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemLog.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

UAbilityTask_FacingLunge::UAbilityTask_FacingLunge(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Strength = 0.0f;
	Duration = 0.0f;
	StrengthOverTime = nullptr;
	StandoffCm = 0.0f;
	YawOffsetDegrees = 0.0f;
	bEnableGravity = false;
}

UAbilityTask_FacingLunge* UAbilityTask_FacingLunge::ApplyFacingLunge(
	UGameplayAbility* OwningAbility,
	FName TaskInstanceName,
	float DistanceCm,
	float DurationSeconds,
	UCurveFloat* StrengthCurve,
	float StandoffCm,
	float YawOffsetDegrees,
	ERootMotionFinishVelocityMode VelocityOnFinishMode,
	FVector SetVelocityOnFinish,
	float ClampVelocityOnFinish,
	bool bEnableGravity,
	float TurnBodyToTravelRate)
{
	UAbilitySystemGlobals::NonShipping_ApplyGlobalAbilityScaler_Duration(DurationSeconds);

	UAbilityTask_FacingLunge* MyTask = NewAbilityTask<UAbilityTask_FacingLunge>(OwningAbility, TaskInstanceName);

	MyTask->ForceName = TaskInstanceName;
	// Distance in, speed stored. Constant force is a velocity, so this divides out exactly --
	// and it is why a strength curve whose mean is not 1 silently changes the distance.
	MyTask->Strength = DurationSeconds > 0.0f ? (DistanceCm / DurationSeconds) : 0.0f;
	MyTask->Duration = DurationSeconds;
	MyTask->StrengthOverTime = StrengthCurve;
	MyTask->StandoffCm = StandoffCm;
	MyTask->YawOffsetDegrees = YawOffsetDegrees;
	MyTask->FinishVelocityMode = VelocityOnFinishMode;
	MyTask->FinishSetVelocity = SetVelocityOnFinish;
	MyTask->FinishClampVelocity = ClampVelocityOnFinish;
	MyTask->bEnableGravity = bEnableGravity;
	// **Before SharedInitAndApply, which builds the source and is called from here.** Anything
	// set on the task after this line never reaches the force that is already applied.
	if (TurnBodyToTravelRate > 0.0f)
	{
		if (const AActor* Avatar = OwningAbility ? OwningAbility->GetAvatarActorFromActorInfo() : nullptr)
		{
			const float TravelYaw = Avatar->GetActorRotation().Yaw + YawOffsetDegrees;
			MyTask->SetTurnBodyToTravel(
				FRotator(0.0f, TravelYaw, 0.0f).Vector(), TravelYaw, TurnBodyToTravelRate);
		}
	}

	MyTask->SharedInitAndApply();

	return MyTask;
}

void UAbilityTask_FacingLunge::SharedInitAndApply()
{
	UAbilitySystemComponent* ASC = AbilitySystemComponent.Get();
	if (ASC && ASC->AbilityActorInfo->MovementComponent.IsValid())
	{
		MovementComponent = Cast<UCharacterMovementComponent>(ASC->AbilityActorInfo->MovementComponent.Get());
		StartTime = GetWorld()->GetTimeSeconds();
		EndTime = StartTime + Duration;

		if (MovementComponent.IsValid())
		{
			ForceName = ForceName.IsNone() ? FName("AbilityTaskFacingLunge") : ForceName;

			TSharedPtr<FTDRootMotionSource_FacingForce> LungeForce = MakeShared<FTDRootMotionSource_FacingForce>();
			LungeForce->InstanceName = ForceName;
			// Override rather than Additive: the lunge *is* the attack's movement, and adding it
			// to whatever else is running would make the authored distance a lie by that amount.
			LungeForce->AccumulateMode = ERootMotionAccumulateMode::Override;
			LungeForce->Priority = 5;
			LungeForce->Strength = Strength;
			LungeForce->Duration = Duration;
			LungeForce->StrengthOverTime = StrengthOverTime;
			LungeForce->StandoffCm = StandoffCm;
			LungeForce->YawOffsetDegrees = YawOffsetDegrees;
			LungeForce->FixedDirection = FixedDirection;
			LungeForce->FinishVelocityParams.Mode = FinishVelocityMode;
			LungeForce->FinishVelocityParams.SetVelocity = FinishSetVelocity;
			LungeForce->FinishVelocityParams.ClampVelocity = FinishClampVelocity;
			if (bEnableGravity)
			{
				LungeForce->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
			}

			RootMotionSourceID = MovementComponent->ApplyRootMotionSource(LungeForce);
		}
	}
	else
	{
		ABILITY_LOG(Warning, TEXT("UAbilityTask_FacingLunge called in Ability %s with null MovementComponent; Task Instance Name %s."),
			Ability ? *Ability->GetName() : TEXT("NULL"),
			*InstanceName.ToString());
	}
}

void UAbilityTask_FacingLunge::SetTurnBodyToTravel(const FVector& InDirection, float InTravelYaw, float InTurnRateDegrees)
{
	FixedDirection = InDirection;
	TravelYaw = InTravelYaw;
	TurnRateDegrees = InTurnRateDegrees;
	bTurnBodyToTravel = !InDirection.IsNearlyZero() && InTurnRateDegrees > 0.0f;
}

void UAbilityTask_FacingLunge::TickTask(float DeltaTime)
{
	if (bIsFinished)
	{
		return;
	}

	Super::TickTask(DeltaTime);

	AActor* MyActor = GetAvatarActor();
	if (!MyActor)
	{
		bIsFinished = true;
		EndTask();
		return;
	}

	// Turned rather than snapped, and the travel does not follow: FixedDirection took the
	// direction at activation, so the body is free to sweep round to it. The motion is what an
	// opponent reads the roll's heading from -- a snap gives them a pose to re-read instead.
	// Written here rather than through the facing update, which the knockdown's own lock
	// short-circuits for the whole get-up.
	if (bTurnBodyToTravel && !bIsSimulating)
	{
		FRotator Rotation = MyActor->GetActorRotation();
		Rotation.Yaw = FMath::FixedTurn(Rotation.Yaw, TravelYaw, TurnRateDegrees * DeltaTime);
		MyActor->SetActorRotation(Rotation);
	}

	if (Duration >= 0.0f && HasTimedOut())
	{
		bIsFinished = true;
		if (!bIsSimulating)
		{
			MyActor->ForceNetUpdate();
			if (ShouldBroadcastAbilityTaskDelegates())
			{
				OnFinish.Broadcast();
			}
			EndTask();
		}
	}
}

void UAbilityTask_FacingLunge::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAbilityTask_FacingLunge, Strength);
	DOREPLIFETIME(UAbilityTask_FacingLunge, Duration);
	DOREPLIFETIME(UAbilityTask_FacingLunge, StrengthOverTime);
	DOREPLIFETIME(UAbilityTask_FacingLunge, StandoffCm);
	DOREPLIFETIME(UAbilityTask_FacingLunge, YawOffsetDegrees);
	DOREPLIFETIME(UAbilityTask_FacingLunge, bEnableGravity);
}

void UAbilityTask_FacingLunge::PreDestroyFromReplication()
{
	bIsFinished = true;
	EndTask();
}

void UAbilityTask_FacingLunge::OnDestroy(bool AbilityIsEnding)
{
	// Removing the source here is what guarantees a cancelled attack leaves no residual motion.
	// Every exit path -- finished, interrupted, cancelled, death -- destroys the task.
	if (MovementComponent.IsValid())
	{
		MovementComponent->RemoveRootMotionSourceByID(RootMotionSourceID);
	}

	Super::OnDestroy(AbilityIsEnding);
}
