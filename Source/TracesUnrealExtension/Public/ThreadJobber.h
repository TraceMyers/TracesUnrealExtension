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

// part of a coordinated group. any instance can be used as an interface to make the whole group do work.
// intended usage (using game thread by default):
// 
// 1. add jobs to stack
// 2. call BeginWork()
// 3. do whatever on controller (probably game) thread
// 4. call JoinWork()
// 5. call ApplyResults()
// 
// adding jobs to the stack can be done at any time. the jobs will be added to the next workload,
// as they are double-buffered. PrepareForWork() rotates the buffers.
// 
// you can go through this loop multiple times per frame with the same team of jobbers, once
// per frame, or whatever. you can do so unconditionally as well. if no jobs were added, nothing happens.
//
// tested working with the "threads" test in Tests.cpp. On an AMD Ryzen 9, a
// series  of performance tests showed 0.6-0.8 microseconds of overhead per job (timers have been removed).
class TRACESUNREALEXTENSION_API FThreadJobber : public FRunnable
{
public:
	
	FThreadJobber() = delete;
	FThreadJobber(uint32 InID, const FThreadJobber* PrimaryJobber=nullptr, bool bInForceControlOnGameThread=true, uint32 StackSize=0);
	virtual ~FThreadJobber() override;
	
	// double-buffered stack by which you add jobs that will be worked on by jobbers
	TSharedPtr<FJobStackInterface> GetJobStack() { return JobStack; }
	// calls Prepare() on every job, then wakes worker threads and gets them working (calling Execute per job).
	void BeginWork() const;
	// whichever thread you created your jobbers from (usually game thread) is known internally
	// as the controller thread. only that thread can call JoinWork(), which makes said thread
	// go idle until work is complete.
	bool JoinWork(double TimeoutSeconds=1) const;
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
