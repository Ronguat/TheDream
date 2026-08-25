#pragma once

#include "CoreMinimal.h"
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
};
