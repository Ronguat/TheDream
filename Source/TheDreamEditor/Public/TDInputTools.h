#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDInputTools.generated.h"

class APlayerController;
class UInputAction;

/**
 *  Synthetic gameplay input for PIE, which no scripting surface reaches on its own.
 *
 *  Enhanced Input's injection API is BlueprintCallable, but it lives on a local player subsystem
 *  and Python exposes only engine and editor subsystem getters -- so the API is reachable and its
 *  handle is not. These wrappers close that gap and nothing else.
 *
 *  Two depths, not equivalent. InputKey enters at the key, where the game viewport enters, so key
 *  state, the mapping context and its modifiers, the action's triggers and the binding all run. The
 *  Inject/Hold functions enter at the action, bypassing the mapping -- for a value no key produces.
 */
UCLASS()
class THEDREAMEDITOR_API UTDInputTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** One press-and-release-worth of value on an action, delivered to the next input tick. */
	UFUNCTION(BlueprintCallable, Category="TheDream|Input")
	static bool InjectAction(APlayerController* PlayerController, const UInputAction* Action, float Value = 1.0f);

	/** Begins a held input that persists until StopHold, which is what a charge or a guard needs. */
	UFUNCTION(BlueprintCallable, Category="TheDream|Input")
	static bool StartHold(APlayerController* PlayerController, const UInputAction* Action, float Value = 1.0f);

	/** Ends a held input begun by StartHold. */
	UFUNCTION(BlueprintCallable, Category="TheDream|Input")
	static bool StopHold(APlayerController* PlayerController, const UInputAction* Action);

	/**
	 *  One key edge through APlayerController::InputKey, the call the game viewport makes.
	 *
	 *  Key state persists between the edges as a keyboard's does, so a hold is a press and a release
	 *  some frames later rather than a repeated press.
	 */
	UFUNCTION(BlueprintCallable, Category="TheDream|Input")
	static bool InputKey(APlayerController* PlayerController, FKey Key, bool bPressed);

	/** One axis sample on a key, for the mouse axes. */
	UFUNCTION(BlueprintCallable, Category="TheDream|Input")
	static bool InputAxis(APlayerController* PlayerController, FKey Key, float Delta);

	/** Action-level 2D injection, for a direction the movement keys cannot express. */
	UFUNCTION(BlueprintCallable, Category="TheDream|Input")
	static bool InjectAxis2D(APlayerController* PlayerController, const UInputAction* Action, FVector2D Value);

	UFUNCTION(BlueprintCallable, Category="TheDream|Input")
	static bool StartHoldAxis2D(APlayerController* PlayerController, const UInputAction* Action, FVector2D Value);

	/** Changes a StartHoldAxis2D already running, without restarting it. */
	UFUNCTION(BlueprintCallable, Category="TheDream|Input")
	static bool UpdateHoldAxis2D(APlayerController* PlayerController, const UInputAction* Action, FVector2D Value);
};
