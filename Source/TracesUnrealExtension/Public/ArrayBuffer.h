#pragma once

#include "CoreMinimal.h"

// for referring to a sub-array within a TArrayBuffer
struct FBufferIndexer
{
	bool operator==(const FBufferIndexer& ColorIndexer) const = default;

	int32 Index = 0;
	int32 Count = 0;
}; 

// for those cases where you can store a bunch of arrays of uniform element type in one long buffer,
// using this is can often be an easy win for allocator costs and cache perf.
template<typename T>
class TArrayBuffer : public TArray<T>
{
public:
	FBufferIndexer BufAppend(const TArray<T>& Items)
	{
		return BufAppend(TArrayView<const T>(Items));
	}
	
	FBufferIndexer BufAppend(const TArrayView<const T> Items)
	{
		if (Items.Num() == 0)
		{
			return {0, 0};
		}
		this->Append(Items);
		return {this->Num() - Items.Num(), Items.Num()};	
	}
	
	FBufferIndexer BufAppend(T* Items, size_t Count)
	{
		TArrayView<T> View(Items, Count);
		this->Append(View);
		return {this->Num() - Count, Count};
	}
	
	TArrayView<T> BufGetView(FBufferIndexer Indexer)
	{
		return TArrayView<T>(this->GetData(), this->Num()).Slice(Indexer.Index, Indexer.Count);
	}
};