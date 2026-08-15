// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/TheDreamCharacter.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "Engine/TimerHandle.h"
#include "Combat/TDAttackHitbox.h"
#include "TDCombatCharacter.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UGameplayEffect;
class UInputAction;
class UStaticMeshComponent;
class UTDAttributeSet;
struct FAbilityEndedData;

/**
 *  A press that nothing was able to answer, kept for a moment in case something can.
 *
 *  Records **both edges**, because the attack ladder's identity is a consequence of how long
 *  the button was held. A press replayed without knowing whether the button had since come up
 *  would leave the attack believing it was still held, sail past every checkpoint, and turn
 *  a tap into a charged heavy.
 *
 *  The release is recorded as a **duration**, and replayed at that same offset from activation.
 *
 *  An earlier version stored it as a yes/no and released at once, on the argument that the
 *  buffer outlives a release by only InputBufferSeconds, so every recordable hold had to be
 *  shorter than the window, and therefore inside the light's hold boundary. **That argument is
 *  false and play disproved it**: the buffer survives indefinitely while the button is *held*, so the
 *  recorded hold is bounded by how long the block lasted, not by the window. A 236 ms hold --
 *  a heavy by every rule the ladder has -- was recorded, flattened to a light, and only found
 *  because the trace prints the duration. Any hold length is reachable this way.
 */
struct FTDBufferedInput
{
	/** Which input was pressed. Invalid means the buffer is empty. */
	FGameplayTag InputTag;

	/** World time the button went down, for the trace's "how late did this fire". */
	float PressWorldTime = 0.0f;

	/** World time to give up at. Held buttons keep pushing this forward; see TickInputBuffer. */
	float ExpiryWorldTime = 0.0f;

	/**
	 *  How long the button was held, valid once bReleased. Replayed at this offset from
	 *  activation, which is what preserves the tier the player actually asked for.
	 */
	float HoldSeconds = 0.0f;

	/** True once the button came up, whether or not the buffer has fired. */
	bool bReleased = false;

	bool IsSet() const { return InputTag.IsValid(); }
	void Clear() { *this = FTDBufferedInput(); }
};

/**
 *  When a debug auto-attacker turns to face what it is swinging at.
 *
 *  A dummy that never turns is a *stable* fixture and a misleading one: every defensive
 *  verdict from Block onward is measured against what it throws, and an attack that cannot
 *  follow a sidestepping player reads as more forgiving than a human opponent would. But a
 *  dummy that always faces you is unpleasant to work around when you are measuring something
 *  else, so this is three modes rather than a bool.
 *
 *  **Facing is all this does.** The dummy does not approach, and that is deliberate -- the
 *  parity rule is "accurate in the dimension being measured", and Block measures what happens
 *  when an attack arrives rather than how it closed the distance.
 */
UENUM(BlueprintType)
enum class ETDDebugFacingMode : uint8
{
	/** Never turn. The pre-2026-08-14 behaviour, kept because it is a useful control. */
	Never,

	/**
	 *  Turn only while an attack is running, and let the position reset restore the placed
	 *  yaw between swings. The default for the training dummy: it aims what it throws without
	 *  following you around the level when you are measuring something unrelated.
	 */
	WhileAttacking,

