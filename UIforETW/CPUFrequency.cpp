/*
Copyright 2015 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "stdafx.h"
#include "PowrProf.h"
#include <algorithm>
#include "CPUFrequency.h"
#include "Utility.h"
#include "ETWProviders\etwprof.h"

// NTSTATUS definition copied from bcrypt.h because including
// ntdef.h fails to compile.
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

#pragma comment(lib, "PowrProf.lib")

// How many ms to wait between sampling the CPU speeds. Don't sample
// 'too' fast or it will waste power and affect the results.
const DWORD kSamplingInterval = 3000;
// Do this many samples each time and take the fastest. That helps to
// avoid problems where an interrupt or other blip causes a CPU to miss
// some cycles occasionally. This should be set as low as possible.
const int kRetryCount = 5;
// How many iterations of SpinALot to make each time. This should be
// set as low as possible.
// A spin count of 10,000 implies 500,000 cycles for each call to
// MeasureFrequencyOnce() which at 800 MHz is 0.625 ms. If kRetryCount is 7
// then that is 4.375 ms which may be enough to trigger raising the
// CPU frequency and perturbing what we are measuring. A spin count
// of 2,000 should complete in under a ms when the CPU is at its
// lowest speed, and far faster when the CPU is running fast.
const int kSpinCount = 2000;

// Spin in a loop for 50*spinCount cycles - this function is defined
// in 32-bit and 64-bit specific .asm files.
extern "C" void SpinALot(int spinCount);
const int kSpinsPerLoop = 50;

// Calculate the frequency of the current CPU in MHz, one time.
static float MeasureFrequencyOnce()
{
	QPCElapsedTimer timer;
	// Spin for kSpinsPerLoop * kSpinCount cycles. This should
	// be fast enough to usually avoid any interrupts.
	SpinALot(kSpinCount);
	auto elapsed = timer.ElapsedSeconds();
	// Calculate the frequency in MHz.
	auto frequency = ((kSpinCount * kSpinsPerLoop) / elapsed) / 1e6;
	return static_cast<float>(frequency);
}

// Calculate the current CPU frequency multiple times and
// return the largest frequency seen.
static float MeasureFrequency(int iterations)
{
	float maxFrequency = 0.0;
	for (int i = 0; i < iterations; ++i)
	{
		float QPCFrequency = MeasureFrequencyOnce();
		if (QPCFrequency > maxFrequency)
			maxFrequency = QPCFrequency;
	}
	return maxFrequency;
}

void CCPUFrequencyMonitor::PerCPUSamplingThread(int cpuNumber)
{
	for (;;)
	{
		// Wait until its time to measure the CPU frequency.
		WaitForSingleObject(workStartSemaphore_, INFINITE);
		if (quit_)
			break;

		float frequency = MeasureFrequency(kRetryCount);
		threads_[cpuNumber].frequency = frequency;

		ReleaseSemaphore(resultsDoneSemaphore_, 1, nullptr);
	}
}

DWORD __stdcall CCPUFrequencyMonitor::StaticPerCPUSamplingThread(LPVOID param)
{
	SetCurrentThreadName("CPU frequency measuring thread");

	auto* pState = reinterpret_cast<CPUSamplerState*>(param);
	pState->pOwner->PerCPUSamplingThread(pState->cpuNumber);
	return 0;
}

/*
From https://msdn.microsoft.com/en-us/library/windows/desktop/aa373184(v=vs.85).aspx:

Note that this structure definition was accidentally omitted from WinNT.h. This
error will be corrected in the future. In the meantime, to compile your
application, include the structure definition contained in this topic in your
source code.
*/
typedef struct _PROCESSOR_POWER_INFORMATION {
	ULONG Number;
	ULONG MaxMhz;
	ULONG CurrentMhz;
	ULONG MhzLimit;
	ULONG MaxIdleState;
	ULONG CurrentIdleState;
} PROCESSOR_POWER_INFORMATION, *PPROCESSOR_POWER_INFORMATION;

