#include "TinyRaycast.h"

bool RaycastTriangle(FTinyRaycastResult& Result, const FVector& RayOrigin, const FVector& RayDirection, const FVector& TriangleA, const FVector& TriangleB, const FVector& TriangleC, FVector* OptionalNormal)
{
	FVector LocalNormal;
	FVector* OutNormal = OptionalNormal ? OptionalNormal : &LocalNormal;
	
	const FVector AB = TriangleB - TriangleA;
	const FVector AC = TriangleC - TriangleA;
	*OutNormal = AC.Cross(AB).GetSafeNormal();
	
	if (RaycastPlane(Result, RayOrigin, RayDirection, TriangleA, *OutNormal))
	{
		const FVector AC_ScaledNormal = OutNormal->Cross(AC);
		const FVector CB_ScaledNormal = OutNormal->Cross(TriangleB - TriangleC);
		const FVector BA_ScaledNormal = OutNormal->Cross(-AB);
		const FVector AH = Result.HitLocation - TriangleA;
		const FVector BH = Result.HitLocation - TriangleB;
		// each triangle edge is on a line bisecting the plane. check the point is on the side the triangle is on.
		if ((AC_ScaledNormal | AH) < 0 || (CB_ScaledNormal | BH) < 0 || (BA_ScaledNormal | BH) < 0)
		{
			Result = {};
			return false;
		}
		return true;
	}
	return false;
}

bool RaycastCircle(FTinyRaycastResult& Result, const FVector& RayOrigin, const FVector& RayDirection,
	const FVector& CircleCenter, const double CircleRadius, const FVector& CircleNormal)
{
	if (RaycastPlane(Result, RayOrigin, RayDirection, CircleCenter, CircleNormal))
	{
		if (FVector::DistSquared(CircleCenter, Result.HitLocation) > FMath::Square(CircleRadius))
		{
			Result = {};
			return false;
		}
		return true;
	}
	return false;
}

bool RaycastRectangle(FTinyRaycastResult& Result, const FVector& RayOrigin, const FVector& RayDirection,
	const FVector& RectCenter, double RectHalfWidth, double RectHalfLength, const FVector& RectWidthDirection,
	const FVector& RectLengthDirection, const FVector& RectNormal)
{
	if (RaycastPlane(Result, RayOrigin, RayDirection, RectCenter, RectNormal))
	{
		const FVector TowardHitDiff = Result.HitLocation - RectCenter;
		const double WidthDist = RectWidthDirection | TowardHitDiff;
		if (FMath::Abs(WidthDist) > RectHalfWidth)
		{
			Result = {};
			return false;
		}
		const double LengthDist = RectLengthDirection | TowardHitDiff;
		if (FMath::Abs(LengthDist) > RectHalfLength)
		{
			Result = {};
			return false;
		}
		return true;
	}
	return false;
}