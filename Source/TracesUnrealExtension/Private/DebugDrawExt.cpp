
#include "DebugDrawExt.h"
#include "Runtime/Engine/Classes/Engine/HitResult.h"
#include "Runtime/Engine/Public/DrawDebugHelpers.h"
#include "GameFramework/Actor.h"

void DebugDrawExt::HitResult(const UWorld* World, const FHitResult& HitResult, float Time, FColor MissLineColor, FColor HitLineColor, FColor HitCircleColor)
{
	DrawDebugPoint(World, HitResult.TraceStart, 5, FColor::Yellow, false, Time);
	if (HitResult.bBlockingHit)
	{
		DrawDebugLine(World, HitResult.TraceStart, HitResult.ImpactPoint, HitLineColor, false, Time, 0, 3);
		if (HitResult.GetActor())
		{
			const FBox HitActorBox = HitResult.GetActor()->GetComponentsBoundingBox();
			DrawDebugBox(World, HitActorBox.GetCenter(), HitActorBox.GetExtent(), FColor::Magenta, false, Time, 0, 4);
		}
		
		const FVector DifferentEnoughAxis = (HitResult.ImpactNormal | FVector::ZAxisVector) > 0.99 ? FVector::XAxisVector : FVector::ZAxisVector;
		const FVector XAxis = HitResult.ImpactNormal.Cross(DifferentEnoughAxis).GetSafeNormal();
		const FVector YAxis = HitResult.ImpactNormal.Cross(XAxis).GetSafeNormal();
		
		DrawDebugCircle(World, HitResult.ImpactPoint, 50, 16, HitCircleColor, false, Time, 0, 2, XAxis, YAxis);
	}
	else
	{
		DrawDebugLine(World, HitResult.TraceStart, HitResult.TraceEnd, MissLineColor, false, Time, 0, 3);
	}
}