// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDCombatCharacter.h"
#include "Combat/Attributes/TDAttributeSet.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "GameplayEffect.h"
#include "TimerManager.h"

ATDCombatCharacter::ATDCombatCharacter()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed: full effect replication to the owning client, minimal to everyone else.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UTDAttributeSet>(TEXT("AttributeSet"));

	// Stamina regen runs per frame rather than on a timer, so the bar moves smoothly
	// instead of stepping.
	PrimaryActorTick.bCanEverTick = true;
}

void ATDCombatCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Attributes are authority-only state; clients see regen by replication.
	if (HasAuthority())
	{
		TickStaminaRegen(DeltaSeconds);
	}
}

void ATDCombatCharacter::TickStaminaRegen(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !AbilitySystemComponent || StaminaRegenPerSecond <= 0.0f)
	{
		return;
	}

	// While an action is running the resume time keeps being pushed forward, which is what
	// makes the pause measure from when the action *ends* without anyone tracking that.
	if (StaminaRegenPausedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StaminaRegenPausedTag))
	{
		RegenSuppressedUntil = World->GetTimeSeconds() + StaminaRegenPauseSeconds;
		return;
	}

	if (World->GetTimeSeconds() < RegenSuppressedUntil || GetStamina() >= GetMaxStamina())
	{
		return;
	}

	AbilitySystemComponent->ApplyModToAttribute(
		UTDAttributeSet::GetStaminaAttribute(),
		EGameplayModOp::Additive,
		StaminaRegenPerSecond * DeltaSeconds);
}

bool ATDCombatCharacter::IsStaminaRegenPaused() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const bool bActionRunning = AbilitySystemComponent && StaminaRegenPausedTag.IsValid()
		&& AbilitySystemComponent->HasMatchingGameplayTag(StaminaRegenPausedTag);

	return bActionRunning || World->GetTimeSeconds() < RegenSuppressedUntil;
}

void ATDCombatCharacter::HandleStaminaChanged(const FOnAttributeChangeData& Data)
{
	if (!bExhausted && Data.NewValue <= 0.0f)
	{
		EnterExhaustion();
	}
}

void ATDCombatCharacter::EnterExhaustion()
{
	bExhausted = true;

	if (AbilitySystemComponent && ExhaustedTag.IsValid())
	{
		AbilitySystemComponent->AddLooseGameplayTag(ExhaustedTag);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ExhaustionTimerHandle,
			this,
			&ATDCombatCharacter::ExitExhaustion,
			FMath::Max(ExhaustionSeconds, 0.01f),
			false);
	}
}

void ATDCombatCharacter::ExitExhaustion()
{
	if (!bExhausted)
	{
		return;
	}
	bExhausted = false;

	if (AbilitySystemComponent && ExhaustedTag.IsValid())
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(ExhaustedTag);
	}
}

void ATDCombatCharacter::Jump()
{
	// Deliberately silent. Exhaustion is communicated by the tag and the empty bar; a
	// failed jump that plays nothing reads as the lockout it is.
	if (bExhausted)
	{
		return;
	}

	Super::Jump();
}

UAbilitySystemComponent* ATDCombatCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ATDCombatCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Covers characters that are never possessed, such as a placed training dummy.
	InitialiseAbilitySystem();
}

void ATDCombatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// A possessed pawn needs the actor info rebound to its new owner.
	bAbilitySystemInitialised = false;
	InitialiseAbilitySystem();
}

