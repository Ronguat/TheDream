// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Combat/Abilities/TDGameplayAbility.h"
#include "TDParryAbility.generated.h"

class UAnimMontage;

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
 *  The three numbers the designer did author are the window, the whiff recovery and the stamina
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
	 *  then charges them the whiff recovery for it.
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
	 *  How long **every** ability is refused after a window closes without catching anything.
	 *
	 *  **This is the whole price of the input**, since activation costs no stamina -- the parry is
	 *  time-priced and this is the time. Applied as State.ParryRecovery.
	 *
	 *  Floored by a constraint rather than chosen for feel: a whiff timed against the *fast* layer
	 *  must stay locked through the charged's arrival, or reading "fast" wrongly costs nothing and
	 *  the charged can never collect on it. A press at ~150 must still be locked at 750, which is
	 *  what re-derived it down from the spec's original 1000.
	 *
	 *  ***The floor above is unchanged by 2026-08-19; what it buys is not.*** It was derived when
	 *  this refused only *defensive* activations, so it priced a whiff at "you cannot defend". The
	 *  designer's ruling that a whiffed parry must prevent acting widened it to "you cannot do
	 *  anything", and GA_Parry now stays alive across it holding the movement lock. The constraint
	 *  still binds -- it is a floor, and a stricter refusal cannot violate it -- but 0.60 is now a
	 *  materially harsher punish than the number chosen for the narrow reading. Whether it is still
	 *  the right cost is a feel question and belongs to play, not to arithmetic.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry", meta=(ClampMin="0.0"))
	float ParryWhiffRecoverySeconds = 0.60f;

	/**
	 *  The clip. Purely cosmetic, and the parry works correctly with this unset.
	 *
	 *  **Fitted to the authored values in two segments, never the reverse** (the designer,
	 *  2026-08-19). A Parry Gesture notify on the montage marks where the gesture reads; the
	 *  segment before it is played across ParryWindowSeconds and the segment after it across
	 *  ParryWhiffRecoverySeconds, each at its own derived rate. A single uniform rate would only
	 *  align both if the marker happened to sit at exactly window/(window+recovery) of the clip --
	 *  1/3 at today's numbers -- so two rates is what makes the fit hold wherever it is placed.
	 *
	 *  **With no marker, the whole clip plays across the total at one rate and warns.** That is a
	 *  legible fallback rather than a correct one: notify placement cannot be read off the asset,
	 *  so the warning is the only thing that can tell you the marker is missing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Combat|Parry")
	TObjectPtr<UAnimMontage> ParryMontage;

	/**
	 *  Switches the montage to its recovery rate when the gesture marker passes.
	 *
	 *  Driven by the notify rather than by a timer set at activation, though the two coincide by
	 *  construction: the marker is reached at ParryWindowSeconds precisely because the first
	 *  segment's rate was derived to make that true. Taking it from the montage's own playhead
	 *  means the switch stays correct if anything perturbs playback, and it costs nothing --
	 *  the notify has to exist anyway to carry the geometry.
	 */
	UFUNCTION()
	void HandleParryGesture(FGameplayEventData Payload);

private:

	/** Starts the clip at the window segment's derived rate. Silent and harmless with no montage. */
	void PlayParryMontage();

	/**
	 *  Trigger time of the Parry Gesture marker on ParryMontage, or -1 if there is none.
	 *
	 *  Read straight off UAnimMontage::Notifies, which **C++ can see even though the MCP toolset
	 *  cannot** -- the "notifies are unreadable" limit in Docs/Working-In-Unreal.md is a fact about
	 *  the toolset, not about the engine. That is what lets the rates be derived at activation
	 *  rather than discovered when the notify fires.
	 */
	float FindGestureTime() const;

	/** The montage this activation is playing, so a stray gesture event from elsewhere is ignored. */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveParryMontage;
};
