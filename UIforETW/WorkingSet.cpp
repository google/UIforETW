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
#include <ETWProviders\etwprof.h>
#define PSAPI_VERSION 1
#include <psapi.h>
#include <vector>
#include <cstdint>
#include <TlHelp32.h>
#include "Utility.h"
#include "WorkingSet.h"

#pragma comment(lib, "psapi.lib")

const DWORD kSamplingInterval = 1000;

void CWorkingSetMonitor::SampleWorkingSets()
{
	Locker locker(&processesLock_);
	if (processes_.empty() && !processAll_)
		return;

	// CreateToolhelp32Snapshot runs faster than EnumProcesses and
	// it returns the process name as well, thus avoiding a call to
	// EnumProcessModules to get the name.
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, TH32CS_SNAPPROCESS);
	if (!hSnapshot)
		return;

	PROCESSENTRY32W peInfo;
	peInfo.dwSize = sizeof(peInfo);
	BOOL nextProcess = Process32First(hSnapshot, &peInfo);

	// Allocate enough space to get the working set of most processes.
	// It will grow if needed.
	ULONG_PTR numEntries = 100000;

	const rsize_t bufferSizeNeeded =
		sizeof(PSAPI_WORKING_SET_INFORMATION) +
		(numEntries * sizeof(PSAPI_WORKING_SET_BLOCK));

	std::vector<char> buffer(bufferSizeNeeded);
	PSAPI_WORKING_SET_INFORMATION* pwsBuffer = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(buffer.data());

	ULONG_PTR totalWSPages = 0;
	// The PSS page count is stored as a multiple of PSSMultiplier.
	// This allows all the supported share counts, from 1 to 7, to be
	// divided out without loss of precision. That is, an unshared page
	// is recorded by adding 420. A page shared by seven processes (the
	// maximum recorded) is recorded by adding 420/7.
	const uint64_t PSSMultiplier = 420; // LCM of 1, 2, 3, 4, 5, 6, 7
	uint64_t totalPSSPages = 0;
	ULONG_PTR totalPrivateWSPages = 0;

	// Iterate through the processes.
	while (nextProcess)
	{
		bool match = processAll_;
		for (const auto& name : processes_)
		{
			if (_wcsicmp(peInfo.szExeFile, name.c_str()) == 0)
			{
				match = true;
			}
		}
		if (match)
		{
			const DWORD pid = peInfo.th32ProcessID;
			// Get a handle to the process.
			HANDLE hProcess =
				OpenProcess(PROCESS_QUERY_INFORMATION |
				PROCESS_VM_READ, FALSE, pid);
			ULONG_PTR wsPages = 0;
			uint64_t PSSPages = 0;
			ULONG_PTR privateWSPages = 0;

			if (NULL != hProcess)
			{
				bool success = true;
				if (bExpensiveWSMonitoring_)
				{
					if (!QueryWorkingSet(hProcess, &buffer[0], static_cast<DWORD>(buffer.size())))
					{
						success = false;
						// Increase the buffer size based on the NumberOfEntries returned,
						// with some padding in case the working set is increasing.
						if (GetLastError() == ERROR_BAD_LENGTH)
						{
							numEntries = pwsBuffer->NumberOfEntries + pwsBuffer->NumberOfEntries / 4;
							buffer.resize(sizeof(PSAPI_WORKING_SET_INFORMATION) + numEntries * sizeof(PSAPI_WORKING_SET_BLOCK));
							pwsBuffer = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(&buffer[0]);
							if (QueryWorkingSet(hProcess, &buffer[0], static_cast<DWORD>(buffer.size())))
							{
								success = true;
							}
						}
					}

					if (success)
					{
						wsPages = pwsBuffer->NumberOfEntries;
						for (ULONG_PTR page = 0; page < wsPages; ++page)
						{
							if (!pwsBuffer->WorkingSetInfo[page].Shared)
							{
								++privateWSPages;
								PSSPages += PSSMultiplier;
							}
							else
							{
								UIETWASSERT(pwsBuffer->WorkingSetInfo[page].ShareCount <= 7);
								PSSPages += PSSMultiplier / pwsBuffer->WorkingSetInfo[page].ShareCount;
							}
						}
						totalPSSPages += PSSPages;
						totalPrivateWSPages += privateWSPages;
					}
				}
				else
				{
					PROCESS_MEMORY_COUNTERS memoryCounters = {sizeof(memoryCounters)};
					if (GetProcessMemoryInfo(hProcess, &memoryCounters, sizeof(memoryCounters)))
					{
						wsPages = memoryCounters.WorkingSetSize / 4096;
					}
				}
				if (success)
				{
					totalWSPages += wsPages;

					wchar_t process[MAX_PATH + 100];
					swprintf_s(process, L"%s (%u)", peInfo.szExeFile, pid);
					ETWMarkWorkingSet(peInfo.szExeFile, process, counter_, static_cast<unsigned>(privateWSPages * 4), static_cast<unsigned>((PSSPages * 4) / PSSMultiplier), static_cast<unsigned>((wsPages * 4)));
				}

				CloseHandle(hProcess);
			}
		}
		nextProcess = Process32Next(hSnapshot, &peInfo);
	}
	CloseHandle(hSnapshot);

	ETWMarkWorkingSet(L"Total", L"", counter_, static_cast<unsigned>(totalPrivateWSPages * 4), static_cast<unsigned>((totalPSSPages * 4) / PSSMultiplier), static_cast<unsigned>(totalWSPages * 4));
	++counter_;
}

