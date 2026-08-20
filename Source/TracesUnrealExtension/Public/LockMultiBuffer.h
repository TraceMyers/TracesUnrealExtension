#pragma once

#include "LockExt.h"
#include "LockBuffer.h"
#include "MathExt.h"

// swap buffer (usually called double or triple buffering) with locking
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
        FQScopeLock<EQLockMode::Read> ScopedLock(&FlipLock);
        return GetBuffer(Buffer).Get(i);
    }

    FORCEINLINE void Push(const T& Value, size_t Buffer)
    {
        FQScopeLock<EQLockMode::Read> ScopedLock(&FlipLock);
        GetBuffer(Buffer).Push(Value);
    }

    FORCEINLINE void Emplace(T&& Value, size_t Buffer)
    {
        FQScopeLock<EQLockMode::Read> ScopedLock(&FlipLock);
        GetBuffer(Buffer).Emplace(std::move(Value));
    }

    FORCEINLINE void Empty(size_t Buffer)
    {
        FQScopeLock<EQLockMode::Read> ScopedLock(&FlipLock);
        GetBuffer(Buffer).Empty();
    }

    FORCEINLINE TArray<T>::SizeType Num(size_t Buffer) const
    {
        FQScopeLock<EQLockMode::Read> ScopedLock(&FlipLock);
        return GetBuffer(Buffer).Num();
    }

	void Empty()
	{
        FQScopeLock<EQLockMode::Read> ScopedLock(&FlipLock);
		for (size_t i = 0; i < BufferCount; i++)
		{
			Buffers[i].Empty();
		}
	}
	
	void Flip(bool bEmptyBeforeFlip=true)
	{
        FQScopeLock ScopedLock(&FlipLock);
		if (bEmptyBeforeFlip)
		{
            Buffers[CurrentBuffer].Clear();
		}
		CurrentBuffer = MathExt::IncrementWrap(CurrentBuffer, 0, BufferCount-1);
	}
	
protected:
    
    TLockBuffer<T>& GetBuffer(size_t Buffer)
    {
        const size_t BufIndex = (CurrentBuffer + Buffer) % BufferCount;
        return Buffers[BufIndex];
    }
    
    // in the case of using this lock, a 'write' is flipping the buffers
    // and a 'read' is using any individual buffer
    mutable FQLock FlipLock;
    TStaticArray<TLockBuffer<T>, BufferCount> Buffers;
	int32_t CurrentBuffer = 0;
};
