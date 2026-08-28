#include "TDCurveTools.h"

#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"

namespace
{
	/** Rewrites one FRichCurve from parallel arrays, tangents auto unless asked otherwise. */
	void WriteRichCurve(FRichCurve& Rich, const TArray<float>& Times, const TArray<float>& Values, bool bAutoTangents)
	{
		Rich.Reset();
		for (int32 i = 0; i < Times.Num(); ++i)
		{
			const FKeyHandle Handle = Rich.AddKey(Times[i], Values[i]);
			Rich.SetKeyInterpMode(Handle, bAutoTangents ? RCIM_Cubic : RCIM_Linear);
			if (bAutoTangents)
			{
				Rich.SetKeyTangentMode(Handle, RCTM_Auto);
			}
		}
		Rich.AutoSetTangents();
	}
}

bool UTDCurveTools::SetFloatCurveKeys(UCurveFloat* Curve, const TArray<float>& Times, const TArray<float>& Values, bool bAutoTangents)
{
	if (!Curve || Times.Num() == 0 || Times.Num() != Values.Num())
	{
		return false;
	}

	Curve->Modify();
	WriteRichCurve(Curve->FloatCurve, Times, Values, bAutoTangents);
	Curve->MarkPackageDirty();
	return true;
}

bool UTDCurveTools::SetVectorCurveKeys(UCurveVector* Curve, const TArray<float>& Times, const TArray<FVector>& Values, bool bAutoTangents)
{
	if (!Curve || Times.Num() == 0 || Times.Num() != Values.Num())
	{
		return false;
	}

	Curve->Modify();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		TArray<float> Component;
		Component.Reserve(Values.Num());
		for (const FVector& V : Values)
		{
			Component.Add(static_cast<float>(V[Axis]));
		}
		WriteRichCurve(Curve->FloatCurves[Axis], Times, Component, bAutoTangents);
	}
	Curve->MarkPackageDirty();
	return true;
}

float UTDCurveTools::GetFloatCurveMean(const UCurveFloat* Curve, int32 Samples)
{
	if (!Curve || Samples < 2)
	{
		return 0.0f;
	}

	float MinTime = 0.0f;
	float MaxTime = 1.0f;
	Curve->GetTimeRange(MinTime, MaxTime);
	if (FMath::IsNearlyEqual(MinTime, MaxTime))
	{
		return Curve->GetFloatValue(MinTime);
	}

	// Trapezoidal, so the endpoints carry half weight and a linear ramp reads its exact mean.
	double Total = 0.5 * (Curve->GetFloatValue(MinTime) + Curve->GetFloatValue(MaxTime));
	for (int32 i = 1; i < Samples; ++i)
	{
		Total += Curve->GetFloatValue(MinTime + (MaxTime - MinTime) * i / Samples);
	}
	return static_cast<float>(Total / Samples);
}
