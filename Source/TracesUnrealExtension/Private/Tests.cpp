#include <string>
#include <thread>

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"

#include "LockExt.h"
#include "ThreadJobber.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "UObject/ObjectMacros.h"

#define TUE_LOG_HEADER TEXT("[Traces Unreal Extension Tests] ")
#define TUE_LOG(Fmt, ...) \
	UE_LOG(LogTemp, Warning, TUE_LOG_HEADER TEXT(Fmt), __VA_ARGS__)
#define TUE_ERROR(Fmt, ...) \
	UE_LOG(LogTemp, Error, TUE_LOG_HEADER TEXT(Fmt), __VA_ARGS__)

namespace
{
	enum class ELockTestResult : uint8
	{
		Success,
		TornPublication,
		WriteOwnershipNonExclusive,
		SumIncorrect
	};
	
	bool LockTest()
	{
		// this path, if enabled in the Build.cs, tests the lock in spin mode.
		// it should not be left enabled, as it adds costly intstrumentation to the lock
		// internals.
		// it primarily tests that spin mode works as intended, and that threads can
		// often reasonably be expected to resolve contention without joining the queue.
		
#if TUE_LOCK_TEST_INSTRUMENTATION
		// Hold a writer lock while a gated contender enters FQLock::Spin. Release after
		// observing its first failed attempt, then require it to acquire without queueing.
		{
			FQLock SpinPathLock(1);
			auto ExerciseContendedSpin = [&SpinPathLock](EQLockMode Mode)
			{
				constexpr int32 MAX_ATTEMPTS = 8;
				for (int32 Attempt = 0; Attempt < MAX_ATTEMPTS; Attempt++)
				{
					const uint64 FailuresBefore = SpinPathLock.GetSpinFailureCount();
					const uint64 AcquisitionsBefore = SpinPathLock.GetContendedSpinAcquisitionCount();
					std::atomic bAttempt = false;
					std::jthread Contender([&SpinPathLock, &bAttempt, Mode]()
					{
						while (!bAttempt.load(std::memory_order_acquire))
						{
							FPlatformProcess::Yield();
						}

						if (Mode == EQLockMode::Read)
						{
							FQRead::Lock(&SpinPathLock);
							FQRead::Unlock(&SpinPathLock);
						}
						else
						{
							FQWrite::Lock(&SpinPathLock);
							FQWrite::Unlock(&SpinPathLock);
						}
					});

					FQWrite::Lock(&SpinPathLock);
					bAttempt.store(true, std::memory_order_release);
					const double Deadline = FPlatformTime::Seconds() + 1.0;
					while (SpinPathLock.GetSpinFailureCount() == FailuresBefore 
						&& FPlatformTime::Seconds() < Deadline)
					{
						FPlatformProcess::Yield();
					}
					FQWrite::Unlock(&SpinPathLock);
					Contender.join();

					if (SpinPathLock.GetSpinFailureCount() > FailuresBefore 
						&& SpinPathLock.GetContendedSpinAcquisitionCount() > AcquisitionsBefore)
					{
						return true;
					}
				}
				return false;
			};

			if (!ExerciseContendedSpin(EQLockMode::Read) ||
				!ExerciseContendedSpin(EQLockMode::Write))
			{
				return false;
			}
		}
#endif
		
		// this path primarily tests the lock in queue mode.
		
		constexpr int32 OUTER_ITERATION_COUNT = 16;

		for (int32 it = 0; it < OUTER_ITERATION_COUNT; it++)
		{
			constexpr int32 MIN_WORKER_COUNT = 2;
			constexpr int32 MAX_WORKER_COUNT = 40;
			constexpr uint32 WRITER_OWNERSHIP_BIT = uint32{1} << 31;
			constexpr double WORKER_STALL_TIMEOUT_SECONDS = 5.0;
			static constexpr int32 INNER_ITERATION_COUNT = 10000;
			
			const int32 WorkerCount = FMath::RandRange(MIN_WORKER_COUNT, MAX_WORKER_COUNT);
			const int32 ReaderCount = WorkerCount / 4;
			const int32 WriterCount = WorkerCount - ReaderCount;
		
			std::atomic TestResult = ELockTestResult::Success;
			TStaticArray<UE::Core::TAlignedElement<uint64, 64>, (4 * 1024)/8> TestData;
			FMemory::Memzero(TestData);
			double TestStart = 0;
		
			{
				// High bit marks a writer; low bits count readers. Atomic modification order
				// guarantees that at least one participant observes every invalid overlap.
				std::atomic<uint32> OwnershipState = 0;
				std::atomic WorkingCount = 0;
				std::atomic ReadyCount = 0;
				std::atomic bStart = false;
				
				TArray<std::atomic<int32>> WorkerProgress;
				WorkerProgress.SetNumZeroed(WorkerCount);
				TArray<std::atomic<bool>> WorkerDone;
				WorkerDone.SetNumZeroed(WorkerCount);
				
				for (int32 i = 0; i < WorkerCount; i++)
				{
					WorkerProgress[i].store(0, std::memory_order_relaxed);
					WorkerDone[i].store(false, std::memory_order_relaxed);
				}
			
				// queue smaller than max worker count will likely test what happens when the queue
				// is full at least a few times.
				FQLock Lock(MAX_WORKER_COUNT * 3 / 4);
				
				TArray<std::jthread> ThreadWorkers;
			
				auto ThreadProc = [&OwnershipState, &WorkingCount, &ReadyCount, &bStart, &WorkerProgress, &WorkerDone, &TestData, &Lock, &TestResult, ReaderCount](int32 ID)
				{
					const EQLockMode Mode = ID < ReaderCount ? EQLockMode::Read : EQLockMode::Write;

					WorkingCount.fetch_add(1, std::memory_order_release);
					ReadyCount.fetch_add(1, std::memory_order_release);
					while (!bStart.load(std::memory_order_acquire))
					{
						FPlatformProcess::Yield();
					}
				
					for (int32 i = 0; i < INNER_ITERATION_COUNT; i++)
					{
						if (Mode == EQLockMode::Read)
						{
							FQRead::Lock(&Lock);
							ELockTestResult Status = ELockTestResult::Success;
							const uint32 OwnershipAtEntry = OwnershipState.fetch_add(1, std::memory_order_relaxed);
							if ((OwnershipAtEntry & WRITER_OWNERSHIP_BIT) != 0)
							{
								Status = ELockTestResult::WriteOwnershipNonExclusive;
							}
						
							if (Status == ELockTestResult::Success)
							{
								const uint64 FirstValue = TestData[0];
						
								// forward sweep for memory tearing
								for (int32 X = 1; X < TestData.Num(); X++)
								{
									if (TestData[X] != FirstValue)
									{
										Status = ELockTestResult::TornPublication;
										break;
									}
								}
						
								// backward sweep
								if (Status == ELockTestResult::Success) for (int32 X = TestData.Num() - 1; X >= 1; X--)
								{
									if (TestData[X] != FirstValue)
									{
										Status = ELockTestResult::TornPublication;
										break;
									}
								}
							}
						
							const uint32 OwnershipAtExit = OwnershipState.fetch_sub(1, std::memory_order_relaxed);
							if ((OwnershipAtExit & WRITER_OWNERSHIP_BIT) != 0)
							{
								Status = ELockTestResult::WriteOwnershipNonExclusive;
							}
							FQRead::Unlock(&Lock);
						
							if (Status != ELockTestResult::Success)
							{
								TestResult = Status;
								break;
							}
						}
						else
						{
							FQWrite::Lock(&Lock);
						
							ELockTestResult Status = ELockTestResult::Success;

							const uint32 OwnershipAtEntry = OwnershipState.fetch_or(WRITER_OWNERSHIP_BIT, std::memory_order_relaxed);
							const bool bEstablishedWriterBit = (OwnershipAtEntry & WRITER_OWNERSHIP_BIT) == 0;
							if (OwnershipAtEntry != 0)
							{
								Status = ELockTestResult::WriteOwnershipNonExclusive;
							}
							else
							{
								uint64 FirstValue = TestData[0];
						
								// forward sweep for memory tearing
								for (int32 X = 1; X < TestData.Num(); X++)
								{
									if (TestData[X] != FirstValue)
									{
										Status = ELockTestResult::TornPublication;
										break;
									}					
								}
							
								for (int32 X = TestData.Num() - 1; X >= 0; X--)
								{
									TestData[X] = FirstValue + 1;
								}
							}
						
							// increase chance of collision by holding onto the lock artificially
							const int32 HoldLockCycles = FMath::RandRange(1, 50);
							// using volatile here is essentially telling the compiler this loop shouldn't be optimized out.
							for (volatile int32 j = 0; j < HoldLockCycles; j++) {;} 
						
							// Only the writer that established the bit may clear it. A second writer
							// must not erase the first writer's diagnostic ownership.
							if (bEstablishedWriterBit &&
								OwnershipState.fetch_and(~WRITER_OWNERSHIP_BIT, std::memory_order_relaxed) != WRITER_OWNERSHIP_BIT)
							{
								Status = ELockTestResult::WriteOwnershipNonExclusive;
							}
						
							FQWrite::Unlock(&Lock);
						
							if (Status != ELockTestResult::Success)
							{
								TestResult = Status;
								break;
							}
						}
					
						WorkerProgress[ID].store(i + 1, std::memory_order_relaxed);
					
						// variable gap before trying lock again to avoid lockstep and to try to reorder threads.
						const int32 JitterCycles = FMath::RandRange(1, 100);
						// using volatile for the same reason as above
						for (volatile int32 j = 0; j < JitterCycles; j++) {;} 
					
						if (TestResult.load(std::memory_order_acquire) != ELockTestResult::Success)
						{
							break;
						}
					}
				
					WorkerDone[ID].store(true, std::memory_order_release);
					WorkingCount.fetch_sub(1, std::memory_order_release);
				};
			
				for (int32 i = 0; i < WorkerCount; i++)
				{
					ThreadWorkers.Emplace(std::jthread(ThreadProc, i));
				}
			
				while (ReadyCount.load(std::memory_order_acquire) < WorkerCount)
				{
					FPlatformProcess::Yield();
				}
			
				TestStart = FPlatformTime::Seconds();
				bStart.store(true, std::memory_order_release);
			
				TArray<int32> LastProgress;
				LastProgress.SetNumZeroed(WorkerCount);
				TArray<double> StalledSeconds;
				StalledSeconds.SetNumZeroed(WorkerCount);
				
				double LastPollTime = TestStart;

				while (WorkingCount.load(std::memory_order_acquire) > 0)
				{
					FPlatformProcess::Sleep(0.001f);
					const double Now = FPlatformTime::Seconds();
					// Do not count long controller pauses, such as stopping at a debugger breakpoint,
					// as time during which workers failed to progress.
					const double ObservedSeconds = FMath::Min(Now - LastPollTime, 0.01);
					LastPollTime = Now;
					for (int32 WorkerID = 0; WorkerID < WorkerCount; WorkerID++)
					{
						if (WorkerDone[WorkerID].load(std::memory_order_acquire))
						{
							continue;
						}

						const int32 Progress = WorkerProgress[WorkerID].load(std::memory_order_relaxed);
						if (Progress != LastProgress[WorkerID])
						{
							LastProgress[WorkerID] = Progress;
							StalledSeconds[WorkerID] = 0;
							continue;
						}

						StalledSeconds[WorkerID] += ObservedSeconds;
						if (StalledSeconds[WorkerID] >= WORKER_STALL_TIMEOUT_SECONDS)
						{
							const TCHAR* ModeName = WorkerID < ReaderCount ? TEXT("reader") : TEXT("writer");
							checkf(false,
								TEXT("Lock test deadlock: worker %d (%s) made no progress for %.1f seconds."),
								WorkerID, ModeName, WORKER_STALL_TIMEOUT_SECONDS);
							StalledSeconds[WorkerID] = 0;
						}
					}
				}
			}
		
			const double TestEnd = FPlatformTime::Seconds();
		
			if (TestResult == ELockTestResult::Success)
			{
				const int32 FinalSum = WriterCount * INNER_ITERATION_COUNT;
				for (int32 i = 0; i < TestData.Num(); i++)
				{
					if (TestData[i] != FinalSum)
					{
						TestResult = ELockTestResult::SumIncorrect;
						break;
					}
				}
			}
	
			TUE_LOG("test iteration runtime = %.3f seconds", TestEnd - TestStart)
			if (TestResult != ELockTestResult::Success)
			{
				// would use UEnum for enum -> string, but that requires a header file. 
				// this .cpp file has no .h companion.
				switch (TestResult)
				{
				case ELockTestResult::TornPublication:
					TUE_ERROR("error result: torn publication", "")
					break;
				case ELockTestResult::WriteOwnershipNonExclusive:
					TUE_ERROR("error result: write ownership non-exclusive", "")
					break;
				case ELockTestResult::SumIncorrect:
					TUE_ERROR("error result: sum incorrect", "")
					break;
				default:;
				}
				return false;
			}
		}
		
		return true;
	}

