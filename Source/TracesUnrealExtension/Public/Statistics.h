#pragma once 

#include "Containers/Array.h"
#include "Containers/Map.h"

#include <limits.h>

// I often like making game AI decisions based on summary statistics.
// some of these things are available in Kismet, but that's kind of a heavy dependency you may not want.
namespace StatsExt
{
	template <typename T>
	T Sum(const TArray<T>& Array)
	{
		T ItemSum = 0;		
		for (const T Item : Array)
		{
			ItemSum += Item;
		}
		return ItemSum;
	}
	
	template <typename T>
	double Mean(const TArray<T>& Array)
	{
		check(Array.Num() > 0)
		return Sum(Array) / (double)Array.Num();
	}
	
	template <typename T>
	T Min(const TArray<T>& Array)
	{
		check(Array.Num() > 0)
		T MinItem = std::numeric_limits<T>::max();
		for (const T& Item : Array)
		{
			MinItem = FMath::Min(MinItem, Item);
		}
		return MinItem;
	}
		
	template <typename T>
	T Max(const TArray<T>& Array)
	{
		check(Array.Num() > 0)
		T MaxItem = std::numeric_limits<T>::min();
		for (const T& Item : Array)
		{
			MaxItem = FMath::Max(MaxItem, Item);
		}
		return MaxItem;
	}

	template <typename T>
	double SortAndGetMedian(TArray<T>& Array)
	{
		check(Array.Num() > 0)
		if (Array.Num() == 1)
		{
			return Array[0];
		}
		if (Array.Num() == 2)
		{
			return (Array[0] + Array[1]) * 0.5;
		}
		
		Array.Sort([](const T& A, const T& B) { return A < B; });
		const size_t HalfCount = Array.Num() / 2;
		
		if (Array.Num() % 2 == 0)
		{
			return (Array[HalfCount] + Array[HalfCount-1]) * 0.5;
		}
		else
		{
			return Array[HalfCount];
		}
	}
	
	template <typename T>
	double Median(const TArray<T>& Array)
	{
		TArray<T> ArrayCopy = Array;
		return SortAndGetMedian(ArrayCopy);
	}
	
	template <typename T>
	T Mode(const TArray<T>& Array)
	{
		check(Array.Num() > 0)
		
		TMap<T, int64> Counts;
		for (const T& Item : Array)
		{
			int64* Count = Counts.Find(Item);
			if (Count)
			{
				*Count += 1;
			}
			else
			{
				Counts.Add(Item, 1);
			}
		}
		
		Counts.ValueSort([](const int64& A, const int64& B){ return A > B; });
		
		TArray<T> SortedKeys;
		Counts.GenerateKeyArray(SortedKeys);
		
		return SortedKeys[0];
	}
	
	template<typename T>
	double SumOfSquareError(const TArray<T>& Array)
	{
		double ArrayMean = Mean(Array);
		double SSE = 0;
		for (const T Item : Array)
		{
			SSE += FMath::Square(Item - ArrayMean);
		}
		return SSE;
	}
	
	// set 'bSampleVariance' to true if this is a sample representing a population
	// rather than the population itself. if this array contains all data points, it's
	// the population. if it's a subset, it's a population sample.
	template<typename T>
	double Variance(const TArray<T>& Array, bool bSampleVariance=true)
	{
		if (Array.Num() == 0 || (bSampleVariance && Array.Num() == 1))
		{
			return 0;
		}
		
		const double SampleSize = bSampleVariance ? Array.Num() - 1 : Array.Num();
		return SumOfSquareError(Array) / SampleSize;
	}
	
	// set 'bSampleVariance' to true if this is a sample representing a population
	// rather than the population itself. if this array contains all data points, it's
	// the population. if it's a subset, it's a population sample.
	template<typename T>
	double StandardDeviation(const TArray<T>& Array, bool bSampleVariance=true)
	{
		const double ArrayVariance = Variance(Array, bSampleVariance);
		if (ArrayVariance == 0)
		{
			return 0;
		}
		return FMath::Sqrt(ArrayVariance);
	}
}