// This thread controls the CPU frequency measurements.
void CCPUFrequencyMonitor::Sample()
{
	// Get the actual number of CPUs, in case numCPUs_ was trimmed.
	// Otherwise CallNtPowerInformation won't return any data.
	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	DWORD actualNumberCPUs = systemInfo.dwNumberOfProcessors;

	// Ask Windows what frequency it thinks the CPUs are running at.
	std::vector<PROCESSOR_POWER_INFORMATION> processorInfo(actualNumberCPUs);
	NTSTATUS powerStatus = CallNtPowerInformation(ProcessorInformation, nullptr,
		0, &processorInfo[0],
		sizeof(processorInfo[0]) * actualNumberCPUs);

	ULONG maxPromisedMHz = 0;
	// Most common failure result is:
	// #define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
	if (powerStatus >= 0)
	{
		for (DWORD i = 0; i < actualNumberCPUs; ++i)
		{
			maxPromisedMHz = std::max(maxPromisedMHz, processorInfo[i].CurrentMhz);
		}
	}

	// Release all of the per-CPU measurement threads. This is not
	// actually guaranteed to wake up all of the threads - one thread
	// could 'absorb' all of the semaphores, but in practice this works
	// very well. Releasing numCPUs_ different semaphores would guarantee
	// that all of the individual threads would run, but would require
	// this thread to run more code (releasing individual semaphores) which
	// might cause more worrisome interference.
	ReleaseSemaphore(workStartSemaphore_, numCPUs_, nullptr);

	// Sleep a little while to give the calculation threads time to finish
	// without this thread interfering.
	Sleep(10);

	// Make sure the CPU frequency measurements are finished.
	for (unsigned i = 0; i < numCPUs_; ++i)
		WaitForSingleObject(resultsDoneSemaphore_, INFINITE);

	// Find the maximum measured frequencies.
	float maxActualFreq = threads_[0].frequency;
	for (DWORD i = 1; i < numCPUs_; ++i)
	{
		maxActualFreq = std::max(maxActualFreq, threads_[i].frequency);
	}

	float freqPercentage = maxActualFreq * 100.f / maxPromisedMHz;
	PCWSTR pStatus = L"Normal";
	if (freqPercentage < 75)
		pStatus = L"Probably modest thermal throttling";
	if (freqPercentage < 50)
		pStatus = L"Probably significant thermal throttling";
	ETWMarkCPUThrottling(startFrequency_, maxActualFreq, static_cast<float>(maxPromisedMHz), freqPercentage, pStatus);
}

void CCPUFrequencyMonitor::MonitorThread()
{

	for (;;)
	{
		DWORD result = WaitForSingleObject(hExitEvent_, kSamplingInterval);
		if (result == WAIT_OBJECT_0)
			break;

		Sample();
	}
}

DWORD __stdcall CCPUFrequencyMonitor::StaticMonitorThread(LPVOID param)
{
	SetCurrentThreadName("CPU frequency monitor thread");

	auto* pThis = reinterpret_cast<CCPUFrequencyMonitor*>(param);
	pThis->MonitorThread();
	return 0;
}

CCPUFrequencyMonitor::CCPUFrequencyMonitor()
{
	// Must do all this before creating the child threads.

	SYSTEM_INFO systemInfo;
	GetSystemInfo(&systemInfo);
	numCPUs_ = systemInfo.dwNumberOfProcessors;
	// Clamp to the number of processors that can be identified by
	// SetThreadAffinityMask - 32 or 64.
	if (numCPUs_ > sizeof(DWORD_PTR) * 8)
		numCPUs_ = sizeof(DWORD_PTR) * 8;
	threads_.resize(numCPUs_);

	workStartSemaphore_ = CreateSemaphore(nullptr, 0, numCPUs_, nullptr);
	resultsDoneSemaphore_ = CreateSemaphore(nullptr, 0, numCPUs_, nullptr);
	if (!workStartSemaphore_ || !resultsDoneSemaphore_)
		return;

	// Create numCPUs_ threads, set their affinity to individual CPU
	// cores, and raise their priority. This will mostly ensure that they
	// run promptly and on the desired CPU core.
	for (unsigned i = 0; i < numCPUs_; ++i)
	{
		threads_[i].pOwner = this;
		threads_[i].cpuNumber = i;
		HANDLE hThread = CreateThread(nullptr, 0x10000, StaticPerCPUSamplingThread, &threads_[i], 0, nullptr);
		if (hThread)
		{
			SetThreadAffinityMask(hThread, DWORD_PTR(1) << i);
			SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);
		}
		threads_[i].hThread = hThread;
	}

	// Get the initial frequency
	QPCElapsedTimer timer;
	// Run the test long enough so that the OS will ramp up the CPU to
	// full speed.
	startFrequency_ = MeasureFrequency(600);
	float testElapsed = static_cast<float>(timer.ElapsedSeconds());

	ETWMark2F("Startup CPU frequency (MHz) and measurement time (s)", startFrequency_, testElapsed);

	// Once the monitor thread is created the other threads will start
	// being told to do measurements occasionally.
	hExitEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	hThread_ = CreateThread(NULL, 0, StaticMonitorThread, this, 0, NULL);
}

CCPUFrequencyMonitor::~CCPUFrequencyMonitor()
{
	// Shut down the child thread.
	SetEvent(hExitEvent_);
	WaitForSingleObject(hThread_, INFINITE);
	CloseHandle(hThread_);
	CloseHandle(hExitEvent_);

	// Shut down the measurement threads.
	quit_ = true;
	ReleaseSemaphore(workStartSemaphore_, numCPUs_, nullptr);
	for (auto& thread : threads_)
	{
		// Wait for the measurement threads to exit and then close
		// the thread handles.
		if (thread.hThread)
		{
			WaitForSingleObject(thread.hThread, INFINITE);
			CloseHandle(thread.hThread);
		}
	}

	CloseHandle(workStartSemaphore_);
	CloseHandle(resultsDoneSemaphore_);
}
