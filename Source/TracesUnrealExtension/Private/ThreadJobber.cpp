#include "ThreadJobber.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"

FThreadTeam::~FThreadTeam()
{
	Shutdown();
}

void FThreadTeam::Startup(size_t ThreadCount, bool bForceControlOnGameThread, int32 ThreadStackSize)
{
	check(Jobbers.Num() == 0)
	
	if (ThreadCount == 0)
	{
		constexpr int32 EXPECTED_BUSY_CORES = 4;
		const int32 CoreCount = FPlatformMisc::NumberOfCores();
		ThreadCount = FMath::Clamp(CoreCount - EXPECTED_BUSY_CORES, 1, 20);
	}
	
	for (uint32 i = 0; i < ThreadCount; i++)
	{
		Jobbers.Emplace(MakeUnique<FThreadJobber>(i + 1, i > 0 ? Jobbers[0].Get() : nullptr, bForceControlOnGameThread, ThreadStackSize));
	}
	
	FJobStackInterface* JobStack = Jobbers[0].Get()->GetJobStack().Get();
	// all of the newly created threads may need a moment to see that the job stack is empty,
	// then go idle.
	while (!JobStack->IsIdle())
	{
		const auto _ = JobStack->SleepControllerThread();
	}
	// if this thread reaches the while(!IsIdle()) check after all threads have gone idle,
	// it never gets a chance to reset its wake signal by sleeping and waking. this
	// manual reset makes sure the main controlling thread (the one this block is running on,
	// probably the game thread) doesn't blow through its first sleep due to having a stale wake signal
	// from startup.
	JobStack->ResetControllerThreadWakeSignal();
}

void FThreadTeam::Shutdown()
{
	if (Jobbers.Num() > 0)
	{
		Jobbers[0].Get()->GetJobStack()->EmptyThreadWakeEvents();
		for (int32 i = 0; i < Jobbers.Num(); i++)
		{
			Jobbers[i].Reset();
		}
	}
	Jobbers.Empty();
}

FThreadJobber* FThreadTeam::GetMember()
{
	if (Jobbers.Num() <= 0)
	{
		return nullptr;
	}
	return Jobbers[0].Get();
}

FThreadJobber::FThreadJobber(uint32 InID, const FThreadJobber* PrimaryJobber, bool bInForceControlOnGameThread, uint32 StackSize) : ID(InID), bForceControlOnGameThread(bInForceControlOnGameThread)
{
	if (bForceControlOnGameThread)
	{
		check(IsInGameThread())
	}
	
	// the stack of jobs is spawned by the primary jobber and shared between all of them.
	// this is the hub they all get jobs from and are woken by to work.
	if (PrimaryJobber != nullptr)
	{
		JobStack = PrimaryJobber->JobStack;
	}
	else
	{
		JobStack = MakeShared<FJobStack>();
		check(JobStack.Get() != nullptr)
	}
	
	// the thread object uses this to wait and wake. unlike sleeping, wait->wake has (allegedly) microsecond resolution
	WakeEvent = FWorkerThreadWakeEvent(FPlatformProcess::GetSynchEventFromPool(), true);
	JobStack->AddWorkerThreadWakeEvent(&WakeEvent);
	
	Thread = FRunnableThread::Create(this, *FString::Printf(L"Creature Thread Worker %d", ID), StackSize, TPri_Normal);
	if (UNLIKELY(Thread == nullptr))
	{
		LowLevelFatalError(L"failed to create a thread jobber with id %u", ID);
	}
}

FThreadJobber::~FThreadJobber()
{
	if (bForceControlOnGameThread)
	{
		check(IsInGameThread())
	}
	
	bRun.store(false, std::memory_order_release);
	WakeEvent.Event->Trigger();
	Thread->WaitForCompletion();
	
	delete Thread;
	Thread = nullptr;
	
	JobStack->RemoveWorkerThreadWakeEvent(&WakeEvent);
	FPlatformProcess::ReturnSynchEventToPool(WakeEvent.Event);
	
	JobStack->ForceDeplete();
	JobStack.Reset();
}

void FThreadJobber::PrepareForWork() const
{
	if (bForceControlOnGameThread)
	{
		check(IsInGameThread())
	}
	
	FJobStack* Stack = JobStack.Get();
	check(Stack->IsIdle());
	
	Stack->Flip();
	Stack->Prepare();
}

void FThreadJobber::BeginWork() const
{
	if (bForceControlOnGameThread)
	{
		check(IsInGameThread())
	}
	
	FJobStack* Stack = JobStack.Get();
	check(Stack->CountWorkingThreads() == 0)
	
	if (Stack->IsDepleted())
	{
		return;
	}
	Stack->WakeWorkerThreads();
}

bool FThreadJobber::JoinWork(double TimeoutSeconds) const
{
	if (bForceControlOnGameThread)
	{
		check(IsInGameThread())
	}

	FJobStack* Stack = JobStack.Get();
	const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
	while (!Stack->IsIdle())
	{
		if (FPlatformTime::Seconds() >= Deadline)
		{
			return false;
		}
		const auto _ = Stack->SleepControllerThread();
	}
	return true;
}

void FThreadJobber::ApplyResults() const
{
	if (bForceControlOnGameThread)
	{
		check(IsInGameThread())
	}
	
	FJobStack* Stack = JobStack.Get();
	check(Stack->IsIdle());
	
	Stack->ApplyResults();
}

bool FThreadJobber::Init()
{
	return true;
}

uint32 FThreadJobber::Run()
{
	while (bRun.load(std::memory_order_acquire))
	{
		FJobStack* Stack = JobStack.Get();
		auto Result = Stack->ExcecuteJob(&WakeEvent);
		switch (Result)
		{
		case FJobStack::Success:
			break;
		case FJobStack::PredicateFailed:
			checkf(false, L"got PredicateFailed result from ConditionalExecuteJob(), which shouldn't be possible.")
		case FJobStack::BufferEmpty:
		case FJobStack::DependencyAlive:
			Stack->SleepWorkerThread(&WakeEvent);
			break;
		}
	}
	return 0;
}

void FThreadJobber::Stop()
{
	bRun.store(false, std::memory_order_release);
	WakeEvent.Event->Trigger();
}