	bool ThreadTeamTest()
	{
		constexpr double JOIN_TIMEOUT_SECONDS = 10.0;

		FThreadTeam Team;
		Team.Startup(8, false);
		
		TArray<int32> SharedData;
		std::atomic<bool> bSharedDataInUse = false;
		int32 FinalSum = 0;
		
		// fully parallelizable job
		struct FAddJob : FJob
		{
			virtual void Prepare() override
			{
				Value = 1;
			}
			virtual void Execute() override
			{
				if (bStarted)
				{
					bStarted->store(true, std::memory_order_release);
				}
				while (bRelease && !bRelease->load(std::memory_order_acquire))
				{
					FPlatformProcess::Yield();
				}
				for (volatile int i = 0; i < 1000; i++)
				{
					Value += 1;
				}
				if (ExecuteCount)
				{
					ExecuteCount->fetch_add(1, std::memory_order_relaxed);
				}
			}
			virtual void ApplyResult() override
			{
				if (FinalSum)
				{
					*FinalSum += Value;
				}
			}
			
			int32 Value = 0;
			int32* FinalSum = nullptr;
			std::atomic<int32>* ExecuteCount = nullptr;
			std::atomic<bool>* bStarted = nullptr;
			std::atomic<bool>* bRelease = nullptr;
		};
		
		// job where all instances share data
		struct FSharedDataJob : FJob
		{
			virtual void Prepare() override
			{
				SharedData->Add(0);
			}
			virtual void Execute() override
			{
				// dependency-based work needs to be respected
				bool bExpectedGateState = false;
				if (!bSharedDataInUse->compare_exchange_strong(bExpectedGateState, true))
				{
					bCorrect->store(false, std::memory_order_relaxed);
					return;
				}

				if (bBlockUntilReleased)
				{
					bBlockerStarted->store(true, std::memory_order_release);
					while (!bReleaseBlocker->load(std::memory_order_acquire))
					{
						FPlatformProcess::Yield();
					}
				}
				
				for (int32 i = 0; i < SharedData->Num(); i++)
				{
					(*SharedData)[i] += 1;
				}
				SharedData->Add(0);
				
				ExecuteCount->fetch_add(1, std::memory_order_relaxed);
				
				bExpectedGateState = true;
				if (!bSharedDataInUse->compare_exchange_strong(bExpectedGateState, false))
				{
					bCorrect->store(false, std::memory_order_relaxed);
				}
			}
			
			TArray<int32>* SharedData = nullptr;
			std::atomic<bool>* bSharedDataInUse = nullptr;
			std::atomic<int32>* ExecuteCount = nullptr;
			std::atomic<bool>* bCorrect = nullptr;
			std::atomic<bool>* bBlockerStarted = nullptr;
			std::atomic<bool>* bReleaseBlocker = nullptr;
			bool bBlockUntilReleased = false;
		};
		
		constexpr int32 FULLY_PARALLEL_JOB_COUNT = 10000;
		
		FThreadJobber* Jobber = Team.GetMember();
		auto JobStack = Jobber->GetJobStack();

		// Empty workloads should complete without waking or stranding workers.
		Jobber->PrepareForWork();
		Jobber->BeginWork();
		if (!Jobber->JoinWork(JOIN_TIMEOUT_SECONDS))
		{
			TUE_ERROR("empty workload timed out", "")
			Team.Shutdown();
			return false;
		}
		Jobber->ApplyResults();

		std::atomic<int32> FullyParallelExecuteCount = 0;
		for (int32 i = 0; i < FULLY_PARALLEL_JOB_COUNT; i++)
		{
			FAddJob& AddJob = JobStack->Push<FAddJob>();
			AddJob.FinalSum = &FinalSum;
			AddJob.ExecuteCount = &FullyParallelExecuteCount;
		}
		
		Jobber->PrepareForWork();
		const double FullyParallelStartTime = FPlatformTime::Seconds();
		Jobber->BeginWork();
		if (!Jobber->JoinWork(JOIN_TIMEOUT_SECONDS))
		{
			TUE_ERROR("fully parallel workload timed out", "")
			Team.Shutdown();
			return false;
		}
		const double FullyParallelEndTime = FPlatformTime::Seconds();
		Jobber->ApplyResults();
		
		const bool bFinalSumCorrect = FinalSum == FULLY_PARALLEL_JOB_COUNT * 1001;
		const bool bFullyParallelExecuteCountCorrect = FullyParallelExecuteCount.load(std::memory_order_relaxed) == FULLY_PARALLEL_JOB_COUNT;
		
		const double FullyParallelRunTime = (FullyParallelEndTime - FullyParallelStartTime) * 1000.0;
		TUE_LOG("fully parallel workload of %d jobs finished in %.3f ms, or ~%.3f ms per job", FULLY_PARALLEL_JOB_COUNT, FullyParallelRunTime, FullyParallelRunTime / FULLY_PARALLEL_JOB_COUNT)
		
		std::atomic<bool> bSharedDependencyRespected = true;
		std::atomic<bool> bBlockerStarted = false;
		std::atomic<bool> bReleaseBlocker = false;
		std::atomic<int32> ExecutedSharedDependencyJobs = 0;
		std::atomic<int32> ExecutedIndependentJobs = 0;
		int32 SharedDependencyJobCount = 0;
		int32 IndependentJobCount = 0;
		
		TArray<int64> DependencyIDs;
		for (int32 i = 0; i < 2000; i++)
		{
			if (i % 3 == 0)
			{
				int64 SharedJobID;
				FSharedDataJob& SharedJob = JobStack->Push<FSharedDataJob>({}, &SharedJobID, DependencyIDs);
				SharedJob.SharedData = &SharedData;
				SharedJob.bSharedDataInUse = &bSharedDataInUse;
				SharedJob.bCorrect = &bSharedDependencyRespected;
				SharedJob.ExecuteCount = &ExecutedSharedDependencyJobs;
				if (SharedDependencyJobCount == 0)
				{
					SharedJob.bBlockUntilReleased = true;
					SharedJob.bBlockerStarted = &bBlockerStarted;
					SharedJob.bReleaseBlocker = &bReleaseBlocker;
				}
				
				SharedDependencyJobCount++;
				DependencyIDs.Add(SharedJobID);
			}
			else
			{
				FAddJob& AddJob = JobStack->Push<FAddJob>();
				AddJob.ExecuteCount = &ExecutedIndependentJobs;
				IndependentJobCount++;
			}
		}
		
		Jobber->PrepareForWork();
		Jobber->BeginWork();

		// Push while work is active; double buffering should defer this job to the next cycle.
		std::atomic<int32> DeferredExecuteCount = 0;
		FAddJob& DeferredJob = JobStack->Push<FAddJob>();
		DeferredJob.ExecuteCount = &DeferredExecuteCount;

		const double BlockDeadline = FPlatformTime::Seconds() + JOIN_TIMEOUT_SECONDS;
		while (!bBlockerStarted.load(std::memory_order_acquire) && FPlatformTime::Seconds() < BlockDeadline)
		{
			FPlatformProcess::Yield();
		}
		while (JobStack->CountWorkingThreads() != 1 && FPlatformTime::Seconds() < BlockDeadline)
		{
			FPlatformProcess::SleepNoStats(0.001f);
		}
		const bool bDependencyBlockedDeterministically = bBlockerStarted.load(std::memory_order_acquire)
			&& JobStack->CountWorkingThreads() == 1
			&& bSharedDependencyRespected.load(std::memory_order_relaxed);
		bReleaseBlocker.store(true, std::memory_order_release);

		if (!Jobber->JoinWork(JOIN_TIMEOUT_SECONDS))
		{
			TUE_ERROR("dependency workload timed out", "")
			Team.Shutdown();
			return false;
		}
		Jobber->ApplyResults();	

		Jobber->PrepareForWork();
		Jobber->BeginWork();
		const bool bDeferredWorkCompleted = Jobber->JoinWork(JOIN_TIMEOUT_SECONDS);
		if (bDeferredWorkCompleted)
		{
			Jobber->ApplyResults();
		}
		
		Team.Shutdown();

		// Exercise one-worker shutdown during active work, then restart with automatic sizing.
		std::atomic<bool> bShutdownJobStarted = false;
		std::atomic<bool> bReleaseShutdownJob = false;
		Team.Startup(1, false);
		Jobber = Team.GetMember();
		JobStack = Jobber->GetJobStack();
		FAddJob& ShutdownJob = JobStack->Push<FAddJob>();
		ShutdownJob.bStarted = &bShutdownJobStarted;
		ShutdownJob.bRelease = &bReleaseShutdownJob;
		Jobber->PrepareForWork();
		Jobber->BeginWork();
		const double ShutdownStartDeadline = FPlatformTime::Seconds() + JOIN_TIMEOUT_SECONDS;
		while (!bShutdownJobStarted.load(std::memory_order_acquire) && FPlatformTime::Seconds() < ShutdownStartDeadline)
		{
			FPlatformProcess::Yield();
		}
		std::jthread ShutdownReleaser([&bReleaseShutdownJob]()
		{
			FPlatformProcess::SleepNoStats(0.01f);
			bReleaseShutdownJob.store(true, std::memory_order_release);
		});
		JobStack.Reset();
		Team.Shutdown();

		Team.Startup(0, false);
		Jobber = Team.GetMember();
		Jobber->PrepareForWork();
		Jobber->BeginWork();
		const bool bAutomaticTeamEmptyWorkCompleted = Jobber->JoinWork(JOIN_TIMEOUT_SECONDS);
		if (bAutomaticTeamEmptyWorkCompleted)
		{
			Jobber->ApplyResults();
		}
		Team.Shutdown();
		
		const bool bExecutedSharedDependencyCountCorrect = SharedDependencyJobCount == ExecutedSharedDependencyJobs.load(std::memory_order_relaxed);
		const bool bExecutedIndependentCountCorrect = IndependentJobCount == ExecutedIndependentJobs.load(std::memory_order_relaxed);
		const bool bDeferredExecuteCountCorrect = DeferredExecuteCount.load(std::memory_order_relaxed) == 1;
		const bool bShutdownJobStartedCorrectly = bShutdownJobStarted.load(std::memory_order_relaxed);
		if (!bExecutedSharedDependencyCountCorrect)
		{
			TUE_ERROR("did not execute as many shared dependency jobs as were queued. queued %d executed %d", SharedDependencyJobCount, ExecutedSharedDependencyJobs.load())
		}
		if (!bFinalSumCorrect)
		{
			TUE_ERROR("the final sum for the fully parallel jobs was incorrect.", "")
		}
		if (!bFullyParallelExecuteCountCorrect)
		{
			TUE_ERROR("did not execute as many fully parallel jobs as were queued. queued %d executed %d", FULLY_PARALLEL_JOB_COUNT, FullyParallelExecuteCount.load())
		}
		if (!bDependencyBlockedDeterministically)
		{
			TUE_ERROR("dependent jobs were not blocked while the first shared-data job held the dependency.", "")
		}
		if (!bSharedDependencyRespected.load(std::memory_order_relaxed))
		{
			TUE_ERROR("the dependency id arrays were not respected. some jobs tried to access shared dependencies at the same time.", "")		
		}
		if (!bExecutedIndependentCountCorrect)
		{
			TUE_ERROR("did not execute as many independent jobs as were queued. queued %d executed %d", IndependentJobCount, ExecutedIndependentJobs.load())
		}
		if (!bDeferredWorkCompleted)
		{
			TUE_ERROR("deferred workload timed out", "")
		}
		if (!bDeferredExecuteCountCorrect)
		{
			TUE_ERROR("deferred job did not execute exactly once. expected 1 executed %d", DeferredExecuteCount.load())
		}
		if (!bShutdownJobStartedCorrectly)
		{
			TUE_ERROR("one-worker shutdown job did not start before the timeout", "")
		}
		if (!bAutomaticTeamEmptyWorkCompleted)
		{
			TUE_ERROR("automatic-size team empty workload timed out", "")
		}
		return bFinalSumCorrect
			&& bFullyParallelExecuteCountCorrect
			&& bDependencyBlockedDeterministically
			&& bSharedDependencyRespected.load(std::memory_order_relaxed)
			&& bExecutedSharedDependencyCountCorrect
			&& bExecutedIndependentCountCorrect
			&& bDeferredWorkCompleted
			&& bDeferredExecuteCountCorrect
			&& bShutdownJobStartedCorrectly
			&& bAutomaticTeamEmptyWorkCompleted;
	}
	
}

