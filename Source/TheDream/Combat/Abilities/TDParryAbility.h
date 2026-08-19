// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDParryAbility.generated.h"

/**
 *  A read. Opens a short window in which any incoming melee hit is negated outright.
 *
 *  **The identity is "I called that!"** -- these fights are a conversation between two players, and
 *  the parry is the button that says you heard the sentence before it finished. Everything about
 *  its shape follows from protecting that signal: it is a standalone input rather than a modifier
 *  on block, because a system that can mint *counterfeit* calls debases the only currency the game
 *  trades in. Release-to-parry, tap-parry and chamber-on-LMB were each rejected on that ground and
 *  the mechanisms are in Docs/Combat-Decisions.md, 2026-08-18.
 *
 *  **Success is derived, not authored, and that is the whole reward design.** A parried attacker is
 *  planted at zero distance by the existing lunge stop and rides their own attack into recovery --
 *  which is already the punish window. So the reward scales with the victim's commitment for free:
 *  a parried charged is a near-guarantee, a parried light much less. The spec's old 500 ms
 *  offensive lock re-derives to *deleted*, subsumed by per-tier recovery with better texture.
 *  **A parry is a dodge that stands still**: it manufactures the whiff at zero centimetres, which
 *  under feel goal #1 makes it the whiff-punish maximiser.
 *
 *  The three numbers the designer did author are the window, the whiff lockout and the stamina
 *  reward. Activation itself costs **nothing**, completing a pricing symmetry the project had
 *  never stated: the dodge is stamina-priced, block is priced in both ledgers, and parry is purely
 *  time-priced.
 *
 *  **The window is mechanical, never a notify.** It is a timestamp on the character checked in
 *  Tick, so the montage is purely visual and can be swapped, retimed or removed without touching
 *  what the parry does. A notify would make the animation the authority over a hit-negation
 *  window, which is the mistake Docs/Combat-Spec.md spent the ladder learning not to make.
 *
 *  **No facing test.** 360 degrees, deliberately: a parry is a read of *timing*, and requiring the
 *  read to also be spatial would price two skills into one input.
 *
 *  Deliberately **not** refused by State.Blockstun -- blockstun and parry never know about each
 *  other (the designer, 2026-08-15). It *is* refused while blocking, which is a property of the
 *  guard rather than of blockstun: you cannot call a read you are already hiding from.
 */
UCLASS(abstract)
class UTDParryAbility : public UTDGameplayAbility
{
	GENERATED_BODY()

public:

	UTDParryAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 *  Never. **A replayed parry is a mistimed parry.**
	 *
	 *  The buffer exists so a deliberate tap is not lost to a brief lockout, and that reasoning
	 *  holds for actions whose value does not depend on *when* they land. A parry's entire value is
	 *  when it lands: replaying one a hundred milliseconds late does not rescue the read, it
	 *  manufactures a window the player never asked for at a moment they did not choose -- and
	 *  then charges them the whiff lockout for it.
	 *
	 *  It is also the anti-counterfeit rule applied to the input layer. A buffered parry is a call
	 *  the player did not make, which is exactly the false positive the whole input scheme was
	 *  chosen to prevent.
	 */
	virtual bool ShouldBufferFailedInput(const FGameplayAbilityActorInfo* ActorInfo) const override;

protected:

	/**
	 *  How long the negation window stays open, from activation.
	 *
	 *  **Fenced at both ends, and neither bound is free** -- both are tuning-map invariants.
	 *
	 *  *Ceiling:* one press must not cover two read-classes, or the parry becomes an option-select
	 *  and stops being a read at all. Under the re-poled ladder the binding gap is fast-to-charged,
	 *  750 - 350 = 400 ms, so the window must stay below that. **300 is legal only because of the
	 *  re-pole**; under the old three-tier ladder the ceiling was 250 and this value would have
	 *  violated it.
	 *
	 *  *Floor:* the window must be at least the longest authored ReleaseSeconds -- 0.15 today --
	 *  or an attack's damaging phase could span the whole window and emerge the other side
	 *  unparried, which reads as the parry simply not working.
	 *
	 *  Covering [t, t+300] catches both members of the fast layer, 200 and 350, under a single
	 *  read. That is the intended grain rather than a side effect.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.0"))
	float ParryWindowSeconds = 0.30f;

	/**
	 *  How long defensive activations are refused after a window closes without catching anything.
	 *
	 *  **This is the whole price of the input**, since activation costs no stamina -- the parry is
	 *  time-priced and this is the time. Applied as State.ParryLockout, which the dodge's tail
	 *  shares; see that tag.
	 *
	 *  Floored by a constraint rather than chosen for feel: a whiff timed against the *fast* layer
	 *  must stay locked through the charged's arrival, or reading "fast" wrongly costs nothing and
	 *  the charged can never collect on it. A press at ~150 must still be locked at 750, which is
	 *  what re-derived it down from the spec's original 1000.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.0"))
	float ParryWhiffLockoutSeconds = 0.60f;
};
