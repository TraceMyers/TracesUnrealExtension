#pragma once

#include "MathExt.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"

#include <atomic>
#include <type_traits>

#define INVALID_THREAD_ID 0

// very simple spinlock implementation
struct FSpinLock
{
	friend struct FScopedSpinLock;
	
	void Lock()
	{
		while (true)
		{
			if (bLocked.load(std::memory_order_relaxed) == false)
			{
				bool bExpectedValue = false;
				if (bLocked.compare_exchange_strong(bExpectedValue, true, std::memory_order_acquire, std::memory_order_relaxed))
				{
					return;	
				}
			}
			FPlatformProcess::Yield();		
		}
	}
	
	void Unlock()
	{
		bLocked.store(false, std::memory_order_release);
	}
	
	bool IsLocked() const { return bLocked.load(std::memory_order_relaxed); }
	
protected:
	
	std::atomic<bool> bLocked = false;
};

// lock a scope with a simple spin lock, e.g. FScopedSpinLock Lock(&MySpinLock);
// if the lock is already locked before a procedure call, do this:
// void SomeProc(bool bLock=true)
// {
//		FScopedSpinLock Lock(&MyLock, bLock);
//		do thing
// }
// pass false for bLock and it won't deadlock.
struct FScopedSpinLock
{
	FScopedSpinLock(FSpinLock* InLock, bool bCondition=true) : Lock(InLock), bLocked(bCondition)
	{
		if (bCondition)
		{
			InLock->Lock();
		}	
	}
	
	~FScopedSpinLock()
	{
		if (bLocked)
		{
			Lock->Unlock();
		}
	}
	
	FScopedSpinLock() = delete;
	FScopedSpinLock(const FScopedSpinLock&) = delete;
	FScopedSpinLock& operator=(const FScopedSpinLock&) = delete;
	
protected:
	
	FSpinLock* Lock = nullptr;
	bool bLocked = false;
};

enum class EQLockMode { Read, Write };

// spinlock that allows either many readers or only one writer at a time.
// becomes a wait->wake queue if any users spin too long. reverts back to spin lock when queue is emptied.
// used with FQScopeLock, FQRead and FQWrite. An instance of this struct alone does nothing.
struct FQLock
{
	friend struct FQWrite;
	friend struct FQRead;
	
	// setting max threads in ctor avoids problem where shrinking, but an event that would be removed
	// is being used by a thread.
	FQLock(int32 MaxThreadsInQueue=20)
	{
		Queue.Members.SetNum(MaxThreadsInQueue);
		for (int32 i = 0; i < MaxThreadsInQueue; i++)
		{
			Queue.Members[i] = {FPlatformProcess::GetSynchEventFromPool(), EQLockMode::Read};
		}	
	}
	
	// you must exit all users from the lock before destruction or bad things happen.
	// to kick out existing members and keep new members from entering makes the lock more
	// complicated and expensive. better the lock owner handles that part.
	~FQLock()
	{
		// weak check but might catch bad behavior
		check(Queue.Count == 0)
		for (FQQueueMember& Member : Queue.Members)
		{
			FPlatformProcess::ReturnSynchEventToPool(Member.Event);
		}
	}
	
	FQLock(const FQLock& Other) = delete;
	FQLock& operator=(const FQLock& Other) = delete;
	
	// if more threads than this number attempt simultaneous access, thread safety is no longer guaranteed.
	static constexpr int32 MaxAttemptingThreads()
	{
		static_assert(WRITE_KEY < 0);
		constexpr int32 ABS_WRITE_KEY = WRITE_KEY * (WRITE_KEY > 0 ? 1 : -1);
		return ABS_WRITE_KEY - 1;
	}

#if TUE_LOCK_TEST_INSTRUMENTATION
	uint64 GetSpinFailureCount() const
	{
		return SpinFailureCount.load(std::memory_order_relaxed);
	}

