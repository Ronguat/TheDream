// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/TDCombatCharacter.h"
#include "Combat/Attributes/TDAttributeSet.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "Combat/TDCombatDebug.h"
#include "Combat/TDGameplayTags.h"
#include "Core/TDPlayerState.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ATDCombatCharacter::ATDCombatCharacter()
{
	// The fallback pair, used only when this character has no PlayerState -- the training dummy.
	// A player builds these too and then ignores them in favour of its PlayerState's; see the
	// header. The subobject *names* are unchanged from when this was the only ASC, so the two
	// character Blueprints keep resolving their component templates.
	OwnedAbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	OwnedAbilitySystemComponent->SetIsReplicated(true);

	// Mixed: full effect replication to the owning client, minimal to everyone else.
	OwnedAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	OwnedAttributeSet = CreateDefaultSubobject<UTDAttributeSet>(TEXT("AttributeSet"));

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

	// Not authority-gated: a buffered press is local input waiting to be spent, and it is
	// spent through the same path a live press takes.
	TickInputBuffer();
}

void ATDCombatCharacter::TickStaminaRegen(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !AbilitySystem || StaminaRegenPerSecond <= 0.0f)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	bool bSuppressorActive = false;

	// While a suppressor is active the resume time keeps being pushed forward, which is what
	// makes each pause measure from when its cause *ends* without anyone tracking that. Taking
	// the max rather than assigning lets two overlap without the shorter cutting the longer short.
	if (StaminaRegenPausedTag.IsValid() && AbilitySystem->HasMatchingGameplayTag(StaminaRegenPausedTag))
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

	AbilitySystem->ApplyModToAttribute(
		UTDAttributeSet::GetStaminaAttribute(),
		EGameplayModOp::Additive,
		StaminaRegenPerSecond * DeltaSeconds);
}

bool ATDCombatCharacter::IsIdle() const
{
	if (!Super::IsIdle())
	{
		return false;
	}

	// A press that was refused and stored is still a press. The player has asked for something
	// and is waiting on it, which is not idling.
	if (BufferedInput.IsSet())
	{
		return false;
	}

	if (!AbilitySystem)
	{
		return true;
	}

	// Any active ability at all, rather than a list of state tags. Attacking and dodging are
	// covered today; block, parry and every stun state are covered without editing this.
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		if (Spec.IsActive())
		{
			return false;
		}
	}

	return true;
}

bool ATDCombatCharacter::IsStaminaRegenPaused() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const bool bActionRunning = AbilitySystem && StaminaRegenPausedTag.IsValid()
		&& AbilitySystem->HasMatchingGameplayTag(StaminaRegenPausedTag);

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

void ATDCombatCharacter::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (bDead || Data.NewValue > 0.0f)
	{
		return;
	}

	EnterDeath();
}

void ATDCombatCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// To everyone, not just the owner: a simulated proxy has to know an opponent is dead or
	// exhausted, because that is what its own ragdoll and greyed bar are drawn from.
	DOREPLIFETIME(ATDCombatCharacter, bDead);
	DOREPLIFETIME(ATDCombatCharacter, bExhausted);
}

void ATDCombatCharacter::EnterDeath()
{
	if (bDead)
	{
		return;
	}
	bDead = true;

	TD_TIMING_LOG(TEXT("[%.3f] DEATH      %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetName());

	// Server-only, and deliberately outside ApplyDeathState. Cancelling abilities is an
	// authority decision that replicates through GAS on its own; running it again from a
	// client's OnRep would cancel that client's *predicted* copies out from under a
	// correction that may never come.
	if (AbilitySystem)
	{
		// The tag alone only refuses *new* activations, which is exhaustion's rule and is
		// visibly wrong here: a killing blow landing mid-swing would otherwise leave a corpse
		// finishing its attack, hitbox included. Cancelling also clears State.Attacking and
		// State.Attacking.Committed through the normal ability-end path, so death cannot leak
		// the tags that would forbid every future defensive action on revive.
		AbilitySystem->CancelAllAbilities();
	}

	// A buffered press must not survive death: it would fire on revive, an action asked for
	// in a situation that no longer exists. Local input state, so it is meaningless on any
	// machine that is not the one that pressed the button.
	BufferedInput.Clear();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BufferedReleaseTimerHandle);
	}

	if (DebugAutoReviveSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			DebugReviveTimerHandle,
			this,
			&ATDCombatCharacter::ReviveFromDebug,
			DebugAutoReviveSeconds,
			false);
	}

	ApplyDeathState();
}