const FAutoConsoleCommandWithWorldAndArgs TestCommand(TEXT("tracesextension.test"), TEXT(""), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	struct Test
	{
		FString Name;
		const TFunction<bool()> Proc;
	};
	
	TMap<FString, TArray<Test>> Tests;
	
	Tests.Add("locks", {{"locks", LockTest}});
	Tests.Add("threads", {{"threads", ThreadTeamTest}});
	Tests.Add("all", {});
	
	TArray<FString> Keys;
	Tests.GetKeys(Keys);
	for (const FString& Key : Keys)
	{
		if (Key == "all")
		{
			continue;
		}
		Tests["all"].Add(Tests[Key][0]);
	}
	
	TArray<Test> RunTests;
	
	for (const FString& Arg : Args)
	{
		auto ProcArray = Tests.Find(Arg);
		if (ProcArray)
		{
			RunTests.Append(*ProcArray);
		}
		else
		{
			TUE_ERROR("invalid argument %s", *Arg)
		}
	}
	
	if (RunTests.Num() > 0)
	{
		AsyncTask(ENamedThreads::Type::AnyHiPriThreadHiPriTask, [RunTests]()
		{
			TUE_LOG("****************", "")
			int32 SuccessCount = 0;
			for (int i = 0; i < RunTests.Num(); i++)
			{
				const Test& T = RunTests[i];
				TUE_LOG("running test (%d/%d) : %s", i+1, RunTests.Num(), *T.Name)
				const bool bSuccess = T.Proc();
				SuccessCount += bSuccess;
				TUE_LOG("%s", bSuccess ? TEXT("[passed]") : TEXT("[failed]"))
			}
			TUE_LOG("passed %d/%d tests", SuccessCount, RunTests.Num())
			TUE_LOG("****************", "")
		});
	}
	else
	{
		TUE_LOG("no valid test arguments were passed", "")
	}
}));