DWORD __stdcall CWorkingSetMonitor::StaticWSMonitorThread(LPVOID param)
{
	SetCurrentThreadName("Working set monitor");

	CWorkingSetMonitor* pThis = static_cast<CWorkingSetMonitor*>(param);
	pThis->WSMonitorThread();
	return 0;
}

void CWorkingSetMonitor::WSMonitorThread()
{

	for (;;)
	{
		const DWORD result = WaitForSingleObject(hExitEvent_, kSamplingInterval);
		if (result == WAIT_OBJECT_0)
			break;

		SampleWorkingSets();
	}
}

CWorkingSetMonitor::CWorkingSetMonitor() noexcept
{
}

void CWorkingSetMonitor::StartThreads()
{
	UIETWASSERT(!hExitEvent_);
	if (!hExitEvent_)
	{
		// The working set monitoring is not needed on Windows 8.1 and above because
		// of the Microsoft-Windows-Kernel-Memory provider.
		if (!IsWindows8Point1OrGreater())
		{
			hExitEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			hThread_ = CreateThread(NULL, 0, StaticWSMonitorThread, this, 0, NULL);
		}
	}
}

void CWorkingSetMonitor::StopThreads() noexcept
{
	if (hExitEvent_)
	{
		// Shut down the child thread.
		SetEvent(hExitEvent_);
		WaitForSingleObject(hThread_, INFINITE);
		CloseHandle(hThread_);
		hThread_ = nullptr;
		CloseHandle(hExitEvent_);
		hExitEvent_ = nullptr;
	}
}

CWorkingSetMonitor::~CWorkingSetMonitor()
{
	StopThreads();
}

void CWorkingSetMonitor::SetProcessFilter(const std::wstring& processes, bool bExpensiveWSMonitoring)
{
	// A 32-bit process on 64-bit Windows will not be able to read the
	// full working set of 64-bit processes, so don't even try.
	if (Is64BitWindows() && !Is64BitBuild())
		return;
	Locker locker(&processesLock_);
	if (processes == L"*")
	{
		processAll_ = true;
	}
	else
	{
		processAll_ = false;
		processes_ = split(processes, ';');
	}
	bExpensiveWSMonitoring_ = bExpensiveWSMonitoring;
}