	uint64 GetContendedSpinAcquisitionCount() const
	{
		return ContendedSpinAcquisitionCount.load(std::memory_order_relaxed);
	}
#endif
	
protected:
	
	static FORCEINLINE bool CanAcquireReadLock (int32 TryKey) { return TryKey >= 0; }
	static FORCEINLINE bool CanAcquireWriteLock(int32 TryKey) { return TryKey == 0; }
	
	template <typename Predicate>
	FORCEINLINE bool TryAcquire(int32 Key, Predicate CanAcquire)
	{
		const int32 CurVal = ReadWriteCounter.load(std::memory_order_relaxed);
		if (CanAcquire(CurVal))
		{
			const int32 PrevVal = ReadWriteCounter.fetch_add(Key, std::memory_order_acquire);
			if (CanAcquire(PrevVal))
			{
				return true;
			}
			ReadWriteCounter.fetch_sub(Key, std::memory_order_release);
		}
		return false;
	}
	
	template <typename Predicate>
	void Spin(EQLockMode Mode, int32 Key, Predicate CanAcquire)
	{
		int32 YieldCount = 0;
		
		while (true)
		{
			if (TryAcquire(Key, CanAcquire))
			{
#if TUE_LOCK_TEST_INSTRUMENTATION
				if (YieldCount > 0)
				{
					ContendedSpinAcquisitionCount.fetch_add(1, std::memory_order_relaxed);
				}
#endif
				return;
			}

#if TUE_LOCK_TEST_INSTRUMENTATION
			SpinFailureCount.fetch_add(1, std::memory_order_relaxed);
#endif
			
			if (++YieldCount < 48)
			{
				// telling core that this thread is inside a busy-wait loop.
				FPlatformProcess::Yield();		
			}
			else
			{
				JoinQueue(Mode, CanAcquire);
				return;
			}
		}	
	}
	
	template <typename Predicate>
	void JoinQueue(EQLockMode Mode, Predicate CanAcquire)
	{
		const int32 Key = Mode == EQLockMode::Read ? READ_KEY : WRITE_KEY;
		int32 QueuePosition = -1;
		
		// only sleeps if the queue is full. 
		// if you never want this to sleep, just make the queue bigger on construction.
		while ((QueuePosition = Queue.TryJoin(Mode)) == -1)
		{
			FPlatformProcess::SleepNoStats(0.001f);
		} 
		
		// queue wait->wake loop
		while (true)
		{
			{
				FScopedSpinLock Lock(&Queue.SpinLock);
			
				if (QueuePosition == Queue.Front && TryAcquire(Key, CanAcquire))
				{
					Queue.AdvanceFront(true);
					// before leaving, wake the new front queue member. they will check if they can also acquire.
					Queue.WakeFront(true);
					return;
				}
			}
			
			// park this thread and wait for either an unlock or for the member in front to advance.
			Queue.Members[QueuePosition].Event->Wait();
		}
	}
	
	struct FQQueueMember
	{
		FQQueueMember() {}
		FQQueueMember(FEvent* InEvent, EQLockMode InMode) : Event(InEvent), Mode(InMode) {}
		FQQueueMember& operator=(FQQueueMember&& Other) { Event = Other.Event; Mode.store(Other.Mode.load(std::memory_order_relaxed), std::memory_order_relaxed); return *this; }
		
		FEvent* Event = nullptr;
		std::atomic<EQLockMode> Mode = EQLockMode::Read;
	};
	
	struct FQQueue
	{
		FORCEINLINE bool HasMembers() const
		{
			FScopedSpinLock Lock(&SpinLock);
			return Count > 0;
		}
	
		// returns (-1) if failed, or WaitQueue index (0 <= x < QueueSize) if success
		int32 TryJoin(EQLockMode Mode)
		{
			FScopedSpinLock Lock(&SpinLock);
			if (Count >= Members.Num())
			{
				return -1;
			}
			check(Count >= 0);
			const int32 MemberIndex = MathExt::AddWrap(Front, Count, 0, Members.Num()-1);
			Members[MemberIndex].Mode = Mode;
			Members[MemberIndex].Event->Reset();
			Count += 1;
			return MemberIndex;
		}
		
