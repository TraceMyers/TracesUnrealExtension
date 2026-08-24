#pragma once

#include "Async/Mutex.h"
#include "Containers/Array.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTLS.h"

#include <atomic>

// a single buffer made for thread-safe usage
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
		RwLock.ReadLock();
		const T Item = Array[i];
		RwLock.ReadUnlock();
		return Item;
	}

	FORCEINLINE TArray<T>& GetArrayLocked()
	{
		check(OwnedByThread.load(std::memory_order_acquire) == FPlatformTLS::GetCurrentThreadId());
		return Array;
	}
	
	FORCEINLINE void PushUnique(const T& Value)
	{
		RwLock.WriteLock();
		Array.AddUnique(Value);
		RwLock.WriteUnlock();
	}
	
	FORCEINLINE void Add(const T& Value)
	{
		RwLock.WriteLock();
		Array.Add(Value);
		RwLock.WriteUnlock();
	}

	FORCEINLINE void Push(const T& Value)
	{
		RwLock.WriteLock();
		Array.Push(Value);
		RwLock.WriteUnlock();
	}

	FORCEINLINE void Emplace(T&& Value)
	{
		RwLock.WriteLock();
		Array.Emplace(std::move(Value));
		RwLock.WriteUnlock();
	}

	FORCEINLINE void Empty(int32 Slack=0)
	{
		RwLock.WriteLock();
		Array.Empty(Slack);
		RwLock.WriteUnlock();
	}

	FORCEINLINE TArray<T>::SizeType Num() const
	{
		RwLock.ReadLock();
		size_t ArrayNum = Array.Num();
		RwLock.ReadUnlock();
		return ArrayNum;
	}
	
	FORCEINLINE TArray<T>::SizeType Max() const
	{
		RwLock.ReadLock();
		size_t ArrayMax = Array.Max();
		RwLock.ReadUnlock();
		return ArrayMax;
	}
	
	FORCEINLINE void Remove(T& Value)
	{
		RwLock.WriteLock();
		Array.Remove(Value);
		RwLock.WriteUnlock();
	}
	
	FORCEINLINE void RemoveAt(size_t i)
	{
		RwLock.WriteLock();
		Array.RemoveAt(i, 1, EAllowShrinking::No);
		RwLock.WriteUnlock();
	}
	
	FORCEINLINE void RemoveSwap(T& Value)
	{
		RwLock.WriteLock();
		Array.RemoveSingleSwap(Value, EAllowShrinking::No);
		RwLock.WriteUnlock();
	}
	
	FORCEINLINE void RemoveAtSwap(size_t i)
	{
		RwLock.WriteLock();
		Array.RemoveAtSwap(i, 1, EAllowShrinking::No);
		RwLock.WriteUnlock();
	}
	
	FORCEINLINE void Shrink()
	{
		RwLock.WriteLock();
		Array.Shrink();
		RwLock.WriteUnlock();
	}
	
	FORCEINLINE size_t Find(T& Value)
	{
		RwLock.ReadLock();
		size_t Index = Array.Find(Value);
		RwLock.ReadUnlock();
		return Index;
	}
	
	FORCEINLINE void Reserve(size_t Count)
	{
		RwLock.WriteLock();
		Array.Reserve(Count);
		RwLock.WriteUnlock();
	}

	FORCEINLINE void Lock()
	{
		RwLock.WriteLock();
		OwnedByThread.store(FPlatformTLS::GetCurrentThreadId(), std::memory_order_release);
	}

	FORCEINLINE void Unlock()
	{
		OwnedByThread.store(0, std::memory_order_release);
		RwLock.WriteUnlock();
	}
	
	void ForAllElements(TFunction<bool(T&)>&& Visitor)
	{
		RwLock.WriteLock();
		for (T& Item : Array)
		{
			if (!Visitor(Item))
			{
				RwLock.WriteUnlock();
				return;
			}
		}
		RwLock.WriteUnlock();
	}
	
	void ForAllElements(TFunction<bool(const T&)>&& Visitor) const
	{
		RwLock.WriteLock();
		for (const T& Item : Array)
		{
			if (!Visitor(Item))
			{
				RwLock.WriteUnlock();
				return;
			}
		}
		RwLock.WriteUnlock();
	}
	
	void WithAllElements(TFunction<void(TArray<T>&)>&& ArrayUser)
	{
		RwLock.WriteLock();
		ArrayUser(Array);
		RwLock.WriteUnlock();
	}
	
	void WithAllElements(TFunction<void(const TArray<T>&)>&& ArrayUser) const
	{
		RwLock.WriteLock();
		ArrayUser(Array);
		RwLock.WriteUnlock();
	}

protected:

	static constexpr uint32 INVALID_THREAD_ID = 0;
	mutable FRWLock RwLock;
	std::atomic<uint32> OwnedByThread = INVALID_THREAD_ID;
	TArray<T> Array;
};