void ATDCombatCharacter::OnRep_Dead()
{
	// The server ran the matching half directly. Everything below is what a client needs in
	// order to *look* and *behave* dead without being told twice.
	if (bDead)
	{
		ApplyDeathState();
	}
	else
	{
		ClearDeathState();
	}
}

void ATDCombatCharacter::ApplyDeathState()
{
	if (AbilitySystem)
	{
		AbilitySystem->AddLooseGameplayTag(TDTags::State_Dead);
	}

	// Otherwise "dead" is only a tag and the corpse keeps walking, which is the exact
	// complaint this item exists to fix. Velocity is zeroed as well as input disabled, or
	// momentum carries the body along for a second afterwards.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	// Dying airborne would otherwise strand this flag set forever: DisableMovement stops the
	// fall, so Landed() never fires to clear it, and regen stays suppressed for the rest of
	// the character's life -- past the revive, silently. Death ends the jump it was tracking.
	bJumpRegenPauseActive = false;

	if (bRagdollOnDeath)
	{
		StartRagdoll();
	}
}

void ATDCombatCharacter::ClearDeathState()
{
	if (AbilitySystem)
	{
		AbilitySystem->RemoveLooseGameplayTag(TDTags::State_Dead);
	}

	// Before the teleport, deliberately. StopRagdoll reattaches the mesh to the capsule, and
	// moving the actor while physics still drives the mesh in world space would leave the body
	// behind. Order is the whole correctness argument here.
	StopRagdoll();

	// An auto-attacker revives at the spot it was placed rather than wherever its last root
	// motion carried it. Without this it stands up displaced and only drifts home on its next
	// attack cycle -- which is what play reported. No-op for anything that is not an
	// auto-attacker, so the player revives where they fell.
	ReturnToDebugAutoAttackHome();

	// Falling rather than Walking, deliberately. The character may have died in mid-air, and
	// forcing Walking there leaves it hovering with no gravity until something else disturbs
	// it. Falling is self-correcting in both cases: on the ground the movement component
	// resolves it to Walking on the next update, in the air it simply resumes the fall.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Falling);
	}
}

void ATDCombatCharacter::StartRagdoll()
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!SkeletalMesh || bRagdollActive)
	{
		return;
	}

	// Physics silently refuses to simulate without one, leaving the character standing dead
	// with no error anywhere. Warned rather than logged quietly: a mesh swap is exactly how
	// this would break, and the symptom -- death stops looking like death -- reads as a
	// gameplay regression rather than a missing asset.
	if (!SkeletalMesh->GetPhysicsAsset())
	{
		UE_LOG(LogTDCombatTiming, Warning,
			TEXT("%s: bRagdollOnDeath is set but the mesh has no physics asset; death will not ragdoll."),
			*GetName());
		return;
	}

	bRagdollActive = true;

	// The capsule stops colliding so it cannot hold the body up or shove it around. It is
	// deliberately left in place rather than moved: it is still what the actor's transform
	// means, and the revive puts the mesh back onto it.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	SkeletalMesh->SetCollisionProfileName(TEXT("Ragdoll"));
	SkeletalMesh->SetAllBodiesSimulatePhysics(true);
	SkeletalMesh->SetSimulatePhysics(true);
	SkeletalMesh->WakeAllRigidBodies();
}

void ATDCombatCharacter::StopRagdoll()
{
	USkeletalMeshComponent* SkeletalMesh = GetMesh();
	if (!SkeletalMesh || !bRagdollActive)
	{
		return;
	}
	bRagdollActive = false;

	SkeletalMesh->SetSimulatePhysics(false);
	SkeletalMesh->SetAllBodiesSimulatePhysics(false);
	SkeletalMesh->PutAllRigidBodiesToSleep();
	SkeletalMesh->SetCollisionProfileName(TEXT("CharacterMesh"));

	// Reattached explicitly: simulation detaches the mesh from the capsule in all but name,
	// and setting the relative transform without reattaching leaves it in world space.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		SkeletalMesh->AttachToComponent(Capsule, FAttachmentTransformRules::KeepRelativeTransform);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// From the value captured before physics ever touched it. Reading the current relative
	// transform here would bake the ragdoll's final pose in as the rest offset.
	SkeletalMesh->SetRelativeTransform(MeshRestRelativeTransform);
}