		void WakeFront(bool bPreLocked=false)
		{
			FScopedSpinLock Lock(&SpinLock, !bPreLocked);
			if (Count > 0)
			{
				Members[Front].Event->Trigger();
			}
		}
		
		void AdvanceFront(bool bPreLocked=false)
		{
			FScopedSpinLock Lock(&SpinLock, !bPreLocked);
			check(Count > 0);
			Front = MathExt::IncrementWrap(Front, 0, Members.Num()-1);
			Count -= 1;
		}
		
		// it's a lock in a lock! how far down does it go?
		mutable FSpinLock SpinLock;
		TArray<FQQueueMember> Members; // this is assumed to be fixed size after instantiation
		int32 Front = 0;
		int32 Count = 0;
	};
	
	static constexpr int32 READ_KEY  = 1;
	// write key is ~= sqrt int32 max, allowing equal number of attempting read and write threads.
	// MaxAttemptingThreads() conservatively expects the *total* number of threads in contention 
	// be less than abs(this value).
	static constexpr int32 WRITE_KEY = -46'000; 

	std::atomic<int32> ReadWriteCounter = 0;
	FQQueue Queue;

#if TUE_LOCK_TEST_INSTRUMENTATION
	std::atomic<uint64> SpinFailureCount = 0;
	std::atomic<uint64> ContendedSpinAcquisitionCount = 0;
#endif
};

struct FQWrite
{
	static FORCEINLINE void Lock(FQLock* Lock)
	{
		if (Lock->Queue.HasMembers())
		{
			Lock->JoinQueue(EQLockMode::Write, FQLock::CanAcquireWriteLock);
		}
		else
		{
			Lock->Spin(EQLockMode::Write, FQLock::WRITE_KEY, FQLock::CanAcquireWriteLock);
		}
	}

	static FORCEINLINE void Unlock(FQLock* Lock)
	{
		const int32 CounterValue = Lock->ReadWriteCounter.fetch_sub(FQLock::WRITE_KEY, std::memory_order_release) - FQLock::WRITE_KEY;
		if (CounterValue == 0)
		{
			Lock->Queue.WakeFront();
		}
	}
};

struct FQRead
{
	static FORCEINLINE void Lock(FQLock* Lock)
	{
		if (Lock->Queue.HasMembers())
		{
			Lock->JoinQueue(EQLockMode::Read, FQLock::CanAcquireReadLock);
		}
		else
		{
			Lock->Spin(EQLockMode::Read, FQLock::READ_KEY, FQLock::CanAcquireReadLock);
		}
	}

	static FORCEINLINE void Unlock(FQLock* Lock)
	{
		const int32 CounterValue = Lock->ReadWriteCounter.fetch_sub(FQLock::READ_KEY, std::memory_order_release) - FQLock::READ_KEY;
		if (CounterValue == 0)
		{
			Lock->Queue.WakeFront();
		}
	}
};

template <EQLockMode Mode=EQLockMode::Write>
struct FQScopeLock
{
	FQScopeLock(FQLock* InLock, bool bCondition=true) : Lock(InLock), bLocked(bCondition)
	{
		if (bCondition)
		{
			LockType::Lock(Lock);
		}
	}

	~FQScopeLock()
	{
		if (bLocked)
		{
			LockType::Unlock(Lock);
		}
	}
	
	FQScopeLock() = delete;
	FQScopeLock(const FQScopeLock& Other) = delete;
	FQScopeLock(FQScopeLock&& Other) = delete;
	FQScopeLock& operator=(FQScopeLock&& Other) = delete;

protected:

	using LockType = std::conditional_t<Mode == EQLockMode::Read, FQRead, FQWrite>;
	
	FQLock* Lock = nullptr;
	bool bLocked = false;
};
