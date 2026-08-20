#pragma once

#include "Runtime/Engine/Classes/Engine/HitResult.h"
#include "Math/Color.h"

class UWorld;

namespace DebugDrawExt
{
	TRACESUNREALEXTENSION_API void HitResult(const UWorld* World, const FHitResult& HitResult, float Time=-1, FColor MissLineColor=FColor::Red, FColor HitLineColor=FColor::Green, FColor HitCircleColor=FColor::Green);
}
