// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TDDebugHUD.generated.h"

class ATDCombatCharacter;

/**
 *  Throwaway debug readout: health, stamina and active gameplay tags for every combatant.
 *
 *  A Canvas HUD rather than UMG: scaffolding for judging combat by feel during development,
 *  not the shipping health bar, so it is one file with no asset dependencies.
 *
 *  Toggle at runtime with the console variable TD.DebugHUD.
 */
UCLASS()
class ATDDebugHUD : public AHUD
{
	GENERATED_BODY()

public:

	virtual void DrawHUD() override;

protected:

	/** Width of the local player's bars, in pixels. Remote bars are scaled down. */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Layout", meta=(ClampMin="20.0"))
	float BarWidth = 240.0f;

	UPROPERTY(EditDefaultsOnly, Category="Debug|Layout", meta=(ClampMin="4.0"))
	float BarHeight = 16.0f;

	/**
	 *  Offset of the local player's panel, which is anchored bottom-centre. X nudges it sideways
	 *  from the centre and is normally 0; Y is the gap above the bottom edge.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Layout")
	FVector2D ScreenPadding = FVector2D(0.0f, 40.0f);

	/** Scale applied to panels drawn above other characters. */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Layout", meta=(ClampMin="0.1"))
	float RemoteScale = 0.6f;

	/** Height above a character's origin at which its panel is drawn, in cm. */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Layout")
	float RemoteHeightOffset = 130.0f;

	/** Characters further away than this are not drawn. */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Layout", meta=(ClampMin="0.0"))
	float MaxDrawDistance = 4000.0f;

	/** Draw panels above characters other than the local player. */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Content")
	bool bShowRemoteCharacters = true;

	/**
	 *  List each combatant's owned gameplay tags. Worth keeping on: "is the tag actually applied
	 *  right now" is a question the state slices need answered directly rather than inferred from
	 *  animation.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Content")
	bool bShowGameplayTags = true;

	/**
	 *  Show how far facing trails the camera, live and as sampled at the last facing lock. The
	 *  "lock" figure is the one that matters: an attack's wedge freezes at the commit checkpoint, so
	 *  that error is the angle between where the player was aiming and where the attack actually
	 *  points. Local player only -- it reads the controller, which a remote character does not have.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Content")
	bool bShowFacingError = true;

	UPROPERTY(EditDefaultsOnly, Category="Debug|Colours")
	FLinearColor HealthColor = FLinearColor(0.85f, 0.15f, 0.15f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Debug|Colours")
	FLinearColor StaminaColor = FLinearColor(0.9f, 0.75f, 0.1f, 1.0f);

	/**
	 *  Fill colour for the stamina bar while State.Exhausted is applied. Draining the colour out
	 *  says "this resource is not yours right now" without needing to be read, where spotting
	 *  State.Exhausted in the tag line does not.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Colours")
	FLinearColor ExhaustedStaminaColor = FLinearColor(0.45f, 0.45f, 0.45f, 1.0f);

	/**
	 *  Fill colour for the health bar while State.Dead is applied. An empty bar and a dead character
	 *  look identical at a glance, both at zero; this distinguishes "at 0 health" from "dead and
	 *  refusing everything".
	 */
	UPROPERTY(EditDefaultsOnly, Category="Debug|Colours")
	FLinearColor DeadHealthColor = FLinearColor(0.35f, 0.05f, 0.05f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Debug|Colours")
	FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f);

	UPROPERTY(EditDefaultsOnly, Category="Debug|Colours")
	FLinearColor TextColor = FLinearColor::White;

private:

	/** Draws one combatant's panel with its top-left corner at (X, Y). Returns height used. */
	float DrawCombatantPanel(ATDCombatCharacter* Character, float X, float Y, float Scale, bool bShowName);

	void DrawLabelledBar(const FString& Label, float Fraction, float Current, float Max, const FLinearColor& FillColor, float X, float Y, float Scale);
};