void ATDCombatCharacter::ReviveFromDebug()
{
	if (!bDead)
	{
		return;
	}
	bDead = false;

	TD_TIMING_LOG(TEXT("[%.3f] REVIVE     %s"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *GetName());

	// Authority-only: attribute writes are the server's, and clients receive them by
	// replication. Restoring them from a client's OnRep would be a client rewriting its own
	// health, which is both wrong and the shape of an exploit.
	if (AbilitySystem)
	{
		AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), GetMaxHealth());

		// Stamina too, deliberately. Dying at low stamina and reviving instantly exhausted is
		// a debug annoyance with no design content -- and exhaustion ends only at full, so it
		// would outlast the revive by several seconds.
		AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetStaminaAttribute(), GetMaxStamina());
	}

	// Exhaustion is cleared explicitly rather than waiting for the stamina delegate: the
	// delegate fires on a *change*, and reviving from an already-full bar changes nothing.
	ExitExhaustion();

	ClearDeathState();
}

void ATDCombatCharacter::EnterExhaustion()
{
	bExhausted = true;
	ApplyExhaustionState();
}

void ATDCombatCharacter::ExitExhaustion()
{
	if (!bExhausted)
	{
		return;
	}
	bExhausted = false;

	ClearExhaustionState();
}

void ATDCombatCharacter::OnRep_Exhausted()
{
	if (bExhausted)
	{
		ApplyExhaustionState();
	}
	else
	{
		ClearExhaustionState();
	}
}

void ATDCombatCharacter::ApplyExhaustionState()
{
	if (AbilitySystem && ExhaustedTag.IsValid())
	{
		AbilitySystem->AddLooseGameplayTag(ExhaustedTag);
	}
}

void ATDCombatCharacter::ClearExhaustionState()
{
	if (AbilitySystem && ExhaustedTag.IsValid())
	{
		AbilitySystem->RemoveLooseGameplayTag(ExhaustedTag);
	}
}

void ATDCombatCharacter::Jump()
{
	// Deliberately silent. Exhaustion is communicated by the tag and the empty bar; a
	// failed jump that plays nothing reads as the lockout it is. Jump is not a
	// GameplayAbility, so the dead check in UTDGameplayAbility does not cover it and has
	// to be repeated here -- the one place that rule is not centralised.
	//
	// The movement lock is here for the same reason and is the third rule this function has had
	// to copy. That is the argument for jump eventually becoming an ability: every lockout the
	// abilities get for free has to be restated here, and this is the only place that can be
	// forgotten. Deferred to the structure audit rather than done inline.
	if (bExhausted || bDead || IsMovementLocked())
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
	return AbilitySystem;
}

void ATDCombatCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Captured before anything can move it, so the revive has a true rest pose to restore.
	if (const USkeletalMeshComponent* SkeletalMesh = GetMesh())
	{
		MeshRestRelativeTransform = SkeletalMesh->GetRelativeTransform();
	}

	// Covers characters that are never possessed, such as a placed training dummy.
	InitialiseAbilitySystem();
}

void ATDCombatCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Possession is where a player's PlayerState first becomes reachable on the server, so this
	// is the call that swaps a player off the fallback ASC and onto the real one.
	InitialiseAbilitySystem();
}

void ATDCombatCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// The client half of the same swap. PossessedBy does not run on a simulated proxy, and the
	// PlayerState replicates after the pawn, so without this a client keeps the fallback ASC.
	InitialiseAbilitySystem();
}

UAbilitySystemComponent* ATDCombatCharacter::ResolveAbilitySystem(AActor*& OutOwner) const
{
	if (ATDPlayerState* TDPlayerState = GetPlayerState<ATDPlayerState>())
	{
		OutOwner = TDPlayerState;
		return TDPlayerState->GetAbilitySystemComponent();
	}

	OutOwner = const_cast<ATDCombatCharacter*>(this);
	return OwnedAbilitySystemComponent;
}

