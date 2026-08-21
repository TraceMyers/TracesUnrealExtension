#pragma once

#include "JobStack.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"

struct FThreadJobberInterface;
class FThreadJobber;

// convenience object to wrap an array of FThreadJobber. handles basic spawn and shutdown duty.
// interfacing with the group threads is done by interacting with any one of them, which is
// why GetMember() returns the first object in the team's array.
struct TRACESUNREALEXTENSION_API FThreadTeam
{
	FThreadTeam() = default;
	FThreadTeam(const FThreadTeam&) = delete;
	FThreadTeam& operator=(const FThreadTeam&) = delete;
	FThreadTeam(FThreadTeam&&) noexcept = default;
	FThreadTeam& operator=(FThreadTeam&&) noexcept = default;
	
	~FThreadTeam();
	
	// if ThreadCount == 0, will spawn the recommended number of threads given an expected load on ~3-4 default
	// Unreal threads at any given time.
	void Startup(size_t ThreadCount=0, bool bForceControlOnGameThread=true, int32 ThreadStackSize=0);
	
	// destroy all jobbers and empty the array
	void Shutdown();
	
	// get the first jobber in the array, which works as an interface to the team
	FThreadJobber* GetMember();
	
	TArray<TUniquePtr<FThreadJobber>> Jobbers;
};

// worker thread object intended for bulk work with non-crazy lock contention. Please note that this system
// is untested as of 8/20/26.
// 
// part of a coordinated group. any instance can be used as an interface to make the whole group do work.
// intended usage (using game thread by default):
// 
// 1. add jobs to stack
// 2. call PrepareForWork()
// 3. do any post-preparation.
// 4. call BeginWork()
// 5. do whatever on game thread
// 6. call JoinWork()
// 7. do any pre-apply results work.
// 8. call ApplyResults()
// 
// note: adding jobs to the stack can be done at any time. the jobs will be added to the next workload,
// as they are double-buffered. PrepareForWork() rotates the buffers.
// 
// note: you can go through this loop multiple times per frame with the same team of jobbers, once
// per frame, or whatever. you can do so unconditionally as well. if no jobs were added, nothing happens.
class TRACESUNREALEXTENSION_API FThreadJobber : public FRunnable
{
public:
	
	FThreadJobber() = delete;
	FThreadJobber(uint32 InID, const FThreadJobber* PrimaryJobber=nullptr, bool bInForceControlOnGameThread=true, uint32 StackSize=0);
	virtual ~FThreadJobber() override;
	
	// double-buffered stack by which you add jobs that will be worked on by jobbers
	TSharedPtr<FJobStackInterface> GetJobStack() { return JobStack; }
	// rotates job buffers so that all of the jobs since the last PrepareForWork() call (or since construction)
	// are up next. then, calls Prepare() on every job.
	void PrepareForWork() const;
	// wakes worker threads and 
	void BeginWork() const;
	// whichever thread you created your jobbers from (usually game thread) is known internally
	// as the controller thread. only that thread can call JoinWork(), which makes said thread
	// go idle until work is complete.
	void JoinWork() const;
	// calls ApplyResults() on every job
	void ApplyResults() const;
	
	void WaitForCompletion() const { Thread->WaitForCompletion(); }
	
	// FRunnable interface
	
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	
protected:
	
	FRunnableThread* Thread = nullptr;
	std::atomic<bool> bRun = true;
	uint32 ID = 0;
	
	TSharedPtr<FJobStack> JobStack = nullptr;
	FWorkerThreadWakeEvent WakeEvent {};
	
	// reasonable default safety mechanism to make sure some work that likely needs to happen on the game thread
	// does happen there, but if you need things your way, turn it off
	bool bForceControlOnGameThread = true;
};
