#pragma once

#include "LockBuffer.h"
#include "MathExt.h"

// swap buffer (usually called double or triple buffering) with locking
// assumes user is assigning one buffer per thread. a little unsafe in that way, but simple.
template<typename T, int BufferCount>
struct TLockMultiBuffer
{
    TLockMultiBuffer() = default;
    TLockMultiBuffer(const TLockMultiBuffer&) = delete;
    TLockMultiBuffer& operator=(const TLockMultiBuffer&) = delete;
    TLockMultiBuffer(TLockMultiBuffer&& Other) = delete;
    TLockMultiBuffer& operator=(TLockMultiBuffer&& Other) = delete;

    FORCEINLINE T Get(TArray<T>::SizeType i, size_t Buffer) const
    {
    	FlipLock.ReadLock();
        const T Value = GetBuffer(Buffer).Get(i);
    	FlipLock.ReadUnlock();
    	return Value;
    }

    FORCEINLINE void Push(const T& Value, size_t Buffer)
    {
    	FlipLock.ReadLock();
        GetBuffer(Buffer).Push(Value);
    	FlipLock.ReadUnlock();
    }

    FORCEINLINE void Emplace(T&& Value, size_t Buffer)
    {
    	FlipLock.ReadLock();
        GetBuffer(Buffer).Emplace(std::move(Value));
    	FlipLock.ReadUnlock();
    }

    FORCEINLINE void Empty(size_t Buffer)
    {
    	FlipLock.ReadLock();
        GetBuffer(Buffer).Empty();
    	FlipLock.ReadUnlock();
    }

    FORCEINLINE TArray<T>::SizeType Num(size_t Buffer) const
    {
    	FlipLock.ReadLock();
        const size_t ArrayNum = GetBuffer(Buffer).Num();
    	FlipLock.ReadUnlock();
    	return ArrayNum;
    }

	void Empty()
	{
    	FlipLock.ReadLock();
		for (size_t i = 0; i < BufferCount; i++)
		{
			Buffers[i].Empty();
		}
    	FlipLock.ReadUnlock();
	}
	
	void Flip(bool bEmptyBeforeFlip=true)
	{
    	FlipLock.WriteLock();
		if (bEmptyBeforeFlip)
		{
            Buffers[CurrentBuffer].Empty();
		}
		CurrentBuffer = MathExt::IncrementWrap(CurrentBuffer, 0, BufferCount-1);
    	FlipLock.WriteUnlock();
	}
	
protected:
    
    TLockBuffer<T>& GetBuffer(size_t Buffer)
    {
        const size_t BufIndex = (CurrentBuffer + Buffer) % BufferCount;
        return Buffers[BufIndex];
    }
	    
	const TLockBuffer<T>& GetBuffer(size_t Buffer) const
    {
    	const size_t BufIndex = (CurrentBuffer + Buffer) % BufferCount;
    	return Buffers[BufIndex];
    }
    
    // in the case of using this lock, a 'write' is flipping the buffers
    // and a 'read' is using any individual buffer
    mutable FRWLock FlipLock;
    TStaticArray<TLockBuffer<T>, BufferCount> Buffers;
	int32_t CurrentBuffer = 0;
};
