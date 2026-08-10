// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/TheDreamCharacter.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "Engine/TimerHandle.h"
#include "TDCombatCharacter.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UGameplayEffect;
class UInputAction;
class UTDAttributeSet;
struct FAbilityEndedData;

/**
 *  Base class for anything that can fight: the player and the training dummy alike.
 *
 *  Inherits locomotion and the third person camera from ATheDreamCharacter and adds
 *  the Ability System Component plus the core combat attributes on top. Abilities are
 *  granted from DefaultAbilities, so a Blueprint subclass decides what it can do
 *  without any graph wiring. Leaving that list empty produces a valid damage target
 *  that cannot act, which is exactly what the training dummy needs.
 */
UCLASS(abstract)
class ATDCombatCharacter : public ATheDreamCharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:

	ATDCombatCharacter();

	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetMaxStamina() const;

	/** Health as a 0-1 fraction, for health bars and debug readouts. */
	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetHealthPercent() const;

	/** Stamina as a 0-1 fraction, for stamina bars and debug readouts. */
	UFUNCTION(BlueprintPure, Category="Combat|Attributes")
	float GetStaminaPercent() const;

	/** True while stamina regen is suppressed, whether by an action or the tail after one. */
	UFUNCTION(BlueprintPure, Category="Combat|Stamina")
	bool IsStaminaRegenPaused() const;

	UFUNCTION(BlueprintPure, Category="Combat|Stamina")
	bool IsExhausted() const { return bExhausted; }

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Blocked while exhausted, per the design: no defensive actions and no jump. */
	virtual void Jump() override;

	/** The actual launch, as opposed to Jump() which only records the press. Starts the regen pause. */
	virtual void OnJumped_Implementation() override;

	/** Ends the jump's regen pause, leaving JumpRegenPauseSeconds of tail behind it. */
	virtual void Landed(const FHitResult& Hit) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	/** Granted on spawn. Empty means this character cannot act (training dummy). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/**
	 *  Applied to self on spawn and never removed -- the always-on effects.
	 *
	 *  Stamina regen is deliberately *not* here. It was an infinite periodic effect until
	 *  2026-08-10 and is now orchestrated in C++ below, because the economy is a small state
	 *  machine -- regen, a pause that outlives the action causing it, and a timed exhaustion
	 *  lockout -- and expressing that across effects needs tag components that cannot be
	 *  scripted in UE 5.8. Currently empty; kept for effects that genuinely are always-on.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Abilities")
	TArray<TSubclassOf<UGameplayEffect>> DefaultEffects;

	/**
	 *  Which input action drives which input tag, e.g. IA_LightAttack -> InputTag.Attack.
	 *
	 *  Abilities are matched by tag rather than by an integer ID, so granting a new
	 *  ability to a button is a content change rather than a C++ enum edit and rebuild.
	 *
	 *  Mapped actions must use a Down trigger (or no trigger at all), never Pressed --
	 *  a Pressed trigger completes on the frame after the press, while the button is
	 *  still held, so the release edge would be lost.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input")
	TMap<TObjectPtr<UInputAction>, FGameplayTag> AbilityInputActions;

	/**
	 *  Stamina regained per second, while regen is running at all.
	 *
	 *  Driven here rather than by a periodic GameplayEffect because the economy is a small
	 *  state machine -- regen, a pause that outlives the action causing it, and an
	 *  exhaustion lockout -- and expressing that across effects means tag components that
	 *  cannot be scripted. Attributes and tags are still GAS; only the orchestration is here.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float StaminaRegenPerSecond = 25.0f;

	/**
	 *  How long regen stays suppressed *after* the last defensive action ends.
	 *
	 *  Measured from the end, not the start, so it is a genuine tax on acting rather than
	 *  something a long action absorbs for free.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float StaminaRegenPauseSeconds = 1.0f;

	/**
	 *  How long regen stays suppressed after *landing* from a jump.
	 *
	 *  Jumping costs no stamina but is not free: regen is suppressed from the moment of the
	 *  jump until this long after landing, so height and airtime are paid for in recovery
	 *  rather than in bar. Separate from StaminaRegenPauseSeconds because a jump is a weaker
	 *  commitment than a defensive action and should not be taxed as heavily.
	 *
	 *  Keyed to the jump *action*, never to being airborne -- walking off a ledge is not
	 *  something you did, and costs nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float JumpRegenPauseSeconds = 0.5f;

	/**
	 *  Present while a defensive action is running, via that ability's owned tags.
	 *
	 *  Regen watches for this rather than each ability telling it to stop, so an ability
	 *  that is cancelled or interrupted cannot leave regen suppressed forever.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina")
	FGameplayTag StaminaRegenPausedTag;

	/**
	 *  Applied when stamina reaches zero, and what defensive abilities block on.
	 *
	 *  **Cleared when stamina reaches Max again, not on a timer.** Recovery is the thing that
	 *  ends exhaustion, so the punishment scales with how empty you ran yourself rather than
	 *  being a flat sentence -- and there is no second number that can disagree with the bar.
	 *
	 *  Regen deliberately continues while exhausted -- exhaustion is a lockout on acting,
	 *  not on recovering. That is now load-bearing rather than merely humane: regen is the
	 *  only thing that can end it, so suppressing regen here would make it permanent.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina")
	FGameplayTag ExhaustedTag;

	/** Starting and maximum health. Characters spawn at full. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Attributes", meta=(ClampMin="1.0"))
	float StartingMaxHealth = 100.0f;

	/** Starting and maximum stamina. Per the design this is 100 for everyone, for now. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Attributes", meta=(ClampMin="1.0"))
	float StartingMaxStamina = 100.0f;

	/**
	 *  Debug only: throw DebugAttackInputTag on a loop, so the dummy can be defended against.
	 *
	 *  Defensive work is unjudgeable against a target that never attacks -- i-frames, block
	 *  coverage and parry windows all need something incoming to be measured against. This
	 *  is deliberately the crudest thing that produces one, and is off by default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugAutoAttack = false;

	/** Input the auto-attack presses, normally InputTag.Attack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	FGameplayTag DebugAutoAttackInputTag;

	/** Seconds between one auto-attack starting and the next. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.1"))
	float DebugAutoAttackInterval = 3.0f;

	/**
	 *  How long the auto-attack holds the button, which selects the tier it throws.
	 *
	 *  The hold thresholds live on GA_Attack's Branches, so this is how you aim the dummy
	 *  at a light, a heavy or a charged: 0.1 for light, 0.3 for heavy, 0.8 for charged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugAutoAttackHoldSeconds = 0.1f;

	/**
	 *  Snap the auto-attacker back to its spawn transform. Debug only.
	 *
	 *  Attack montages carry root motion, so an attacker on a loop walks itself across the
	 *  level -- ours reached the edge of the map. Resetting is deliberately preferred over
	 *  zeroing AnimRootMotionTranslationScale: suppressing the lunge would shorten the
	 *  dummy's effective reach, and reach is exactly what spacing tests measure.
	 *
	 *  Fires when the swing *ends*, so the attacker spends the gap between attacks sitting
	 *  where it was placed rather than wherever its last lunge left it -- which is what makes
	 *  it pleasant to stand in front of. It also fires before each swing, which is normally a
	 *  no-op but covers an ability that was cancelled and so never ended cleanly; between them
	 *  every attack is guaranteed to start from an identical transform.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	bool bDebugAutoAttackResetPosition = true;

	/**
	 *  Extra delay after the attack ability ends before the reset fires. Debug only.
	 *
	 *  The ability ends when its montage blends out, which is noticeably earlier than the
	 *  swing looks finished, so resetting on that edge alone snaps the attacker home
	 *  mid-follow-through. This waits for the animation to actually read as over.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugAutoAttackResetDelaySeconds = 0.35f;

public:

	/**
	 *  Free-form line drawn under this character's bars by ATDDebugHUD. Debug only.
	 *
	 *  For per-activation detail that is not worth a gameplay tag -- which way a dodge
	 *  resolved, which parry window is open. Tags answer "is this state on"; this answers
	 *  "with what values", without every variation needing a tag of its own. Set it when an
	 *  ability starts and clear it when the ability ends, or it will outlive what it
	 *  describes.
	 */
	UPROPERTY(Transient, BlueprintReadWrite, Category="Combat|Debug")
	FString DebugStatusLine;

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UTDAttributeSet> AttributeSet;

