#include "JobStack.h"

FJobStackInterface::FJobStackInterface()
{
	ControllerThreadID = FPlatformTLS::GetCurrentThreadId();
	ControllerThreadWakeEvent = FPlatformProcess::GetSynchEventFromPool();
}

FJobStackInterface::~FJobStackInterface()
{
	FQScopeLock Lock1(&CurLock);
	FQScopeLock Lock2(&NextLock);
	
	ForceDeplete(true);
	EmptyCurrent(true);
	JobsDB.Flip();
	ForceDeplete(true);
	EmptyCurrent(true);
	
	FPlatformProcess::ReturnSynchEventToPool(ControllerThreadWakeEvent);
	ControllerThreadWakeEvent = nullptr;
}

int32 FJobStackInterface::CountWorkingThreads() const
{
	int32 Count = 0;
	WorkerThreadWakeEvents.ForAllElements([&Count](const FWorkerThreadWakeEvent* WakeEvent)
	{
		Count += WakeEvent->WakeCounter.load(std::memory_order_relaxed).bAwake;
		return true;
	});
	return Count;
}

void FJobStackInterface::WakeWorkerThreads(const FWorkerThreadWakeEvent* ThisThreadWakeEvent)
{
	WorkerThreadWakeEvents.ForAllElements([&](FWorkerThreadWakeEvent* WakeEvent)
	{
		// excluding this thread.
		if (WakeEvent == ThisThreadWakeEvent)
		{
			return true;
		}
		
		while (true)
		{
			FWorkerWakeCounter BeforeWake = WakeEvent->WakeCounter.load(std::memory_order_relaxed);	
			if (WakeEvent->WakeCounter.compare_exchange_strong(BeforeWake, {BeforeWake.Epoch+1, true}, std::memory_order_release, std::memory_order_relaxed))
			{
				WakeEvent->Event->Trigger();
				return true;
			}
		}
	});	
}

void FJobStackInterface::ResetControllerThreadWakeSignal() const
{
	ControllerThreadWakeEvent->Reset();
}

void FJobStackInterface::WakeControllerThread() const
{
	ControllerThreadWakeEvent->Trigger();
}

void FJobStackInterface::SleepControllerThread() const
{
	check(ControllerThreadID == FPlatformTLS::GetCurrentThreadId())
	ControllerThreadWakeEvent->Wait();
}

void FJobStackInterface::EmptyCurrent(bool bPreLocked)
{
	FQScopeLock Lock1(&CurLock, !bPreLocked);
	
	auto& Jobs = JobsDB.GetCurrent();
	check(DepletionCounter.load(std::memory_order_acquire) == Jobs.Num());
		
	for (auto& Stacked : Jobs)
	{
		delete Stacked.Job;
	}
	
	Jobs.Empty(Jobs.Max());
}

void FJobStackInterface::EmptyThreadWakeEvents()
{
	WorkerThreadWakeEvents.Empty();
}

void FJobStackInterface::InitJob(FStackedJob& Stacked, TArrayView<int64> DependencyIDs)
{
	Stacked.ID = IDCounter.fetch_add(1);
	if (DependencyIDs.Num() > 0)
	{
		// the only valid ID's are from jobs lower in the stack
		const auto& NextJobs = JobsDB.GetNext();
		
#if UE_BUILD_DEBUG | UE_BUILD_DEVELOPMENT
		checkf(NextJobs.Num() > 1, L"%hs: tried to add dependencies to the first job in the stack. dependencies can only come from jobs lower in the stack.", __FUNCTION__)
#else
		if (NextJobs.Num() <= 1)
		{
			LowLevelFatalError(L"%hs: tried to add dependencies to the first job in the stack. dependencies can only come from jobs lower in the stack.", __FUNCTION__)
		}
#endif
		
		const int64 MinValidID = NextJobs[0].ID;
		const int64 MaxValidID = NextJobs[NextJobs.Num()-2].ID;
		
		// if dependency ID is invalid in shipping, we crash.
		// why? here are the only 3 good options as I see it:
		// 1. crash. pro: easy to debug, con: dependably ruins user experience.
		// 2. remove invalid IDs. pro: might work, con: may add pernicious bugs only to shipping build. hard to debug.
		// 3. ignore invalid IDs. pro: might work + easy to debug with machine access, con: potential deadlocks. hard to debug without access to machine.
		// I like easy to debug.
#if UE_BUILD_DEBUG | UE_BUILD_DEVELOPMENT
		for (int64 ID : DependencyIDs)
		{
			checkf(ID >= MinValidID && ID <= MaxValidID, L"%hs: invalid dependency ID %lld (min valid: %lld, max valid: %lld) while adding job of type %s", __FUNCTION__, ID, MinValidID, MaxValidID, *Stacked.Job->GetName().ToString())
		}
#else
		for (int64 ID : DependencyIDs)
		{
			if (ID < MinValidID || ID > MaxValidID)
			{
				LowLevelFatalError(L"%hs: invalid dependency ID %lld (min valid: %lld, max valid: %lld) while adding job of type %s", __FUNCTION__, ID, MinValidID, MaxValidID, *Stacked.Job->GetName().ToString());
			}
		}
#endif
		
		Stacked.DependencyIDs = DependencyIDsDB.GetNext().BufAppend(DependencyIDs);
	}
	Stacked.Status = FStackedJob::Queued;
}