void ATDCombatCharacter::InitialiseAbilitySystem()
{
	AActor* OwnerActor = nullptr;
	UAbilitySystemComponent* Resolved = ResolveAbilitySystem(OwnerActor);
	if (!Resolved)
	{
		return;
	}

	AbilitySystem = Resolved;

	if (ATDPlayerState* TDPlayerState = Cast<ATDPlayerState>(OwnerActor))
	{
		AttributeSet = TDPlayerState->GetAttributeSet();
	}
	else
	{
		AttributeSet = OwnedAttributeSet;
	}

	// Rebound every time rather than once: possession changes the owner, and the owner is what
	// GAS resolves prediction keys and net roles against. The avatar stays the pawn, so traces,
	// sockets and montages keep reading the body.
	AbilitySystem->InitAbilityActorInfo(OwnerActor, this);

	// Attributes and abilities are authority-only state; clients receive them by replication.
	if (HasAuthority())
	{
		SeedAbilitySystemDefaults();
	}
}

void ATDCombatCharacter::SeedAbilitySystemDefaults()
{
	// Guarded per *ASC*, not per call and not per character. Actor info is rebound on every
	// possession but seeding must happen once, or a pawn possessed after BeginPlay is granted
	// every ability a second time and stacks a second copy of every DefaultEffect -- and for an
	// infinite effect that is a permanently doubled magnitude rather than a visible one-off.
	//
	// Where the flag lives is the subtle half. A player's BeginPlay runs *before* possession, so
	// a single flag on the character would be spent on the fallback ASC and the PlayerState's
	// real one would never be seeded -- a player with no attributes and no abilities, while the
	// never-possessed training dummy worked perfectly.
	ATDPlayerState* TDPlayerState = GetPlayerState<ATDPlayerState>();

	if (TDPlayerState)
	{
		if (TDPlayerState->HasSeededDefaults())
		{
			return;
		}
		TDPlayerState->MarkDefaultsSeeded();
	}
	else
	{
		if (bOwnedDefaultsApplied)
		{
			return;
		}
		bOwnedDefaultsApplied = true;
	}

	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetMaxHealthAttribute(), StartingMaxHealth);
	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetHealthAttribute(), StartingMaxHealth);
	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetMaxStaminaAttribute(), StartingMaxStamina);
	AbilitySystem->SetNumericAttributeBase(UTDAttributeSet::GetStaminaAttribute(), StartingMaxStamina);

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
	{
		if (AbilityClass)
		{
			// Input is matched against the ability's InputTag at press time, so the spec
			// needs no input ID of its own.
			AbilitySystem->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	}

	FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
	Context.AddSourceObject(this);

	for (const TSubclassOf<UGameplayEffect>& EffectClass : DefaultEffects)
	{
		if (!EffectClass)
		{
			continue;
		}

		const FGameplayEffectSpecHandle SpecHandle = AbilitySystem->MakeOutgoingSpec(EffectClass, 1, Context);
		if (SpecHandle.IsValid())
		{
			AbilitySystem->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}

	// Bound after seeding, so the initial fill to full does not read as a change to zero.
	AbilitySystem->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetStaminaAttribute())
		.AddUObject(this, &ATDCombatCharacter::HandleStaminaChanged);

	AbilitySystem->GetGameplayAttributeValueChangeDelegate(UTDAttributeSet::GetHealthAttribute())
		.AddUObject(this, &ATDCombatCharacter::HandleHealthChanged);

	if (bDebugAutoAttack && DebugAutoAttackInputTag.IsValid())
	{
		// Captured before the first swing, so it is the placed transform rather than wherever
		// root motion has since carried us.
		DebugAutoAttackHomeTransform = GetActorTransform();

		// Reset when the swing actually finishes rather than on a fixed delay: attack length
		// varies by tier, and a delay long enough for a charged attack would be most of the gap.
		AbilitySystem->OnAbilityEnded.AddUObject(this, &ATDCombatCharacter::HandleDebugAutoAttackEnded);

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
	// Guarded on the auto-attack flag as well as the reset flag, because HomeTransform is only
	// captured for auto-attackers. Without this, calling it on anything else -- the player, on
	// revive -- teleports to an identity transform, i.e. the world origin.
	if (!bDebugAutoAttackResetPosition || !bDebugAutoAttack)
	{
		return;
	}

	// Death cancels the running attack, which fires the ability-ended reset -- so without this
	// the dummy's corpse teleports home mid-ragdoll, dragging the capsule out from under a mesh
	// that physics is driving in world space. ReviveFromDebug calls this again once the ragdoll
	// has been put away, which is what actually gets the body home.
	if (bDead)
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
	if (!AbilitySystem || !InputTag.IsValid())
	{
		return;
	}

	// Collect handles rather than acting inside the loop: activating an ability can
	// modify the spec list, and the specs would move underneath the iterator.
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
	{
		const UTDGameplayAbility* Ability = Cast<UTDGameplayAbility>(Spec.Ability);
		if (Ability && Ability->InputTag.MatchesTagExact(InputTag))
		{
			OutHandles.Add(Spec.Handle);
		}
	}
}

bool ATDCombatCharacter::TryActivateAbilitiesForInput(const FGameplayTag& InputTag, bool bForwardToActive)
{
	if (!AbilitySystem)
	{
		return false;
	}

	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	bool bActivated = false;

	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		if (!Spec)
		{
			continue;
		}

		// Marks the spec as held before activating, which is the state WaitInputRelease reads
		// -- so an ability starts life knowing the button is down and holds keep working.
		// Skipped for anything already running on a retry: see bForwardToActive.
		if (bForwardToActive || !Spec->IsActive())
		{
			AbilitySystem->AbilitySpecInputPressed(*Spec);
		}

		if (!Spec->IsActive())
		{
			bActivated |= AbilitySystem->TryActivateAbility(Handle);
		}
	}

	return bActivated;
}