void ATDCombatCharacter::InitialiseAbilitySystem()
{
	if (bAbilitySystemInitialised || !AbilitySystemComponent)
	{
		return;
	}

	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	bAbilitySystemInitialised = true;

	// Attributes and abilities are authority-only state; clients receive them by replication.
	if (!HasAuthority())
	{
		return;
	}

	// Actor info is rebound on every possession, but seeding must happen once. A pawn
	// possessed after BeginPlay would otherwise be granted every ability a second time and
	// stack a second copy of every DefaultEffect -- and for an infinite regen effect, that
	// means permanently doubled regen rather than a visible one-off error.
	if (bDefaultsApplied)
	{
		return;
	}
	bDefaultsApplied = true;

	AbilitySystemComponent->SetNumericAttributeBase(UTDAttributeSet::GetMaxHealthAttribute(), StartingMaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), StartingMaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(UTDAttributeSet::GetMaxStaminaAttribute(), StartingMaxStamina);
	AbilitySystemComponent->SetNumericAttributeBase(UTDAttributeSet::GetStaminaAttribute(), StartingMaxStamina);

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
	{
		if (AbilityClass)
		{
			// Input is matched against the ability's InputTag at press time, so the spec
			// needs no input ID of its own.
			AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	}

	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	Context.AddSourceObject(this);

	for (const TSubclassOf<UGameplayEffect>& EffectClass : DefaultEffects)
	{
		if (!EffectClass)
		{
			continue;
		}

		const FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(EffectClass, 1, Context);
		if (SpecHandle.IsValid())
		{
			AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}

	// Bound after seeding, so the initial fill to full does not read as a change to zero.
	AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetStaminaAttribute())
		.AddUObject(this, &ATDCombatCharacter::HandleStaminaChanged);

	if (bDebugAutoAttack && DebugAutoAttackInputTag.IsValid())
	{
		GetWorldTimerManager().SetTimer(
			DebugAutoAttackTimerHandle,
			this,
			&ATDCombatCharacter::DebugAutoAttackPress,
			DebugAutoAttackInterval,
			true,
			DebugAutoAttackInterval);
	}
}

void ATDCombatCharacter::DebugAutoAttackPress()
{
	OnAbilityInputPressed(DebugAutoAttackInputTag);

	if (DebugAutoAttackHoldSeconds <= 0.0f)
	{
		DebugAutoAttackRelease();
		return;
	}

	// Held rather than tapped, because how long the button stays down is what selects the
	// tier -- releasing immediately would make the dummy incapable of anything but a light.
	GetWorldTimerManager().SetTimer(
		DebugAutoAttackReleaseTimerHandle,
		this,
		&ATDCombatCharacter::DebugAutoAttackRelease,
		DebugAutoAttackHoldSeconds,
		false);
}

void ATDCombatCharacter::DebugAutoAttackRelease()
{
	OnAbilityInputReleased(DebugAutoAttackInputTag);
}

void ATDCombatCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	for (const TPair<TObjectPtr<UInputAction>, FGameplayTag>& Binding : AbilityInputActions)
	{
		if (!Binding.Key || !Binding.Value.IsValid())
		{
			continue;
		}

		EnhancedInput->BindAction(Binding.Key, ETriggerEvent::Started, this, &ATDCombatCharacter::OnAbilityInputPressed, Binding.Value);
		EnhancedInput->BindAction(Binding.Key, ETriggerEvent::Completed, this, &ATDCombatCharacter::OnAbilityInputReleased, Binding.Value);
	}
}

void ATDCombatCharacter::GatherAbilitiesForInput(const FGameplayTag& InputTag, TArray<FGameplayAbilitySpecHandle>& OutHandles) const
{
	if (!AbilitySystemComponent || !InputTag.IsValid())
	{
		return;
	}

	// Collect handles rather than acting inside the loop: activating an ability can
	// modify the spec list, and the specs would move underneath the iterator.
	for (const FGameplayAbilitySpec& Spec : AbilitySystemComponent->GetActivatableAbilities())
	{
		const UTDGameplayAbility* Ability = Cast<UTDGameplayAbility>(Spec.Ability);
		if (Ability && Ability->InputTag.MatchesTagExact(InputTag))
		{
			OutHandles.Add(Spec.Handle);
		}
	}
}

void ATDCombatCharacter::OnAbilityInputPressed(FGameplayTag InputTag)
{
	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		if (FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromHandle(Handle))
		{
			// Marks the spec as held and forwards the press to any live instance. This is
			// the state WaitInputRelease reads, so holds keep working.
			AbilitySystemComponent->AbilitySpecInputPressed(*Spec);

			if (!Spec->IsActive())
			{
				AbilitySystemComponent->TryActivateAbility(Handle);
			}
		}
	}
}

void ATDCombatCharacter::OnAbilityInputReleased(FGameplayTag InputTag)
{
	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		if (FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromHandle(Handle))
		{
			AbilitySystemComponent->AbilitySpecInputReleased(*Spec);
		}
	}
}

float ATDCombatCharacter::GetHealth() const
{
	return AttributeSet ? AttributeSet->GetHealth() : 0.0f;
}

float ATDCombatCharacter::GetMaxHealth() const
{
	return AttributeSet ? AttributeSet->GetMaxHealth() : 0.0f;
}

float ATDCombatCharacter::GetStamina() const
{
	return AttributeSet ? AttributeSet->GetStamina() : 0.0f;
}

float ATDCombatCharacter::GetMaxStamina() const
{
	return AttributeSet ? AttributeSet->GetMaxStamina() : 0.0f;
}

float ATDCombatCharacter::GetHealthPercent() const
{
	const float Max = GetMaxHealth();
	return (Max > 0.0f) ? GetHealth() / Max : 0.0f;
}

float ATDCombatCharacter::GetStaminaPercent() const
{
	const float Max = GetMaxStamina();
	return (Max > 0.0f) ? GetStamina() / Max : 0.0f;
}
