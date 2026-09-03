#include "TDInputTools.h"

#include "EnhancedInputSubsystems.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"

namespace
{
	UEnhancedInputLocalPlayerSubsystem* SubsystemFor(APlayerController* PlayerController)
	{
		ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
		return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	}

	/**
	 *  CreateSimulated fills the input device and timestamp a real event would carry, then the
	 *  local player's viewport is attached so anything reading Viewport off the args still resolves.
	 */
	FInputKeyEventArgs SimulatedArgs(APlayerController* PlayerController, const FKey& Key,
		EInputEvent Event, float AmountDepressed)
	{
		FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(Key, Event, AmountDepressed);
		if (const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr)
		{
			if (UGameViewportClient* ViewportClient = LocalPlayer->ViewportClient)
			{
				Args.Viewport = ViewportClient->Viewport;
			}
		}
		return Args;
	}
}

bool UTDInputTools::InjectAction(APlayerController* PlayerController, const UInputAction* Action, float Value)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = SubsystemFor(PlayerController);
	if (!Subsystem || !Action)
	{
		return false;
	}
	Subsystem->InjectInputForAction(Action, FInputActionValue(Value), {}, {});
	return true;
}

bool UTDInputTools::StartHold(APlayerController* PlayerController, const UInputAction* Action, float Value)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = SubsystemFor(PlayerController);
	if (!Subsystem || !Action)
	{
		return false;
	}
	Subsystem->StartContinuousInputInjectionForAction(Action, FInputActionValue(Value), {}, {});
	return true;
}

bool UTDInputTools::StopHold(APlayerController* PlayerController, const UInputAction* Action)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = SubsystemFor(PlayerController);
	if (!Subsystem || !Action)
	{
		return false;
	}
	Subsystem->StopContinuousInputInjectionForAction(Action);
	return true;
}

bool UTDInputTools::InputKey(APlayerController* PlayerController, FKey Key, bool bPressed)
{
	if (!PlayerController || !Key.IsValid())
	{
		return false;
	}
	const FInputKeyEventArgs Args = SimulatedArgs(PlayerController, Key,
		bPressed ? IE_Pressed : IE_Released, bPressed ? 1.0f : 0.0f);
	return PlayerController->InputKey(Args);
}

bool UTDInputTools::InputAxis(APlayerController* PlayerController, FKey Key, float Delta)
{
	if (!PlayerController || !Key.IsValid())
	{
		return false;
	}
	FInputKeyEventArgs Args = SimulatedArgs(PlayerController, Key, IE_Axis, Delta);
	Args.NumSamples = 1;
	return PlayerController->InputKey(Args);
}

bool UTDInputTools::InjectAxis2D(APlayerController* PlayerController, const UInputAction* Action, FVector2D Value)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = SubsystemFor(PlayerController);
	if (!Subsystem || !Action)
	{
		return false;
	}
	Subsystem->InjectInputForAction(Action, FInputActionValue(Value), {}, {});
	return true;
}

bool UTDInputTools::StartHoldAxis2D(APlayerController* PlayerController, const UInputAction* Action, FVector2D Value)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = SubsystemFor(PlayerController);
	if (!Subsystem || !Action)
	{
		return false;
	}
	Subsystem->StartContinuousInputInjectionForAction(Action, FInputActionValue(Value), {}, {});
	return true;
}

bool UTDInputTools::UpdateHoldAxis2D(APlayerController* PlayerController, const UInputAction* Action, FVector2D Value)
{
	UEnhancedInputLocalPlayerSubsystem* Subsystem = SubsystemFor(PlayerController);
	if (!Subsystem || !Action)
	{
		return false;
	}
	Subsystem->UpdateValueOfContinuousInputInjectionForAction(Action, FInputActionValue(Value));
	return true;
}