	/** Turn continuously. Closest to a real opponent, and the most intrusive to work around. */
	Always
};

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

	/** True from health reaching 0 until revived. Every ability is refused throughout. */
	UFUNCTION(BlueprintPure, Category="Combat|Health")
	bool IsDead() const { return bDead; }

	/** True while BlockingTag is present, i.e. a guard is up. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsBlocking() const;

	/** True from a guard breaking until its stun expires. Every ability is refused throughout. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsGuardBroken() const { return bGuardBroken; }

	/**
	 *  True while a *successful* block's lockout is running. Refuses offense and parry only.
	 *
	 *  Deliberately not the same state as IsGuardBroken(), which is the penalty for a guard that
	 *  failed and refuses everything. A block that worked still costs the defender initiative;
	 *  it does not cost them their guard.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsInBlockstun() const { return bInBlockstun; }

	/**
	 *  Starts the blockstun lockout, or extends one already running to the later end time.
	 *
	 *  Called by the attacking ability when its hit is blocked, so the duration travels with the
	 *  attack rather than being a property of the defender -- a heavy should pin a guard for longer
	 *  than a light does, and only the attack knows which it was.
	 */
	void EnterBlockstun(float DurationSeconds);

	/**
	 *  Starts the guard's minimum-duration commitment. Called by the block ability on activation.
	 *
	 *  Pushed from the ability rather than detected here as a rising edge on IsBlocking(), because
	 *  an edge is exactly the thing that goes wrong when a guard is cancelled and resumed inside a
	 *  frame -- which this project has already had happen twice.
	 */
	void BeginBlockCommitment();

	/**
	 *  Charges BlockInitialStaminaCost. Called by the block ability once it has actually activated.
	 *
	 *  Deliberately *not* EffectOnStart, which is how the dodge pays. That route needs a
	 *  GameplayEffect asset, and a GameplayEffect's modifier attribute cannot be reliably configured
	 *  through the toolset -- it accepts the write and leaves the FProperty null, so the effect
	 *  modifies nothing and says so nowhere. The guard's other stamina spend already lives in C++ on
	 *  this character for the same reason, and one home beats two.
	 */
	void PayBlockInitialCost();

	/** True while a guard is inside its minimum duration and cannot be acted out of. */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsBlockCommitted() const;

	/**
	 *  True if this attack's origin lies inside the guard's forward arc.
	 *
	 *  The spec's "180 degree forward coverage" as a single question, asked in the *defender's*
	 *  frame at the moment the hit resolves. A block is a claim about where you are looking, so a
	 *  hit from behind is not blocked however long the button has been held.
	 *
	 *  Deliberately measured against facing rather than against the camera: the defender's body is
	 *  what an attacker can see, and defence has to be legible from the outside. That is the same
	 *  reason the damage wedge is actor-framed while aim assist is camera-framed.
	 */
	UFUNCTION(BlueprintPure, Category="Combat|Block")
	bool IsGuardFacing(const FVector& AttackOriginWorld) const;

	/**
	 *  Spends stamina from a blocked hit and breaks the guard if that empties the bar.
	 *
	 *  **This is the only thing that can break a guard**, which is the distinction the whole
	 *  design rests on: drain runs you to zero and leaves you there, damage is what punishes you
	 *  for being there. Returns true if the guard broke.
	 *
	 *  The break condition is *post-damage stamina is zero*, one rule that covers both cases
	 *  without a special case -- damage exceeding what is left, and damage landing on a bar that
	 *  is already empty. The second is what makes holding a guard at zero costly rather than free,
	 *  and it is also why this cannot be driven from the stamina-changed delegate: that fires only
	 *  on a *change*, so a hit taken at exactly zero would move nothing and be silently ignored.
	 *
	 *  Server only. The bar replicates, and the tag is applied on both sides by the state pair.
	 */
	void ApplyStaminaDamage(float Amount);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Blocked while exhausted, per the design: no defensive actions and no jump. */
	virtual void Jump() override;

	/**
	 *  The dead do not turn to face the camera -- **and neither does a character mid-swing.**
	 *
	 *  ORed with Super rather than replacing it. The base holds the attack lock, so returning
	 *  only bDead here would discard it silently: attacks would keep tracking the camera through
	 *  their own release window and drag an actor-frame hitbox around with them.
	 */
	virtual bool IsFacingLocked() const override { return bDead || Super::IsFacingLocked(); }

	/**
	 *  Idle additionally means no ability running and no press waiting to be answered.
	 *
	 *  ANDed with Super rather than replacing it, so movement input and being airborne still
	 *  count as activity. Deliberately keyed on *any* active ability rather than a list of
	 *  state tags: attacking, dodging, and every future block, parry and stun are covered
	 *  without this function being revisited, which is the whole reason the rule is "zero
	 *  presses" rather than "not attacking".
	 *
	 *  The buffered press counts too. A press that was refused and stored is still a press --
	 *  the player has asked for something and is waiting on it, which is not idling.
	 */
	virtual bool IsIdle() const override;

	/** The actual launch, as opposed to Jump() which only records the press. Starts the regen pause. */
	virtual void OnJumped_Implementation() override;

	/** Ends the jump's regen pause, leaving JumpRegenPauseSeconds of tail behind it. */
	virtual void Landed(const FHitResult& Hit) override;
	virtual void PossessedBy(AController* NewController) override;

	/**
	 *  A client's PlayerState arrives *after* its pawn, so the ASC has to be resolved again here.
	 *
	 *  Without this a client initialises against a null PlayerState, silently falls back to the
	 *  owned ASC, and has no abilities -- while the server, which possesses before replicating,
	 *  works perfectly. That asymmetry is what makes it worth an override rather than a comment.
	 */
	virtual void OnRep_PlayerState() override;

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
	 *  How long a press nothing could answer keeps trying, once the button is back up.
	 *
	 *  Without this an input pressed during a committed action is simply discarded, which the
	 *  player cannot tell apart from the action being unresponsive -- so it confounded every
	 *  timing verdict in the project until it existed.
	 *
	 *  **It is a window on taps, not on intent.** A button still held is not a stale input, so
	 *  the buffer does not expire while it is down; this measures from the release. That is
	 *  what lets a heavy be buffered at all -- its 200 ms boundary is beyond any window this
	 *  size, so a tier above light can only ever come from a hold that outlives the window.
	 *
	 *  Zero disables buffering entirely, which is the honest way to A/B whether it is helping.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Input", meta=(ClampMin="0.0"))
	float InputBufferSeconds = 0.1f;

	/**
	 *  Stamina regained per second while *not* exhausted, and while regen is running at all.
	 *
	 *  Driven here rather than by a periodic GameplayEffect because the economy is a small
	 *  state machine -- regen, a pause that outlives the action causing it, and an
	 *  exhaustion lockout -- and expressing that across effects means tag components that
	 *  cannot be scripted. Attributes and tags are still GAS; only the orchestration is here.
	 *
	 *  Raised 25 -> 40 on 2026-08-14, at the same time exhaustion kept the old 25. Note what that
	 *  changes beyond recovery speed: this number is how fast a *dodge* becomes affordable again,
	 *  so it sets the cadence of repeated evasion as directly as DodgeSeconds does.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float StaminaRegenPerSecond = 40.0f;

	/**
	 *  Stamina regained per second *while exhausted*. Separate from StaminaRegenPerSecond so that
	 *  being run out of stamina costs more than the bar it emptied.
	 *
	 *  **This is not the ExhaustionSeconds timer that was deleted, and the difference matters.**
	 *  That was a duration which could disagree with the bar -- two termination conditions, one of
	 *  which was a lie. This is a *rate*: exhaustion still ends when stamina reaches Max and at no
	 *  other moment, so the bar remains the single source of truth for how long it lasts. What
	 *  splitting the rate buys is that the penalty can be tuned without also retuning how fast
	 *  everyone recovers in normal play, which is what welding them prevented.
	 *
	 *  **Zero is forbidden, and it is forbidden for a real reason rather than tidiness.** Regen is
	 *  the only thing that can end exhaustion, so a rate of zero is not "no recovery" -- it is a
	 *  character that is exhausted permanently, with every defensive action locked out for the rest
	 *  of the match. That is the same defect the regen-suppression fix removes, reintroduced through
	 *  a designer-facing knob, so the clamp is load-bearing. See TickStaminaRegen.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.01"))
	float ExhaustedStaminaRegenPerSecond = 25.0f;

	/**
	 *  How long regen stays suppressed *after* the last action carrying StaminaRegenPausedTag ends.
	 *
	 *  Measured from the end, not the start, so it is a genuine tax on acting rather than
	 *  something a long action absorbs for free.
	 *
	 *  **Shared by every such ability, and attacks joined them on 2026-08-14** -- so an attack is
	 *  taxed for its whole windup, release and recovery, plus this. A per-ability tail was proposed
	 *  the same day and deliberately declined: four authored numbers is real authoring overhead for
	 *  a distinction nobody has felt yet. The shared value stays until play asks for the split.
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
	 *  Present while an action that suppresses regen is running, via that ability's owned tags.
	 *
	 *  Regen watches for this rather than each ability telling it to stop, so an ability
	 *  that is cancelled or interrupted cannot leave regen suppressed forever.
	 *
	 *  **Carried by GA_Dodge, GA_Block and -- since 2026-08-14 -- GA_Attack.** Those assets are
	 *  authoritative for who suppresses regen; nothing in C++ names them, which is what makes
	 *  adding a fourth a content change rather than a code one.
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
	 *  not on recovering. That is load-bearing rather than merely humane: regen is the only
	 *  thing that can end it.
	 *
	 *  **The regen pause is the one thing that still suppresses it**, and deliberately so as of
	 *  2026-08-14. Bypassing the pause while exhausted was tried for a few hours and thrown out by
	 *  play -- it refunded the cost of the very action that emptied the bar. Holding an action at
	 *  zero can therefore suppress recovery for as long as it is held, which is a choice with an
	 *  obvious exit rather than a trap: releasing is always available and always correct, and a
	 *  guard held at zero accomplishes nothing anyway since anything it blocks breaks it.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina")
	FGameplayTag ExhaustedTag;

	/**
	 *  Present while a guard is held, via GA_Block's owned tags. Drives the drain below.
	 *
	 *  Read from the character rather than the ability draining itself, because every other
	 *  stamina rule already lives here -- regen, its pause and exhaustion are orchestrated in one
	 *  place precisely so they cannot disagree, and a second spender running on an ability's own
	 *  clock would be the first thing able to.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block")
	FGameplayTag BlockingTag;

	/**
	 *  Stamina spent per second for as long as a guard is held. **Drain, not damage.**
	 *
	 *  The two are separate mechanisms and only one of them can break a guard. This one is
	 *  self-inflicted: it runs the bar down and parks it at zero, harmlessly, for as long as the
	 *  player cares to hold. What it actually buys the attacker is that the defender stops being
	 *  able to *absorb* anything -- a guard at zero breaks to the very next blocked hit.
	 *
	 *  So this is not a countdown on how long you may block. It is how fast holding a guard
	 *  converts into risk, which is why raising it makes blocking more committal without ever
	 *  taking the option away. The user's value is 10, giving ten seconds from full before a guard
	 *  is breakable by anything at all.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float BlockDrainPerSecond = 10.0f;

	/**
	 *  How long a broken guard is stunned for, and how long regen stays suppressed across it.
	 *
	 *  **One number for both deliberately.** The stun and the suppression are the same event seen
	 *  from two sides, and authoring them apart would immediately allow the pair that makes no
	 *  sense -- regen resuming while you are still stunned for it.
	 *
	 *  The existing StaminaRegenPauseSeconds runs *after* this rather than instead of it, because
	 *  the regen tick takes the max of every live suppressor rather than assigning: so a break
	 *  reads as the stun, then the ordinary pause, then recovery at the exhausted rate. The user's
	 *  value is 1.0, which with the shipped 0.5 pause and 25/s exhausted regen puts a broken guard
	 *  at roughly five and a half seconds from break to full.
	 *
	 *  Bounded, and that is what keeps it on the right side of the 2026-08-14 rule about
	 *  suppressors: an unbounded one is a deadlock, a bounded one is a cost.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float GuardBreakStunSeconds = 1.0f;

	/**
	 *  Ground speed cap while a guard is up. Authored, not derived.
	 *
	 *  Chosen as 25% of the 500 `MaxWalkSpeed` the character otherwise runs at, which puts it
	 *  halfway between the locomotion blendspace's idle and walk rows -- a braced shuffle rather
	 *  than a walk. **Recorded as a relationship, not implemented as one**: it stays a number to
	 *  tune, so changing `MaxWalkSpeed` will not move it and should prompt a second look here.
	 *
	 *  Restored from the value captured at BeginPlay rather than to a constant, so a Blueprint that
	 *  authors a different `MaxWalkSpeed` is not silently overwritten with this class's idea of one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float BlockingMaxWalkSpeed = 125.0f;

	/**
	 *  Ground speed cap while exhausted. Authored, not derived, exactly as the guard's cap is.
	 *
	 *  400 is the user's value: 20% below the 500 `MaxWalkSpeed` the character otherwise runs at.
	 *  Being run dry should read in the body before the player checks a bar, and exhaustion until
	 *  now was invisible except as a refusal -- something you discovered by pressing a button and
	 *  getting nothing.
	 *
	 *  **Recorded as a relationship, not implemented as one**, following BlockingMaxWalkSpeed: it
	 *  stays a number to tune, so changing `MaxWalkSpeed` will not move it.
	 *
	 *  **Combines with the guard's cap by taking the slower**, which is reachable rather than
	 *  theoretical: raising a guard you cannot afford exhausts you with the guard still up, so both
	 *  clamps are live at once. Both are penalties and neither is a licence to move faster, so the
	 *  minimum is the only combination that cannot be gamed by entering states in the right order.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Stamina", meta=(ClampMin="0.0"))
	float ExhaustedMaxWalkSpeed = 400.0f;

	/**
	 *  How long a raised guard is committed for. **Locking: nothing but movement is allowed.**
	 *
	 *  From play, 2026-08-14: without a floor the guard can be feathered at input speed, and
	 *  `attack > block > attack > block` read as unfinished. This is a gameplay fix and not a
	 *  cosmetic one -- the first diagnosis offered was a missing animation blend and the user
	 *  corrected it, which is worth recording because the number lands near a blend's duration and
	 *  the mistake is easy to repeat.
	 *
	 *  **It has to gate the attack to do anything at all.** A floor that only holds the guard up
	 *  while attacking still cancels out of it changes nothing about the feathering. So this is a
	 *  deliberate narrowing of "whichever comes last wins": that still holds between block, dodge
	 *  and attack, *except* inside this window, where the guard has already won.
	 *
	 *  Releasing inside the window does not lower the guard early; it schedules the drop for the
	 *  moment the window ends, and the guard defends for the whole of it. The commitment is real in
	 *  both directions -- you get the protection and you pay the drain.
	 *
	 *  Responsiveness comes from the input buffer rather than from shortening this: an attack
	 *  pressed inside the window is refused, buffered, and fires the instant it expires.
	 *
	 *  0.25 is the user's value, signed off knowing it is untuned, with the reasoning that a
	 *  clearly-too-long first probe answers "is feathering dead" better than a borderline one.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float MinimumBlockSeconds = 0.25f;

	/**
	 *  Stamina taken once, the moment a guard goes up. Zero by default, so it costs nothing to raise.
	 *
	 *  **Paid, never required**, like every cost in this project: raising a guard at 4 stamina with
	 *  a cost of 5 works, empties the bar and exhausts you. It is not refused, and the guard still
	 *  does everything it would have done -- including cancelling an attack, which is the case worth
	 *  stating because it is the one that has to survive. GAS applies an ability's cancel tags
	 *  during activation, before this is charged, so the attack is already gone by the time the
	 *  exhaustion lands. You get the cancel and you pay for it.
	 *
	 *  **It is not a guard break.** Nothing is broken and there is no stun; you are simply exhausted,
	 *  exactly as an over-budget dodge leaves you. Breaking a guard remains the sole preserve of
	 *  stamina *damage*, which is the distinction the whole economy rests on.
	 *
	 *  Charged on every activation, including a resume after an attack or dodge, under the rule the
	 *  user stated once and which settles this and the minimum duration together: **a resume is an
	 *  intended block, and all blocks are created equal.**
	 *
	 *  That is a gameplay claim rather than an implementation convenience, and it is what makes the
	 *  consequence acceptable instead of merely tolerated: with a non-zero value, holding a guard
	 *  through a swing pays twice, because it is two guards. The alternative -- a cheaper guard that
	 *  the player did not ask for and cannot distinguish -- would make the cost conditional on
	 *  something invisible, which is the failure the bimodal durations already demonstrated.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Block", meta=(ClampMin="0.0"))
	float BlockInitialStaminaCost = 0.0f;

	/**
	 *  Collapse into a ragdoll on death.
	 *
	 *  The minimal death shipped by the Death slice is otherwise *inert* rather than legible --
	 *  the character simply stops and nothing announces it, which was the first thing play
	 *  reported. A ragdoll is the cheapest unambiguous read, and it needs no animation content,
	 *  which matters because the directional `Death_<DIR>` clips belong to Stun.
	 *
	 *  Requires a physics asset on the mesh or physics silently does nothing. Ours has one
	 *  (`PA_Mannequin`, on the bundle's `SKM_Manny`); a mesh without one dies standing up.
	 *
	 *  Exposed so Stun can turn it off when death gets real animation, rather than having
	 *  to unpick it from code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Death")
	bool bRagdollOnDeath = true;

	/**
	 *  Debug only: seconds after death before reviving at full. 0 disables the auto-revive.
	 *
	 *  Death is deliberately the *minimum*: a state that stops the character acting, so the
	 *  health bar means something and damage observations after a kill are not garbage.
	 *  Respawn rules, whether death routes through knockdown, and whether the dummy should die
	 *  at all are Stun's, and a debug revive is what lets this ship without answering them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug", meta=(ClampMin="0.0"))
	float DebugAutoReviveSeconds = 3.0f;

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

	/**
	 *  Whether this auto-attacker turns to face its target, and when. Debug only.
	 *
	 *  **The turn itself needs nothing but a focus.** A possessed pawn with no focus has
	 *  AAIController copying its control rotation *from the pawn* every tick
	 *  (bSetControlRotationFromPawnOrientation, on by default), so the rotation error is
	 *  permanently zero and every turn rate on this character multiplies nothing -- which is
	 *  why the dummy held a bearing of -81 degrees across 100 seconds of swinging before this
	 *  existed. Setting a focus makes the control rotation point at the target, APawn::FaceRotation
	 *  no-ops because bUseControllerRotationYaw is false, and CharacterMovementComponent closes
	 *  the gap at RotationRate.Yaw -- the same path the player's three turn rates already govern.
	 *
	 *  So parity is inherited rather than configured: ATDCombatCharacter::IsIdle() returns false
	 *  while any ability is active, so a swinging dummy turns at TurnRateDegrees exactly as a
	 *  swinging player does, and idles at IdleTurnRateDegrees between swings.
	 *
	 *  **CoilTurnRateDegrees is the one rate this does not reach today**, because
	 *  DebugAutoAttackHoldSeconds is 0.1 and the light never coils. Raise the hold past the
	 *  heavy's boundary and it becomes live with no further change here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat|Debug")
	ETDDebugFacingMode DebugAutoAttackFacingMode = ETDDebugFacingMode::Never;

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

	FTDAttackHitbox AimAssistWedge;
	FGameplayTagContainer AimAssistImmunityTags;
	bool bAimAssistHoming = false;
	bool bAimAssistDrawDebug = false;

public:

	/**
	 *  Target Lock: turn the body toward whatever the camera has tagged, for the base lunge's span.
	 *
	 *  **Homing runs before the commit checkpoint and stops at it**, which is what stops it being
	 *  the homing this design rejected. Tracking *after* commit would mean a defender's movement
	 *  could no longer make an attack whiff; tracking before it costs nothing, because commit is
	 *  where the defender's reaction window opens in the first place. Freeze at the boundary that
	 *  already exists and whiff punish is untouched.
	 *
	 *  What it buys is that the correction arrives *gradually*. Without it the whole turn lands in
	 *  one frame at commit, which reads as a pop once the wedge is wide enough to matter -- and
	 *  that artifact, not the design, would have been the thing capping how wide the wedge could be.
	 *
	 *  Set at activation with the first branch's wedge, since every tier shares the windup and a
	 *  light is what releasing now would produce. Cleared at commit **and again in EndAbility**,
	 *  the one place every exit converges -- same contract as SetAbilityFacingLocked, because a
	 *  stranded homing state is a character that turns toward strangers forever.
	 */
	void SetAimAssistHoming(const FTDAttackHitbox& InWedge, const FGameplayTagContainer& InImmunityTags, bool bActive, bool bInDrawDebug);

	/**
	 *  The best Target Lock candidate for a wedge, or null. Shared by homing and the commit snap.
	 *
	 *  One implementation deliberately, because two would drift -- and the one that drifted would
	 *  be the one deciding where attacks point. Selection is smallest bearing with distance
	 *  breaking ties: angle is the player's expressed intent, distance is their own responsibility,
	 *  and the tiebreak exists so two machines cannot order equal candidates differently.
	 *
	 *  @param AimYawDegrees  The frame to measure in -- the *camera's*, not the body's. See
	 *                        ATheDreamCharacter::GetAimYawDegrees.
	 */
	static AActor* FindAimAssistTarget(
		const AActor* Attacker,
		float AimYawDegrees,
		const FTDAttackHitbox& Wedge,
		const FGameplayTagContainer& ImmunityTags,
		float& OutBearingDegrees);

