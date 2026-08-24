#include <string>
#include <thread>

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"

#include "ThreadJobber.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "UObject/ObjectMacros.h"


namespace
{
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
		TUE_LOG("fully parallel workload of %d jobs finished in %.3f ms, or ~%.4f ms per job", FULLY_PARALLEL_JOB_COUNT, FullyParallelRunTime, FullyParallelRunTime / FULLY_PARALLEL_JOB_COUNT)
		
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

const FAutoConsoleCommandWithWorldAndArgs TestCommand(TEXT("tue.test"), TEXT(""), FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	struct Test
	{
		FString Name;
		const TFunction<bool()> Proc;
	};
	
	TMap<FString, TArray<Test>> Tests;
	
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