void ATDCombatCharacter::ReleaseAbilitiesForInput(const FGameplayTag& InputTag)
{
	if (!AbilitySystem)
	{
		return;
	}

	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		if (FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle))
		{
			AbilitySystem->AbilitySpecInputReleased(*Spec);
		}
	}
}

bool ATDCombatCharacter::ShouldBufferInput(const FGameplayTag& InputTag) const
{
	if (InputBufferSeconds <= 0.0f || !AbilitySystem)
	{
		return false;
	}

	const FGameplayAbilityActorInfo* ActorInfo = AbilitySystem->AbilityActorInfo.Get();

	TArray<FGameplayAbilitySpecHandle> Handles;
	GatherAbilitiesForInput(InputTag, Handles);

	// Asked of the abilities rather than decided here, so the rule sits next to the flag that
	// creates it. Any one of them wanting the press remembered is enough.
	for (const FGameplayAbilitySpecHandle& Handle : Handles)
	{
		const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle);
		const UTDGameplayAbility* Ability = Spec ? Cast<UTDGameplayAbility>(Spec->Ability) : nullptr;
		if (Ability && Ability->ShouldBufferFailedInput(ActorInfo))
		{
			return true;
		}
	}

	return false;
}

void ATDCombatCharacter::OnAbilityInputPressed(FGameplayTag InputTag)
{
	// A live edge always beats a recorded one. Without this, a replay still scheduled from an
	// earlier buffered press would land on whatever ability is running by the time it fires --
	// releasing a hold the player is in the middle of.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BufferedReleaseTimerHandle);
	}

	// Any new press supersedes a buffered one, whether or not this press succeeds and whatever
	// it was. Pressing something else says you have stopped waiting on the last thing -- and
	// without this a dodge buffered into a lockout could still surface after an attack the
	// player chose instead of it. Pressing the *same* thing again is unremarkable and is only
	// cleared here so it cannot outlive the press that replaced it.
	if (BufferedInput.IsSet())
	{
		if (!BufferedInput.InputTag.MatchesTagExact(InputTag))
		{
			TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: dropped, superseded by %s"),
				GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f,
				*BufferedInput.InputTag.ToString(),
				*InputTag.ToString());
		}

		BufferedInput.Clear();
	}

	if (TryActivateAbilitiesForInput(InputTag, /*bForwardToActive=*/true))
	{
		return;
	}

	if (!ShouldBufferInput(InputTag))
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// One slot, last press wins. A queue would replay stale intent as a burst: press dodge
	// then attack into a lockout and you would get both, in an order you had already stopped
	// asking for. Only the most recent press is still something the player wants.
	const float Now = World->GetTimeSeconds();

	BufferedInput.Clear();
	BufferedInput.InputTag = InputTag;
	BufferedInput.PressWorldTime = Now;
	BufferedInput.ExpiryWorldTime = Now + InputBufferSeconds;

	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: stored"), Now, *InputTag.ToString());
}