protected:

	/** Homing's answer for the facing system: the bearing to the tagged target, in world yaw. */
	virtual bool GetFacingHomingYaw(float& OutYaw) const override;

protected:

	/**
	 *  Held props, attached to the SwordShield pack's own `Sword` / `Shield` sockets.
	 *
	 *  Those sockets hang off `hand_r` / `hand_l` and carry the grip rotation plus a
	 *  non-uniform scale that corrects for meshes authored several times too large -- the
	 *  shield's is 0.25, 0.20, 0.30 against a mesh 256 units tall. **Both props are therefore
	 *  correct at identity, and no offset should be authored here.** They exist only on the
	 *  bundle's `SKM_Manny`, not on Epic's `SKM_Manny_Simple`.
	 *
	 *  Not the `weapon_r` / `weapon_l` bones, which look like the obvious choice and fail
	 *  twice: absent from Epic's mesh, and animated only by GDH clips, so under any Epic
	 *  animation a prop parented there freezes at reference pose.
	 *
	 *  Deliberately cosmetic: collision is off and the melee trace is a separate ability task,
	 *  so the sword's mesh never decides what the sword hits. Left empty on the CDO, which is
	 *  what keeps the training dummy unarmed without needing its own class.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Equipment")
	TObjectPtr<UStaticMeshComponent> WeaponMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat|Equipment")
	TObjectPtr<UStaticMeshComponent> ShieldMesh;

	/**
	 *  The ASC this character is actually using: its PlayerState's, or OwnedAbilitySystemComponent.
	 *
	 *  Resolved rather than assumed, because only *players* have a PlayerState -- the training
	 *  dummy is AI-possessed with no PlayerState at all. Null until InitialiseAbilitySystem has
	 *  run, and on a client that is later than you expect, so every use is null-checked.
	 *
	 *  **Never reach past this to OwnedAbilitySystemComponent.** For a player that subobject
	 *  exists, is registered, and holds nothing -- so using it compiles, runs, and silently
	 *  reads an empty ASC with no attributes and no abilities. The fallback carries a name
	 *  nobody types by accident for exactly that reason.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	/** Attributes on whichever ASC was resolved. Null until InitialiseAbilitySystem has run. */
	UPROPERTY(Transient)
	TObjectPtr<UTDAttributeSet> AttributeSet;

	/**
	 *  The fallback ASC, for characters that have no PlayerState -- i.e. the training dummy.
	 *
	 *  A player carries this subobject too and never uses it. That is the accepted cost of one
	 *  character class serving both cases: it seeds no attributes and grants no abilities, so it
	 *  costs registration rather than bandwidth. Splitting into player and AI classes would
	 *  remove it, and was rejected because it means reparenting the two Blueprints everything
	 *  else depends on, to buy a distinction this comment already makes.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Combat")
	TObjectPtr<UAbilitySystemComponent> OwnedAbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UTDAttributeSet> OwnedAttributeSet;

private:

	/**
	 *  Points AbilitySystem at the right ASC, binds actor info, and seeds defaults once.
	 *
	 *  Safe -- and expected -- to call repeatedly: from BeginPlay, from every possession, and
	 *  from OnRep_PlayerState. Actor info is rebound every time because possession changes who
	 *  owns the ASC; seeding is guarded per-ASC, not per-call and not per-character.
	 */
	void InitialiseAbilitySystem();

	/**
	 *  Which ASC this character should use, and which actor owns it.
	 *
	 *  The PlayerState's when there is one, the owned component when there is not. OutOwner is
	 *  what InitAbilityActorInfo is given as the *owner*, which is not the avatar: GAS wants the
	 *  PlayerState as owner and the pawn as avatar, so that ability state survives a pawn while
	 *  traces, sockets and montages still resolve against the body.
	 */
	UAbilitySystemComponent* ResolveAbilitySystem(AActor*& OutOwner) const;

	/** Attributes, abilities and effects, applied once per ASC. Authority only. */
	void SeedAbilitySystemDefaults();

	void OnAbilityInputPressed(FGameplayTag InputTag);
	void OnAbilityInputReleased(FGameplayTag InputTag);

	/** Handles of granted abilities whose InputTag matches, in activation order. */
	void GatherAbilitiesForInput(const FGameplayTag& InputTag, TArray<FGameplayAbilitySpecHandle>& OutHandles) const;

	/**
	 *  Presses the input at every matching spec and starts whatever is not already running.
	 *
	 *  Returns whether a *new* activation happened -- deliberately not whether anything is
	 *  live. A press arriving at an ability that is already running is the single most
	 *  important thing to buffer, since a committed swing is what refuses most inputs.
	 *
	 *  bForwardToActive is false on a buffered retry: the button was pressed once, and a
	 *  retry is this system trying again rather than the player pressing again. Telling a
	 *  running ability it was re-pressed every frame is a lie that nothing reads today and
	 *  that WaitInputRelease would read tomorrow.
	 */
	bool TryActivateAbilitiesForInput(const FGameplayTag& InputTag, bool bForwardToActive);

	/** Forwards the release edge to every matching spec. */
	void ReleaseAbilitiesForInput(const FGameplayTag& InputTag);

	/** True if any ability answering this input wants its refusal remembered. */
	bool ShouldBufferInput(const FGameplayTag& InputTag) const;

	/** Retries the buffered press, or drops it once its window has passed. */
	void TickInputBuffer();

	/**
	 *  Replays a buffered release, HoldSeconds after the buffered press finally activated.
	 *
	 *  Cancelled the moment real input for that tag arrives, because a live edge always beats
	 *  a recorded one -- otherwise a replay scheduled for a hold the player has since abandoned
	 *  would land on whatever ability is running by then.
	 */
	void ReplayBufferedRelease(FGameplayTag InputTag);

	/** Presses the debug attack input, then releases it DebugAutoAttackHoldSeconds later. */
	void DebugAutoAttackPress();
	void DebugAutoAttackRelease();

	/** Teleports back to DebugAutoAttackHomeTransform and kills leftover velocity. No-op if disabled. */
	void ReturnToDebugAutoAttackHome();

	/**
	 *  Points the AI controller's focus at the nearest other pawn, or clears it. Debug only.
	 *
	 *  Re-resolved on every call rather than cached in BeginPlay, because a placed dummy is
	 *  possessed before the player pawn exists -- a focus taken at BeginPlay would be null
	 *  forever. Calling it per swing costs one overlap query every DebugAutoAttackInterval.
	 */
	void UpdateDebugFacingFocus(bool bAttacking);

	/** Bound to the ASC so the attacker returns home the moment a swing finishes. */
	void HandleDebugAutoAttackEnded(const FAbilityEndedData& EndedData);

	/** Adds StaminaRegenPerSecond * delta, unless suppressed or already full. */
	void TickStaminaRegen(float DeltaSeconds);

	/** Spends BlockDrainPerSecond * delta while BlockingTag is present. Never breaks a guard. */
	void TickBlockDrain(float DeltaSeconds);

	/**
	 *  Ends any running block. Used by the guard break and by becoming airborne.
	 *
	 *  One helper because a guard has grown three ways to end that are not the player letting go,
	 *  and each of them must also stop the drain -- which happens for free, since the drain reads
	 *  BlockingTag and the tag leaves with the ability.
	 */
	void CancelBlockAbility();

	/**
	 *  Applies whichever authored speed caps are live, and the captured default when none are.
	 *
	 *  Takes the *slowest* applicable cap rather than the last one checked, because blocking and
	 *  exhaustion can overlap. Renamed from TickBlockingMoveSpeed 2026-08-14 when exhaustion became
	 *  the second clamp; the old name would have read as though the guard owned the mechanism.
	 */
	void TickMoveSpeedClamps();

	/**
	 *  Maintains State.Blocking.Committed, and finishes a release that was held back by it.
	 *
	 *  Written as "make the tag match the current state" rather than as edges, for the same reason
	 *  the speed cap is: an edge-driven version has to be right on every entry and exit, and a
	 *  stranded commit tag would refuse every action forever with nothing on screen to explain it.
	 */
	void TickBlockCommitment(float Now);

	/** Requests a resume pass. Never performs one -- OnAbilityEnded is re-entrant. */
	void HandleAbilityEndedForResume(const FAbilityEndedData& EndedData);

	/** Re-activates abilities that opted into resuming and whose input is still held. */
	void TickResumeHeldAbilities();

	/** Applies ExhaustedTag. Removed only once stamina is back to Max -- there is no timer. */
	void EnterExhaustion();
	void ExitExhaustion();

	/**
	 *  The locally-visible half of exhaustion and death, run on every machine.
	 *
	 *  **The split is the whole point.** Enter/Exit decide *whether* the state changed and run
	 *  on the server alone, because the stamina and health delegates that drive them are bound
	 *  behind the authority gate. Apply/Clear make the state *true on this machine* -- the tag,
	 *  the ragdoll, the movement stop -- and run on the server directly and on clients from
	 *  OnRep.
	 *
	 *  Before this existed the tag was applied by the server only, and since a loose gameplay
	 *  tag does not replicate, a client's ASC never had it: its own CanActivateAbility would
	 *  pass, it would predict an action the server had already refused, and only a correction
	 *  would tell it otherwise.
	 */
	void ApplyExhaustionState();
	void ClearExhaustionState();
	void ApplyDeathState();
	void ClearDeathState();

	/**
	 *  Starts and ends the guard-break stun. Server decides; the state pair runs everywhere.
	 *
	 *  EndGuardBreak is driven from Tick against GuardBreakEndsAt rather than from a timer, so
	 *  the stun cannot outlive a pause, a seek or a slow frame in a way nothing observes.
	 */
	void EnterGuardBreak();
	void EndGuardBreak();
	void ApplyGuardBreakState();
	void ClearGuardBreakState();

	/**
	 *  Ends the blockstun lockout. Driven from Tick against BlockstunEndsAt, as the guard break is.
	 *
	 *  There is no ApplyBlockstunState pairing beyond the tag, because blockstun cancels nothing --
	 *  it refuses activations through ActivationBlockedTags and lets whatever is running finish.
	 *  That is the deliberate difference from a break, which takes the guard down with it.
	 */
	void EndBlockstun();
	void ApplyBlockstunState();
	void ClearBlockstunState();

	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_Exhausted();

	UFUNCTION()
	void OnRep_GuardBroken();

	UFUNCTION()
	void OnRep_Blockstun();

	/**
	 *  Applies State.Dead, cancels everything running, and stops the character moving.
	 *
	 *  Cancelling is the deliberate difference from exhaustion, which gates activation and
	 *  lets a running ability finish. That is right for a stamina lockout and visibly wrong
	 *  for a corpse: without it a killing blow landing mid-swing leaves the dead character
	 *  completing the attack, hitbox and all.
	 */
	void EnterDeath();

	/** Debug only: clears State.Dead, restores movement, refills health and stamina. */
	void ReviveFromDebug();

	/** Hands the mesh to physics. No-op without a physics asset -- and silent, so verify in play. */
	void StartRagdoll();

	/** Returns the mesh to the capsule at its authored offset. Exact, or the character stays skewed. */
	void StopRagdoll();

	void HandleStaminaChanged(const struct FOnAttributeChangeData& Data);
	void HandleHealthChanged(const struct FOnAttributeChangeData& Data);

	/** The one press waiting for something to answer it. Single slot: last press wins. */
	FTDBufferedInput BufferedInput;

	/** Pending replayed release. Live input for the same tag cancels it. */
	FTimerHandle BufferedReleaseTimerHandle;

	/** World time before which regen stays suppressed. Pushed out while a suppressor is active. */
	float RegenSuppressedUntil = 0.0f;

	/** True between an actual jump launch and landing. Never set by merely falling. */
	bool bJumpRegenPauseActive = false;

	/**
	 *  Replicated, because the tags they drive are *loose* tags and loose tags do not replicate.
	 *
	 *  These bools are the wire format and the tag is the local consequence: the server decides,
	 *  the bool replicates, and OnRep applies the same tag on each client that the server
	 *  applied on itself. A GameplayEffect carrying the tag would replicate on its own and is
	 *  the more usual answer, but UE 5.8 expresses effect-granted tags through gEComponents,
	 *  which cannot be scripted -- see Docs/Working-In-Unreal.md -- so it would need a human in
	 *  the editor for a mechanism the economy already owns in C++.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Exhausted)
	bool bExhausted = false;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	/** Follows the same server-decides/OnRep-applies contract as the two above. */
	UPROPERTY(ReplicatedUsing = OnRep_GuardBroken)
	bool bGuardBroken = false;

	/**
	 *  When the guard-break stun expires, in world seconds. Server-authoritative.
	 *
	 *  A timestamp checked in Tick rather than a SetTimer, deliberately: the two network-unaware
	 *  SetTimer sites this project already has are filed as a multiplayer trap, and a third would
	 *  have joined them for no gain. A timestamp is also what the regen suppressor beside it
	 *  already uses, so the stun and the pause it implies are expressed the same way.
	 */
	float GuardBreakEndsAt = 0.0f;

	/** Follows the same server-decides/OnRep-applies contract as the three above. */
	UPROPERTY(ReplicatedUsing = OnRep_Blockstun)
	bool bInBlockstun = false;

	/**
	 *  When the blockstun lockout expires, in world seconds. Server-authoritative.
	 *
	 *  A timestamp checked in Tick rather than a SetTimer, for the reason GuardBreakEndsAt gives.
	 *  Extended by taking the max, never reassigned, so a second blocked hit landing inside an
	 *  existing lockout can only ever lengthen it -- blocking two attacks must not be a way to
	 *  serve a shorter sentence than blocking the slower one alone.
	 */
	float BlockstunEndsAt = 0.0f;

	/**
	 *  True only while TickBlockDrain is writing. **This is what stops drain exhausting you.**
	 *
	 *  Exhaustion is decided by the stamina-changed delegate, which sees "the bar reached zero" and
	 *  cannot see what emptied it. That is correct for every other spender -- a dodge that empties
	 *  you should exhaust you -- and wrong for the guard's drain, which is the only *continuous*
	 *  spend in the game and is meant to park the bar at zero harmlessly.
	 *
	 *  The rule it encodes: **exhaustion is a consequence of an action or an attack, never of a
	 *  state you are holding.** A flag rather than a redesign because the delegate is right about
	 *  everything except this one case, and moving every spender onto explicit calls would trade a
	 *  narrow exception for a broad chance of forgetting one.
	 */
	bool bApplyingBlockDrain = false;

	/** MaxWalkSpeed as authored, captured before any block modifies it. */
	float DefaultMaxWalkSpeed = 0.0f;

	/**
	 *  A resume pass is owed on the next tick. Set by an ability ending and by landing.
	 *
	 *  The deferral is the fix, not an optimisation: performing the resume inside OnAbilityEnded
	 *  re-entered ability activation and double-activated the guard, leaking its spec's activeCount
	 *  and sticking it up permanently. A flag drained once per tick cannot re-enter anything.
	 */
	bool bResumePending = false;

	/** When the guard's minimum duration expires, in world seconds. */
	float BlockCommitEndsAt = 0.0f;


	/**
	 *  The mesh's authored offset from the capsule, captured once before physics ever moves it.
	 *
	 *  Ragdolling drives the mesh in world space, so the relative transform is meaningless by
	 *  the time it stops. Restoring from a value read *after* death would bake the ragdoll's
	 *  final pose in as the new rest offset, and the character would stand up crooked and stay
	 *  that way. Captured in BeginPlay rather than the constructor so it reflects whatever a
	 *  Blueprint authored.
	 */
	FTransform MeshRestRelativeTransform;

	bool bRagdollActive = false;

	FTimerHandle DebugReviveTimerHandle;

	/**
	 *  Seeded-once flag for the *owned* ASC only -- i.e. the training dummy's.
	 *
	 *  A player's flag lives on ATDPlayerState instead, because that is where its ASC lives.
	 *  Splitting them is not tidiness: a player pawn's BeginPlay runs before possession, so a
	 *  single flag on the character would be spent seeding the fallback ASC and the real one
	 *  would never be seeded at all. See ATDPlayerState::HasSeededDefaults.
	 */
	bool bOwnedDefaultsApplied = false;

	FTimerHandle DebugAutoAttackTimerHandle;
	FTimerHandle DebugAutoAttackReleaseTimerHandle;
	FTimerHandle DebugAutoAttackResetTimerHandle;

	/** Where the auto-attacker started, captured once, so each swing begins from the same spot. */
	FTransform DebugAutoAttackHomeTransform;
};
