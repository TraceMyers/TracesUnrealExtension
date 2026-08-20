#pragma once

#include "ArrayBuffer.h"
#include "LockBuffer.h"
#include "MultiBuffer.h"
#include "LockExt.h"

struct FJob
{
	virtual ~FJob() {}
	virtual void Prepare() {}; // run on controller (probably game) thread before work
	virtual void Execute() = 0; // run by worker thread
	virtual void ApplyResult() {} // run on controller thread after work
	virtual FName GetName() { return FName("FJob"); } // override this for runtime debug info
};

struct FWorkerWakeCounter
{
	int64 Epoch = 0;
	bool bAwake = false;
};

struct FWorkerThreadWakeEvent
{
	FWorkerThreadWakeEvent() {}
	FWorkerThreadWakeEvent(FEvent* InEvent, bool bInAwake) : Event(InEvent), WakeCounter({0, bInAwake}) {}
	FWorkerThreadWakeEvent(const FWorkerThreadWakeEvent& InEvent) : Event(InEvent.Event), WakeCounter(InEvent.WakeCounter.load()) {}
	FWorkerThreadWakeEvent& operator=(const FWorkerThreadWakeEvent& InEvent)
	{
		Event = InEvent.Event;
		WakeCounter.store(InEvent.WakeCounter);
		return *this;
	}
	FEvent* Event = nullptr;
	std::atomic<FWorkerWakeCounter> WakeCounter{};
	int64 LastKnownEpoch = 0;
};

class FJobStackInterface
{
public:	
	
	template<typename JobType>
	JobType& Push(JobType&& InJob={}, int64* OutID=nullptr, TArrayView<int64> DependencyIDs={})
	{
		static_assert(TIsDerivedFrom<JobType, FJob>::Value);
		FQScopeLock Lock(&NextLock);
		auto& Jobs = JobsDB.GetNext();
		Jobs.Add({.Job=new JobType(MoveTemp(InJob))});
		FStackedJob& Stacked = Jobs[Jobs.Num()-1];
		InitJob(Stacked, DependencyIDs);
		if (OutID)
		{
			*OutID = Stacked.ID;
		}
		return *Stacked.Job;
	}
	
	int32 CountWorkingThreads() const;
	
	// unlocked to reduce contention
	bool IsDepleted() const
	{
		return DepletionCounter.load(std::memory_order_acquire) == JobsDB.GetCurrent().Num() && WorkingJobCount.load(std::memory_order_relaxed) == 0;
	}
	
	bool IsIdle() const
	{
		return IsDepleted() && CountWorkingThreads() == 0;
	}
	
	void ForceDeplete(bool bPreLocked=false)
	{
		FQScopeLock Lock(&CurLock, !bPreLocked);
		DepletionCounter.store(JobsDB.GetCurrent().Num(), std::memory_order_release);
	}
	
	// if you pass a non-null pointer for ThisThreadWakeEvent, said thread wake event will be excluded from waking
	void WakeWorkerThreads(const FWorkerThreadWakeEvent* ThisThreadWakeEvent=nullptr);
	
	void ResetControllerThreadWakeSignal() const;
	
	void WakeControllerThread() const;
	
	void SleepControllerThread() const;
	
	void EmptyCurrent(bool bPreLocked=false);
	
	void EmptyThreadWakeEvents();
	
protected:

	struct FStackedJob
	{
		enum EStatus : uint8
		{
			Queued,
			Prepared,
			Working,
			WorkFinished,
			Applied
		};
		
		FJob* Job = nullptr;
		int64 ID = 0;
		FBufferIndexer DependencyIDs = {};
		TAtomic<EStatus> Status = Queued;
	};
	
	FJobStackInterface();
	
	~FJobStackInterface();
	
	void InitJob(FStackedJob& Stacked, TArrayView<int64> DependencyIDs);
	
	TMultiBuffer<int64, 2, TArrayBuffer> DependencyIDsDB;
	TMultiBuffer<FStackedJob, 2> JobsDB;
	std::atomic<int64> IDCounter = 0;
	std::atomic<int32> DepletionCounter = 0;
	std::atomic<int32> WorkingJobCount = 0;
	std::atomic<int32> IdleWorkerCount = 0;
	std::atomic<int64> WorkerWakeCounter = 0;
	TLockBuffer<int64> WorkingJobIDs;
	TLockBuffer<FWorkerThreadWakeEvent*> WorkerThreadWakeEvents;
	
	uint32 ControllerThreadID = 0;
	FEvent* ControllerThreadWakeEvent = nullptr;
	
	mutable FQLock CurLock;
	mutable FQLock NextLock;
};

class FJobStack : public FJobStackInterface
{
public:
	
	enum EExecuteResult
	{
		Success,
		BufferEmpty,
		PredicateFailed,
		DependencyAlive
	};
	
	void Prepare();
	
	void ApplyResults();
	
	EExecuteResult ExcecuteJob(FWorkerThreadWakeEvent* ThisThreadWakeEvent, TFunction<bool(const FJob*)>&& Predicate=[](const FJob*) { return true; });
	
	void Flip();
	
	void SleepWorkerThread(FWorkerThreadWakeEvent* Event);
	
	void AddWorkerThreadWakeEvent(FWorkerThreadWakeEvent* Event);
	
	void RemoveWorkerThreadWakeEvent(FWorkerThreadWakeEvent* Event);
	
	bool HasWorkerThreadWakeEvent(FWorkerThreadWakeEvent* Event);
	
};
