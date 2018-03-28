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

#pragma once

#include <vector>
#include <atomic>

class CCPUFrequencyMonitor
{
public:
	CCPUFrequencyMonitor() noexcept;
	~CCPUFrequencyMonitor();

	// Start and stop the sampling threads so that they aren't running
	// when tracing is not running.
	void StartThreads();
	void StopThreads();

private:
	// Call this function with the 'this' pointer.
	static DWORD __stdcall StaticMonitorThread(LPVOID);
	void MonitorThread();

	void Sample();

	// Call this with a pointer to a CPUSamplerState object.
	static DWORD __stdcall StaticPerCPUSamplingThread(LPVOID);
	void PerCPUSamplingThread(int cpuNumber);

	// Pass this to StaticPerCPUSamplingThread to initialize those threads.
	struct CPUSamplerState
	{
		CCPUFrequencyMonitor* pOwner;
		int cpuNumber; // Used to give each thread a CPU number
		HANDLE hThread;
		float frequency; // Measured frequency
	};

	// The number of CPUs that we monitor - may be less than the actual number
	// of CPUs on some crazy multi-core machines.
	unsigned numCPUs_ = 0;
	// Startup information for sampling threads.
	std::vector<CPUSamplerState> threads_;

	// When this is set then the workStartSemaphore will cause the per-CPU
	// threads to exit.
	std::atomic_bool quit_;

	// Semaphores to start measurement and wait for results.
	HANDLE workStartSemaphore_ = nullptr;
	HANDLE resultsDoneSemaphore_ = nullptr;

	float startFrequency_ = 0.0f;

	// Handle to monitor thread and event to tell it to halt.
	HANDLE hThread_ = nullptr;
	HANDLE hExitEvent_ = nullptr;

	CCPUFrequencyMonitor(const CCPUFrequencyMonitor&) = delete;
	CCPUFrequencyMonitor(const CCPUFrequencyMonitor&&) = delete;
	CCPUFrequencyMonitor& operator=(const CCPUFrequencyMonitor&) = delete;
	CCPUFrequencyMonitor& operator=(const CCPUFrequencyMonitor&&) = delete;
};