private:

	/** Binds the actor info, seeds the attributes and grants DefaultAbilities. Safe to call twice. */
	void InitialiseAbilitySystem();

	void OnAbilityInputPressed(FGameplayTag InputTag);
	void OnAbilityInputReleased(FGameplayTag InputTag);

	/** Handles of granted abilities whose InputTag matches, in activation order. */
	void GatherAbilitiesForInput(const FGameplayTag& InputTag, TArray<FGameplayAbilitySpecHandle>& OutHandles) const;

	/** Presses the debug attack input, then releases it DebugAutoAttackHoldSeconds later. */
	void DebugAutoAttackPress();
	void DebugAutoAttackRelease();

	/** Teleports back to DebugAutoAttackHomeTransform and kills leftover velocity. No-op if disabled. */
	void ReturnToDebugAutoAttackHome();

	/** Bound to the ASC so the attacker returns home the moment a swing finishes. */
	void HandleDebugAutoAttackEnded(const FAbilityEndedData& EndedData);

	/** Adds StaminaRegenPerSecond * delta, unless suppressed or already full. */
	void TickStaminaRegen(float DeltaSeconds);

	/** Applies ExhaustedTag. Removed only once stamina is back to Max -- there is no timer. */
	void EnterExhaustion();
	void ExitExhaustion();

	void HandleStaminaChanged(const struct FOnAttributeChangeData& Data);

	/** World time before which regen stays suppressed. Pushed out while a suppressor is active. */
	float RegenSuppressedUntil = 0.0f;

	/** True between an actual jump launch and landing. Never set by merely falling. */
	bool bJumpRegenPauseActive = false;

	bool bExhausted = false;

	bool bAbilitySystemInitialised = false;

	/** Attributes, abilities and effects are seeded once, even though actor info is not. */
	bool bDefaultsApplied = false;

	FTimerHandle DebugAutoAttackTimerHandle;
	FTimerHandle DebugAutoAttackReleaseTimerHandle;
	FTimerHandle DebugAutoAttackResetTimerHandle;

	/** Where the auto-attacker started, captured once, so each swing begins from the same spot. */
	FTransform DebugAutoAttackHomeTransform;
};
