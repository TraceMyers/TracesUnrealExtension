#pragma once

#include "CoreMinimal.h"

// simple raycast procedures for when you need to raycast to specific basic shapes
// can potentially save effort and time compared to using Unreal raycasts.

struct FTinyRaycastResult
{
	FVector HitLocation = FVector(0);
	double HitDistance = FLT_MAX;
	bool bIsHit = false;
	bool bHitBackFace = false;
};

FORCEINLINE bool RaycastPlane(FTinyRaycastResult& Result, const FVector& RayOrigin, const FVector& RayDirection, const FVector& PointOnPlane, const FVector& PlaneNormal)
{
	const FVector DiffToPlane		   = PointOnPlane - RayOrigin;
	const double SignedDistanceToPlane = DiffToPlane | PlaneNormal;
	const double PointingCosT		   = PlaneNormal | RayDirection;
	Result.HitDistance				   = SignedDistanceToPlane / PointingCosT;
	Result.HitLocation				   = RayOrigin + RayDirection * Result.HitDistance;
	const bool bIsNanOrInf = isinf(Result.HitDistance) || isnan(Result.HitDistance);
	// always check bIsHit before trusting other results.
	Result.bIsHit					   = (Result.HitDistance >= 0) && (PointingCosT != 0) && !bIsNanOrInf; 
	Result.bHitBackFace				   = SignedDistanceToPlane > 0;
	return Result.bIsHit;
}

TRACESUNREALEXTENSION_API bool RaycastTriangle(FTinyRaycastResult& Result, const FVector& RayOrigin, const FVector& RayDirection, const FVector& TriangleA, const FVector& TriangleB, const FVector& TriangleC, FVector* OptionalOutNormal = nullptr);

TRACESUNREALEXTENSION_API bool RaycastCircle(FTinyRaycastResult& Result, const FVector& RayOrigin, const FVector& RayDirection, const FVector& CircleCenter, const double CircleRadius, const FVector& CircleNormal);

TRACESUNREALEXTENSION_API bool RaycastRectangle(FTinyRaycastResult& Result, const FVector& RayOrigin, const FVector& RayDirection, const FVector& RectCenter, double RectHalfWidth, double RectHalfLength, const FVector& RectWidthDirection, const FVector& RectLengthDirection, const FVector& RectNormal);