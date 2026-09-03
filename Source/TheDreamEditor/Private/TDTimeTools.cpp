#include "TDTimeTools.h"

#include "Misc/App.h"

bool UTDTimeTools::SetFixedTimeStep(bool bEnabled, float DeltaSeconds)
{
	if (bEnabled && DeltaSeconds > 0.0f)
	{
		FApp::SetFixedDeltaTime(static_cast<double>(DeltaSeconds));
	}
	FApp::SetUseFixedTimeStep(bEnabled);
	return FApp::UseFixedTimeStep();
}

bool UTDTimeTools::IsFixedTimeStep()
{
	return FApp::UseFixedTimeStep();
}

float UTDTimeTools::GetFixedDeltaTime()
{
	return static_cast<float>(FApp::GetFixedDeltaTime());
}
