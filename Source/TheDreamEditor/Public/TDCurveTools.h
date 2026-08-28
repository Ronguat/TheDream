#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDCurveTools.generated.h"

class UCurveFloat;
class UCurveVector;

/**
 *  Key authoring for curve assets, which no scripting surface reaches.
 *
 *  UCurveFloat::FloatCurve and UCurveVector::FloatCurves are bare UPROPERTY()s the reflection layer
 *  cannot see, and neither type carries an AddKey UFUNCTION -- so a script can create the asset and
 *  cannot fill it. These write the keys and nothing else; creation stays with AssetTools.
 */
UCLASS()
class THEDREAMEDITOR_API UTDCurveTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 *  Replaces every key on a float curve. Times and Values must be the same length and at least
	 *  one pair long; the curve is left untouched and false returned otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category="TheDream|Curves")
	static bool SetFloatCurveKeys(UCurveFloat* Curve, const TArray<float>& Times, const TArray<float>& Values, bool bAutoTangents = true);

	/** Replaces every key on a vector curve, writing one key per component at each time. */
	UFUNCTION(BlueprintCallable, Category="TheDream|Curves")
	static bool SetVectorCurveKeys(UCurveVector* Curve, const TArray<float>& Times, const TArray<FVector>& Values, bool bAutoTangents = true);

	/**
	 *  Mean of a float curve, sampled evenly across its own time range. The strength-curve contract
	 *  is a mean of 1.0; a time-mapping curve's mean means nothing, so read this only for the first.
	 */
	UFUNCTION(BlueprintCallable, Category="TheDream|Curves")
	static float GetFloatCurveMean(const UCurveFloat* Curve, int32 Samples = 100);
};
