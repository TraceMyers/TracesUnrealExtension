#pragma once

#include "CoreMinimal.h"
#include "MathExt.h"

template<typename T, int BufferCount, template<typename...> typename ArrayType=TArray>
struct TMultiBuffer
{
	FORCEINLINE ArrayType<T>& GetCurrent()
	{
		return Buffers[CurrentBuffer];
	}
	
	FORCEINLINE const ArrayType<T>& GetCurrent() const
	{
		return Buffers[CurrentBuffer];
	}
	
	FORCEINLINE ArrayType<T>& GetNext()
	{
		return Buffers[MathExt::IncrementWrap(CurrentBuffer, 0, BufferCount-1)];
	}
	
	FORCEINLINE const ArrayType<T>& GetNext() const
	{
		return Buffers[MathExt::IncrementWrap(CurrentBuffer, 0, BufferCount-1)];
	}
	
	FORCEINLINE ArrayType<T>& GetPrev()
	{
		return Buffers[MathExt::DecrementWrap(CurrentBuffer, 0, BufferCount-1)];
	}
	
	FORCEINLINE const ArrayType<T>& GetPrev() const
	{
		return Buffers[MathExt::DecrementWrap(CurrentBuffer, 0, BufferCount-1)];
	}
	
	void Empty(bool bKeepSlack=false)
	{
		for (int i = 0; i < BufferCount; i++)
		{
			const int32 Slack = bKeepSlack ? Buffers[i].Max() : 0;
			Buffers[i].Empty(Slack);
		}
	}
	
	void Flip(bool bEmptyBeforeFlip=true, bool bKeepSlackIfEmpty=true)
	{
		if (bEmptyBeforeFlip)
		{
			ArrayType<T>& Cur = GetCurrent();
			if (bKeepSlackIfEmpty)
			{
				Cur.Empty(Cur.Max());
			}
			else
			{
				Cur.Empty(0);
			}
		}
		CurrentBuffer = MathExt::IncrementWrap(CurrentBuffer, 0, BufferCount-1);
	}
	
	TStaticArray<ArrayType<T>, BufferCount> Buffers;
	int32 CurrentBuffer = 0;	
};