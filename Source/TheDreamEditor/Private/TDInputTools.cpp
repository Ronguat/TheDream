#include "TDInputTools.h"

#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"

namespace
{
	UEnhancedInputLocalPlayerSubsystem* SubsystemFor(APlayerController* PlayerController)
	{
		ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
		return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
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
