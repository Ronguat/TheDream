// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/UI/TDDebugHUD.h"
#include "Combat/TDCombatCharacter.h"
#include "AbilitySystemComponent.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"

static TAutoConsoleVariable<int32> CVarTDDebugHUD(
	TEXT("TD.DebugHUD"),
	1,
	TEXT("Draw the combat debug readout: health, stamina and gameplay tags. 0 to hide."),
	ECVF_Cheat);

void ATDDebugHUD::DrawHUD()
{
	Super::DrawHUD();

	if (CVarTDDebugHUD.GetValueOnGameThread() == 0 || !Canvas)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// The local player, pinned to the corner where it is always readable.
	ATDCombatCharacter* LocalCharacter = Cast<ATDCombatCharacter>(GetOwningPawn());
	if (LocalCharacter)
	{
		DrawCombatantPanel(LocalCharacter, ScreenPadding.X, ScreenPadding.Y, 1.0f, false);
	}

	if (!bShowRemoteCharacters)
	{
		return;
	}

	const FVector ViewLocation = LocalCharacter ? LocalCharacter->GetActorLocation() : FVector::ZeroVector;

	for (TActorIterator<ATDCombatCharacter> It(World); It; ++It)
	{
		ATDCombatCharacter* Character = *It;
		if (!Character || Character == LocalCharacter)
		{
			continue;
		}

		if (MaxDrawDistance > 0.0f && FVector::Dist(ViewLocation, Character->GetActorLocation()) > MaxDrawDistance)
		{
			continue;
		}

		const FVector WorldAnchor = Character->GetActorLocation() + FVector(0.0f, 0.0f, RemoteHeightOffset);
		const FVector ScreenAnchor = Project(WorldAnchor);

		// Project returns a negative depth for anything behind the camera.
		if (ScreenAnchor.Z <= 0.0f)
		{
			continue;
		}

		const float PanelWidth = BarWidth * RemoteScale;
		DrawCombatantPanel(Character, ScreenAnchor.X - (PanelWidth * 0.5f), ScreenAnchor.Y, RemoteScale, true);
	}
}

float ATDDebugHUD::DrawCombatantPanel(ATDCombatCharacter* Character, float X, float Y, float Scale, bool bShowName)
{
	if (!Character)
	{
		return 0.0f;
	}

	const float LineHeight = (BarHeight + 6.0f) * Scale;
	float CursorY = Y;

	if (bShowName)
	{
		DrawText(Character->GetName(), TextColor, X, CursorY, nullptr, Scale);
		CursorY += LineHeight;
	}

	DrawLabelledBar(TEXT("HP"), Character->GetHealthPercent(), Character->GetHealth(), Character->GetMaxHealth(), HealthColor, X, CursorY, Scale);
	CursorY += LineHeight;

	DrawLabelledBar(TEXT("SP"), Character->GetStaminaPercent(), Character->GetStamina(), Character->GetMaxStamina(), StaminaColor, X, CursorY, Scale);
	CursorY += LineHeight;

	if (bShowGameplayTags)
	{
		if (UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent())
		{
			FGameplayTagContainer OwnedTags;
			ASC->GetOwnedGameplayTags(OwnedTags);

			if (!OwnedTags.IsEmpty())
			{
				DrawText(OwnedTags.ToStringSimple(), TextColor, X, CursorY, nullptr, Scale);
				CursorY += LineHeight;
			}
		}
	}

	return CursorY - Y;
}

void ATDDebugHUD::DrawLabelledBar(const FString& Label, float Fraction, float Current, float Max, const FLinearColor& FillColor, float X, float Y, float Scale)
{
	const float Width = BarWidth * Scale;
	const float Height = BarHeight * Scale;

	DrawRect(BackgroundColor, X, Y, Width, Height);
	DrawRect(FillColor, X, Y, Width * FMath::Clamp(Fraction, 0.0f, 1.0f), Height);

	const FString Text = FString::Printf(TEXT("%s %.0f / %.0f"), *Label, Current, Max);
	DrawText(Text, TextColor, X + (4.0f * Scale), Y, nullptr, Scale);
}
