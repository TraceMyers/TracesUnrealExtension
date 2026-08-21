#include <string>
#include <thread>

#include "CoreMinimal.h"
#include "EnumExt.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"

#include "LockExt.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "UObject/ObjectMacros.h"

#define TUE_LOG_HEADER TEXT("[Traces Unreal Extension Tests] ")
#define TUE_LOG(Fmt, ...) \
	UE_LOG(LogTemp, Warning, TUE_LOG_HEADER TEXT(Fmt), __VA_ARGS__)

namespace
{
	enum class ETestFlags : uint8
	{
		Success						= 0x00,
		TornPublication				= 0x01,
		WriteOwnershipNonExclusive	= 0x02,
		SumIncorrect				= 0x04
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
		
			std::atomic TestFlags = ETestFlags::Success;
			TStaticArray<uint64, (4 * 1024)/8, 64> TestData = {0};
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
			
				auto ThreadProc = [&OwnershipState, &WorkingCount, &ReadyCount, &bStart, &WorkerProgress, &WorkerDone, &TestData, &Lock, &TestFlags, ReaderCount](int32 ID)
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
							ETestFlags Status = ETestFlags::Success;
							const uint32 OwnershipAtEntry = OwnershipState.fetch_add(1, std::memory_order_relaxed);
							if ((OwnershipAtEntry & WRITER_OWNERSHIP_BIT) != 0)
							{
								Status = ETestFlags::WriteOwnershipNonExclusive;
							}
						
							if (Status == ETestFlags::Success)
							{
								const uint64 FirstValue = TestData[0];
						
								// forward sweep for memory tearing
								for (int32 X = 1; X < TestData.Num(); X++)
								{
									if (TestData[X] != FirstValue)
									{
										Status = ETestFlags::TornPublication;
										break;
									}
								}
						
								// backward sweep
								if (Status == ETestFlags::Success) for (int32 X = TestData.Num() - 1; X >= 1; X--)
								{
									if (TestData[X] != FirstValue)
									{
										Status = ETestFlags::TornPublication;
										break;
									}
								}
							}
						
							const uint32 OwnershipAtExit = OwnershipState.fetch_sub(1, std::memory_order_relaxed);
							if ((OwnershipAtExit & WRITER_OWNERSHIP_BIT) != 0)
							{
								Status = ETestFlags::WriteOwnershipNonExclusive;
							}
							FQRead::Unlock(&Lock);
						
							if (Status != ETestFlags::Success)
							{
								EnumExt::AddFlagsAtomic(TestFlags, Status);
								break;
							}
						}
						else
						{
							FQWrite::Lock(&Lock);
						
							ETestFlags Status = ETestFlags::Success;

							const uint32 OwnershipAtEntry = OwnershipState.fetch_or(WRITER_OWNERSHIP_BIT, std::memory_order_relaxed);
							const bool bEstablishedWriterBit = (OwnershipAtEntry & WRITER_OWNERSHIP_BIT) == 0;
							if (OwnershipAtEntry != 0)
							{
								Status = ETestFlags::WriteOwnershipNonExclusive;
							}
							else
							{
								uint64 FirstValue = TestData[0];
						
								// forward sweep for memory tearing
								for (int32 X = 1; X < TestData.Num(); X++)
								{
									if (TestData[X] != FirstValue)
									{
										Status = ETestFlags::TornPublication;
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
								Status = ETestFlags::WriteOwnershipNonExclusive;
							}
						
							FQWrite::Unlock(&Lock);
						
							if (Status != ETestFlags::Success)
							{
								EnumExt::AddFlagsAtomic(TestFlags, Status);
								break;
							}
						}
					
						WorkerProgress[ID].store(i + 1, std::memory_order_relaxed);
					
						// variable gap before trying lock again to avoid lockstep and to try to reorder threads.
						const int32 JitterCycles = FMath::RandRange(1, 100);
						// using volatile for the same reason as above
						for (volatile int32 j = 0; j < JitterCycles; j++) {;} 
					
						if (TestFlags.load(std::memory_order_acquire) != ETestFlags::Success)
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
		
			if (TestFlags == ETestFlags::Success)
			{
				const int32 FinalSum = WriterCount * INNER_ITERATION_COUNT;
				for (int32 i = 0; i < TestData.Num(); i++)
				{
					if (TestData[i] != FinalSum)
					{
						EnumExt::AddFlagsAtomic(TestFlags, ETestFlags::SumIncorrect);
						break;
					}
				}
			}
	
			TUE_LOG("test iteration runtime = %.3f seconds", TestEnd - TestStart)
			if (TestFlags != ETestFlags::Success)
			{
				return false;
			}
		}
		
		return true;
	}

	bool JobStackTest()
	{
		return false;
	}

	bool ThreadJobberTest()
	{
		return false;
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
	
	Tests.Add("lock", {{"lock", LockTest}});
	Tests.Add("jobstack", {{"jobstack", JobStackTest}});
	Tests.Add("threadjobber", {{"threadjobber", ThreadJobberTest}});
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
			TUE_LOG("invalid argument %s", *Arg)
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