void ATDCombatCharacter::OnAbilityInputReleased(FGameplayTag InputTag)
{
	// Same reason as the press: a real release makes any pending replay redundant at best.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BufferedReleaseTimerHandle);
	}

	ReleaseAbilitiesForInput(InputTag);

	// Recorded rather than acted on. The buffer needs this edge because the attack ladder
	// resolves its tier from whether the button is still down at each checkpoint: a press
	// replayed as though it were still held would run past every one of them and turn a tap
	// into a charged heavy. It is also what starts the window counting down -- until now the
	// press was a held button, which is live intent rather than something to expire.
	const UWorld* World = GetWorld();
	if (!World || !BufferedInput.IsSet() || BufferedInput.bReleased)
	{
		return;
	}

	if (!BufferedInput.InputTag.MatchesTagExact(InputTag))
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	BufferedInput.bReleased = true;
	BufferedInput.HoldSeconds = FMath::Max(0.0f, Now - BufferedInput.PressWorldTime);
	BufferedInput.ExpiryWorldTime = Now + InputBufferSeconds;

	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: released after %.0fms held"),
		Now, *InputTag.ToString(), BufferedInput.HoldSeconds * 1000.0f);
}

void ATDCombatCharacter::ReplayBufferedRelease(FGameplayTag InputTag)
{
	ReleaseAbilitiesForInput(InputTag);

	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: replayed release"),
		GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f, *InputTag.ToString());
}

void ATDCombatCharacter::TickInputBuffer()
{
	if (!BufferedInput.IsSet())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	// A button still down is not a stale input, so it never expires -- the same
	// push-the-deadline-forward idiom the regen pause uses. This is what makes a buffered
	// heavy reachable at all: its 200 ms boundary is past any window this size, so every
	// tier above light necessarily comes from a hold that outlives the window.
	if (!BufferedInput.bReleased)
	{
		BufferedInput.ExpiryWorldTime = FMath::Max(BufferedInput.ExpiryWorldTime, Now + InputBufferSeconds);
	}

	if (Now >= BufferedInput.ExpiryWorldTime)
	{
		TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: expired, %.0fms after press"),
			Now, *BufferedInput.InputTag.ToString(), (Now - BufferedInput.PressWorldTime) * 1000.0f);
		BufferedInput.Clear();
		return;
	}

	// Retried every frame rather than woken by an event. Every reason an activation can be
	// refused -- a blocking tag, a live instance, an ability ending, the airborne check --
	// would otherwise have to be enumerated and hooked, and missing one fails silently.
	// Polling cannot be incomplete, and there is at most one buffered input to retry.
	if (!TryActivateAbilitiesForInput(BufferedInput.InputTag, /*bForwardToActive=*/false))
	{
		return;
	}

	const FGameplayTag FiredTag = BufferedInput.InputTag;
	const bool bWasReleased = BufferedInput.bReleased;
	const float HoldSeconds = BufferedInput.HoldSeconds;
	const float LateBySeconds = Now - BufferedInput.PressWorldTime;

	BufferedInput.Clear();

	if (!bWasReleased)
	{
		// Still held, so the live release edge arrives on its own and the hold is measured
		// from activation. The time held before then is deliberately not credited: the windup
		// is preset, and crediting it would land the attack sooner than its tier is authored
		// to take.
		TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: fired %.0fms late, still held"),
			Now, *FiredTag.ToString(), LateBySeconds * 1000.0f);
		return;
	}

	// The button came up before anything could answer it, so replay that edge at the offset it
	// really had. Releasing at once instead would flatten every buffered hold to the shortest
	// branch -- a 236ms hold, a heavy by every rule the ladder has, came out a light before
	// this existed. The windup still runs its full preset length from activation; only the
	// *tier* is carried across, never the time already spent holding.
	TD_TIMING_LOG(TEXT("[%.3f] BUFFER     %s: fired %.0fms late, replaying release at +%.0fms"),
		Now, *FiredTag.ToString(), LateBySeconds * 1000.0f, HoldSeconds * 1000.0f);

	if (HoldSeconds <= KINDA_SMALL_NUMBER)
	{
		ReplayBufferedRelease(FiredTag);
		return;
	}

	World->GetTimerManager().SetTimer(
		BufferedReleaseTimerHandle,
		FTimerDelegate::CreateWeakLambda(this, [this, FiredTag]() { ReplayBufferedRelease(FiredTag); }),
		HoldSeconds,
		false);
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
