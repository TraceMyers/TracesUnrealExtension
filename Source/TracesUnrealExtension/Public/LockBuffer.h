#pragma once

#include "LockExt.h"

// a single buffer made for thread-safe usage, with the functionality to lock once and use 
// extensively where needed.
template<typename T>
struct TLockBuffer
{
	TLockBuffer() = default;
	TLockBuffer(const TLockBuffer&) = delete;
	TLockBuffer& operator=(const TLockBuffer&) = delete;
	TLockBuffer(TLockBuffer&& Other) = delete;
	TLockBuffer& operator=(TLockBuffer&& Other) = delete;

	FORCEINLINE T Get(TArray<T>::SizeType i) const
	{
        FQScopeLock<EQLockMode::Read> ScopedLock(&RwLock);
		return Array[i];
	}

	FORCEINLINE TArray<T>& GetArrayLocked()
	{
		check(OwnedByThread.load(std::memory_order_acquire) == FPlatformTLS::GetCurrentThreadId());
		return Array;
	}
	
	FORCEINLINE void PushUnique(const T& Value)
	{
		FQScopeLock ScopeLock(&RwLock);
		Array.AddUnique(Value);
	}

	FORCEINLINE void Push(const T& Value)
	{
        FQScopeLock ScopedLock(&RwLock);
		Array.Push(Value);
	}

	FORCEINLINE void Emplace(T&& Value)
	{
        FQScopeLock ScopedLock(&RwLock);
		Array.Emplace(std::move(Value));
	}

	FORCEINLINE void Empty(int32 Slack=0)
	{
        FQScopeLock ScopedLock(&RwLock);
		Array.Empty(Slack);
	}

	FORCEINLINE TArray<T>::SizeType Num() const
	{
        FQScopeLock<EQLockMode::Read> ScopedLock(&RwLock);
		return Array.Num();
	}
	
	FORCEINLINE TArray<T>::SizeType Max() const
	{
        FQScopeLock<EQLockMode::Read> ScopedLock(&RwLock);
		return Array.Max();
	}
	
	FORCEINLINE void Remove(T& Value)
	{
        FQScopeLock ScopedLock(&RwLock);
		Array.Remove(Value);
	}
	
	FORCEINLINE void RemoveAt(size_t i)
	{
        FQScopeLock ScopedLock(&RwLock);
		Array.RemoveAt(i, 1, EAllowShrinking::No);
	}
	
	FORCEINLINE void RemoveSwap(T& Value)
	{
        FQScopeLock ScopedLock(&RwLock);
		Array.RemoveSingleSwap(Value, EAllowShrinking::No);
	}
	
	FORCEINLINE void RemoveAtSwap(size_t i)
	{
        FQScopeLock ScopedLock(&RwLock);
		Array.RemoveAtSwap(i, 1, EAllowShrinking::No);
	}
	
	FORCEINLINE void Shrink()
	{
		FQScopeLock ScopedLock(&RwLock);
		Array.Shrink();
	}
	
	FORCEINLINE size_t Find(T& Value)
	{
        FQScopeLock<EQLockMode::Read> ScopedLock(&RwLock);
		return Array.Find(Value);
	}

	FORCEINLINE void Lock()
	{
		FQWrite::Lock(&RwLock);
		OwnedByThread.store(FPlatformTLS::GetCurrentThreadId(), std::memory_order_release);
	}

	FORCEINLINE void Unlock()
	{
		OwnedByThread.store(0, std::memory_order_release);
		FQWrite::Unlock(&RwLock);
	}
	
	void ForAllElements(TFunction<bool(T&)>&& Visitor)
	{
        FQScopeLock Lock(&RwLock);
		for (T& Item : Array)
		{
			if (!Visitor(Item))
			{
				return;
			}
		}
	}
	
	void ForAllElements(TFunction<bool(const T&)>&& Visitor) const
	{
        FQScopeLock Lock(&RwLock);
		for (const T& Item : Array)
		{
			if (!Visitor(Item))
			{
				return;
			}
		}
	}
	
	void WithAllElements(TFunction<void(TArray<T>&)>&& ArrayUser)
	{
        FQScopeLock Lock(&RwLock);
		ArrayUser(Array);
	}
	
	void WithAllElements(TFunction<void(const TArray<T>&)>&& ArrayUser) const
	{
        FQScopeLock Lock(&RwLock);
		ArrayUser(Array);
	}

protected:

	mutable FQLock RwLock;
	std::atomic<uint32> OwnedByThread = INVALID_THREAD_ID;
	TArray<T> Array;
};
