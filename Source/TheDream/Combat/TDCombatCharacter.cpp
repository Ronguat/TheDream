// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDCombatCharacter.h"
#include "Combat/Attributes/TDAttributeSet.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "TimerManager.h"

ATDCombatCharacter::ATDCombatCharacter()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);

	// Mixed: full effect replication to the owning client, minimal to everyone else.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UTDAttributeSet>(TEXT("AttributeSet"));

	// The pack's own Sword / Shield sockets, which hang off hand_r and hand_l and carry the
	// grip rotation and a non-uniform scale (the shield's is 0.25, 0.20, 0.30 -- the mesh is
	// authored several times too large and the socket is what corrects it). Attach here and
	// both props are right at identity; anything else means re-deriving what the pack knows.
	//
	// Deliberately NOT the weapon_r / weapon_l bones, which look like the obvious choice and
	// are worse twice over: they are absent from Epic's SKM_Manny_Simple entirely, and only
	// GDH clips animate them -- so under any Epic animation the props freeze at reference
	// pose. hand_r / hand_l are driven by every animation there is.
	//
	// Cosmetic only. Collision is off because the melee trace is UAbilityTask_MeleeTrace's
	// job -- a prop that could block or overlap would let the mesh quietly decide reach,
	// which is the thing spacing tests measure.
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	WeaponMesh->SetupAttachment(GetMesh(), TEXT("Sword"));
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ShieldMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShieldMesh"));
	ShieldMesh->SetupAttachment(GetMesh(), TEXT("Shield"));
	ShieldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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

	const float Now = World->GetTimeSeconds();
	bool bSuppressorActive = false;

	// While a suppressor is active the resume time keeps being pushed forward, which is what
	// makes each pause measure from when its cause *ends* without anyone tracking that. Taking
	// the max rather than assigning lets two overlap without the shorter cutting the longer short.
	if (StaminaRegenPausedTag.IsValid() && AbilitySystemComponent->HasMatchingGameplayTag(StaminaRegenPausedTag))
	{
		RegenSuppressedUntil = FMath::Max(RegenSuppressedUntil, Now + StaminaRegenPauseSeconds);
		bSuppressorActive = true;
	}

	// Set at the jump's launch and cleared on landing, so the pause spans the whole airborne
	// period plus its tail. Deliberately not driven by IsFalling(): walking off a ledge is not
	// an action and costs nothing.
	if (bJumpRegenPauseActive)
	{
		RegenSuppressedUntil = FMath::Max(RegenSuppressedUntil, Now + JumpRegenPauseSeconds);
		bSuppressorActive = true;
	}

	if (bSuppressorActive || Now < RegenSuppressedUntil || GetStamina() >= GetMaxStamina())
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

	return bActionRunning || bJumpRegenPauseActive || World->GetTimeSeconds() < RegenSuppressedUntil;
}

void ATDCombatCharacter::HandleStaminaChanged(const FOnAttributeChangeData& Data)
{
	if (!bExhausted)
	{
		if (Data.NewValue <= 0.0f)
		{
			EnterExhaustion();
		}
		return;
	}

	// Recovery ends exhaustion, not a clock. Regen is the only thing that can get here, which
	// is why it must keep running while exhausted -- see ExhaustedTag.
	if (Data.NewValue >= GetMaxStamina())
	{
		ExitExhaustion();
	}
}

void ATDCombatCharacter::EnterExhaustion()
{
	bExhausted = true;

	if (AbilitySystemComponent && ExhaustedTag.IsValid())
	{
		AbilitySystemComponent->AddLooseGameplayTag(ExhaustedTag);
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

void ATDCombatCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();

	// Hooked here rather than in Jump(), which only records the button press. A press that
	// never becomes a launch -- held against a ceiling, or pressed while already falling --
	// must not pause regen, or the pause would be charging for something that did not happen.
	bJumpRegenPauseActive = true;
}

void ATDCombatCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// Clearing the flag is the whole job: the last airborne tick already pushed
	// RegenSuppressedUntil to JumpRegenPauseSeconds ahead, so the tail measures from here.
	// Landing after walking off a ledge clears a flag that was never set, which is the point.
	bJumpRegenPauseActive = false;
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
	// stack a second copy of every DefaultEffect -- and for an infinite effect, that means a
	// permanently doubled magnitude rather than a visible one-off error.
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
		// Captured before the first swing, so it is the placed transform rather than wherever
		// root motion has since carried us.
		DebugAutoAttackHomeTransform = GetActorTransform();

		// Reset when the swing actually finishes rather than on a fixed delay: attack length
		// varies by tier, and a delay long enough for a charged attack would be most of the gap.
		AbilitySystemComponent->OnAbilityEnded.AddUObject(this, &ATDCombatCharacter::HandleDebugAutoAttackEnded);

		GetWorldTimerManager().SetTimer(
			DebugAutoAttackTimerHandle,
			this,
			&ATDCombatCharacter::DebugAutoAttackPress,
			DebugAutoAttackInterval,
			true,
			DebugAutoAttackInterval);
	}
}

void ATDCombatCharacter::ReturnToDebugAutoAttackHome()
{
	if (!bDebugAutoAttackResetPosition)
	{
		return;
	}

	// Teleported rather than swept: a swept move would be blocked by whatever the attacker has
	// walked into, which is exactly the state being undone.
	SetActorTransform(DebugAutoAttackHomeTransform, false, nullptr, ETeleportType::TeleportPhysics);

	// Root motion leaves velocity behind; without this the attacker slides away from home
	// immediately after being put back.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
}

void ATDCombatCharacter::HandleDebugAutoAttackEnded(const FAbilityEndedData& EndedData)
{
	// The interesting reset: it puts the attacker home for the whole gap between swings, so it
	// idles where it was placed instead of wherever its last lunge left it.
	//
	// Delayed, because the ability ends when its montage blends out rather than when the swing
	// looks finished -- resetting on that edge alone visibly snaps the attacker home in the
	// middle of its follow-through.
	if (!bDebugAutoAttackResetPosition)
	{
		return;
	}

	if (DebugAutoAttackResetDelaySeconds <= 0.0f)
	{
		ReturnToDebugAutoAttackHome();
		return;
	}

	GetWorldTimerManager().SetTimer(
		DebugAutoAttackResetTimerHandle,
		this,
		&ATDCombatCharacter::ReturnToDebugAutoAttackHome,
		DebugAutoAttackResetDelaySeconds,
		false);
}

void ATDCombatCharacter::DebugAutoAttackPress()
{
	// A pending delayed reset must not survive into the next swing, or it would snap the
	// attacker home mid-attack. The reset below covers the same ground immediately.
	GetWorldTimerManager().ClearTimer(DebugAutoAttackResetTimerHandle);

	// Belt and braces. The post-attack reset normally leaves nothing to do here, but an ability
	// that is cancelled or interrupted may never end cleanly, and this preserves the guarantee
	// that every swing starts from an identical transform.
	ReturnToDebugAutoAttackHome();

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