void FJobStack::Prepare()
{
	FQScopeLock Lock(&CurLock);
	auto& Jobs = JobsDB.GetCurrent();
	for (auto& Stacked : Jobs)
	{
		Stacked.Job->Prepare();
		Stacked.Status = FStackedJob::Prepared;
	}
}

void FJobStack::ApplyResults()
{
	FQScopeLock Lock(&CurLock);
	auto& Jobs = JobsDB.GetCurrent();
	for (auto& Stacked : Jobs)
	{
		Stacked.Job->ApplyResult();
		Stacked.Status = FStackedJob::Applied;
	}	
}

FJobStack::EExecuteResult FJobStack::ExcecuteJob(FWorkerThreadWakeEvent* ThisThreadWakeEvent, TFunction<bool(const FJob*)>&& Predicate)
{
	FStackedJob* Stacked = nullptr;
	int32 Depletion = 0;
	bool bIsFinalJob = false;
	
	// clear stale wake events right before checking if there is work to do.
	ThisThreadWakeEvent->LastKnownEpoch = ThisThreadWakeEvent->WakeCounter.load(std::memory_order_acquire).Epoch;
	ThisThreadWakeEvent->Event->Reset();
		
	{
		FQScopeLock Lock(&CurLock);
			
		Depletion = DepletionCounter.load(std::memory_order_relaxed);
		auto& Jobs = JobsDB.GetCurrent();
			
		if (Depletion >= Jobs.Num())
		{
			return BufferEmpty;
		}
			
		Stacked = &Jobs[Depletion];
			
		if (Stacked->DependencyIDs.Count > 0)
		{
			TArrayView<int64> Blockers = DependencyIDsDB.GetCurrent().BufGetView(Stacked->DependencyIDs);
			for (int64 Blocker : Blockers)
			{
				if (WorkingJobIDs.Find(Blocker) != -1)
				{
					return DependencyAlive;
				}
			}
		}
			
		if (!Predicate(Stacked->Job))
		{
			return PredicateFailed;
		}
			
		// make sure this id is tracked before unlocking, otherwise another thread might slip by
		// without checking against this dependency
		WorkingJobIDs.Push(Stacked->ID);
		// might as well keep this in sync with the two surrounding actions. it's cheap to set.
		Stacked->Status = FStackedJob::Working; 
		// avoid false positive on depletion + no workers by setting this before unlock
		WorkingJobCount.fetch_add(1, std::memory_order_release);
		
		DepletionCounter.fetch_add(1, std::memory_order_release);
	}
		
	Stacked->Job->Execute();
		
	Stacked->Status = FStackedJob::WorkFinished;
	WorkingJobIDs.RemoveSwap(Stacked->ID);
	WorkingJobCount.fetch_sub(1);
		
	if (DepletionCounter.load(std::memory_order_acquire) < JobsDB.GetCurrent().Num())
	{
		// any threads that may be waiting on dependencies or for the situation to change so that a predicate
		// will return true need to be awoken.
		WakeWorkerThreads(ThisThreadWakeEvent);
	}
		
	return Success;
}

void FJobStack::Flip()
{
	FQScopeLock Lock1(&CurLock);
	FQScopeLock Lock2(&NextLock);
	
	EmptyCurrent(true);
	JobsDB.Flip();
	DependencyIDsDB.Flip();
	DepletionCounter.store(0, std::memory_order_release);
}

void FJobStack::SleepWorkerThread(FWorkerThreadWakeEvent* Event)
{
	FWorkerWakeCounter ExpectedCounter = {Event->LastKnownEpoch, true};
	if (Event->WakeCounter.compare_exchange_strong(ExpectedCounter, {ExpectedCounter.Epoch+1, false}))
	{
		int32 NewIdleWorkerCount = IdleWorkerCount.fetch_add(1, std::memory_order_acq_rel) + 1;
		if (NewIdleWorkerCount == WorkerThreadWakeEvents.Num())
		{
			WakeControllerThread();
		}

		// thread execution stops here until the event is signaled by another thread
		Event->Event->Wait();
		IdleWorkerCount.fetch_sub(1, std::memory_order_release);
	}
}

void FJobStack::AddWorkerThreadWakeEvent(FWorkerThreadWakeEvent* Event)
{
	WorkerThreadWakeEvents.Push(Event);
}

void FJobStack::RemoveWorkerThreadWakeEvent(FWorkerThreadWakeEvent* Event)
{
	WorkerThreadWakeEvents.Remove(Event);
}

bool FJobStack::HasWorkerThreadWakeEvent(FWorkerThreadWakeEvent* Event)
{
	return WorkerThreadWakeEvents.Find(Event) != -1;
}
