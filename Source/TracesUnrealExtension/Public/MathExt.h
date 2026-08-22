#pragma once

#include "CoreMinimal.h"

#include <type_traits>

namespace MathExt
{
	template<typename T>
	FORCEINLINE T IncrementWrap(T X, T MinValue, T MaxValue)
	{
		if (X >= MaxValue)
		{
			return MinValue;
		}
		return X + 1;
	}
	
	template<typename T>
	FORCEINLINE T DecrementWrap(T X, T MinValue, T MaxValue)
	{
		if (X <= MinValue)
		{
			return MaxValue;
		}
		return X - 1;
	}
	
	template<typename T>
	FORCEINLINE T AddWrap (T Val, T AddAmt, T Min, T Max)
	{
		static_assert(std::is_integral_v<T> && sizeof(T) < 8);
		check(Val >= Min && Val <= Max && Max >= Min);
		
		const int64 Sum = (int64)Val + (int64)AddAmt;
		int64 OutValue = Sum;
		const int64 MinMaxDiff = (int64)Max - (int64)Min + 1;
		
		if (Sum > Max)
		{
			const int64 SumMaxDiff = Sum - (Max + 1); // adding 1 makes the rest simpler
			const int64 Remainder = SumMaxDiff % MinMaxDiff;
			OutValue = Min + Remainder;
		} 
		else if (Sum < Min)
		{
			const int64 MinSumDiff = (Min - 1) - Sum;
			const int64 Remainder = MinSumDiff % MinMaxDiff;
			OutValue = Max - Remainder;
		}
		
		return OutValue;
	}
	
	FORCEINLINE FVector ToVec3(const FVector2D& Vec2)
	{
		return {Vec2.X, Vec2.Y, 0};
	}
	
	FORCEINLINE FVector2D ToVec2(const FVector& Vec3)
	{
		return {Vec3.X, Vec3.Y};
	}
	
	FORCEINLINE FVector Add(const FVector& Vec3, const FVector2D& Vec2)
	{
		return {Vec3.X + Vec2.X, Vec3.Y + Vec2.Y, Vec3.Z};
	}
	
	FORCEINLINE FVector Sub(const FVector& Vec3, const FVector2D& Vec2)
	{
		return {Vec3.X - Vec2.X, Vec3.Y - Vec2.Y, Vec3.Z};
	}
}

FORCEINLINE FVector operator+(const FVector& Vec3, const FVector2D& Vec2)
{
	return MathExt::Add(Vec3, Vec2);
}

FORCEINLINE FVector operator-(const FVector& Vec3, const FVector2D& Vec2)
{
	return MathExt::Sub(Vec3, Vec2);
}