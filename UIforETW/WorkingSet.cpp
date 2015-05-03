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
#include "Utility.h"
#include "WorkingSet.h"

#pragma comment(lib, "psapi.lib")

const DWORD kSamplingInterval = 1000;

static void SampleWorkingSets()
{
	DWORD processIDs[1024];
	DWORD cbReturned;

	// Get a list of process IDs.
	if (!EnumProcesses(processIDs, sizeof(processIDs), &cbReturned))
	{
		return;
	}

	const DWORD numProcesses = cbReturned / sizeof(processIDs[0]);

	ULONG_PTR numEntries = 100000;
	std::vector<char> buffer(sizeof(PSAPI_WORKING_SET_INFORMATION) + numEntries * sizeof(PSAPI_WORKING_SET_BLOCK));
	PSAPI_WORKING_SET_INFORMATION* pwsBuffer = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(&buffer[0]);

	// Iterate through the processes.
	uint64_t totalWSPages = 0;
	// The PSS page count is stored as a multiple of PSSMultiplier.
	// This allows all the supported share counts, from 1 to 7, to be
	// divided out without loss of precision. That is, an unshared page
	// is recorded by adding 420. A page shared by seven processes (the
	// maximum recorded) is recorded by adding 420/7.
	const uint64_t PSSMultiplier = 420; // LCM of 1, 2, 3, 4, 5, 6, 7
	uint64_t totalPSSPages = 0;
	uint64_t totalPrivateWSPages = 0;
	for (DWORD i = 0; i < numProcesses; ++i)
	{
		// Get a handle to the process.
		HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
			PROCESS_VM_READ, FALSE, processIDs[i]);

		if (NULL != hProcess)
		{
			HMODULE hMod;

			// Get the process name and working set.
			char processName[MAX_PATH];
			if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbReturned) &&
				GetModuleBaseNameA(hProcess, hMod, processName,
				ARRAYSIZE(processName)))
			{
				if (_stricmp(processName, "chrome.exe") == 0)
				{
					bool success = true;
					if (!QueryWorkingSet(hProcess, &buffer[0], buffer.size()))
					{
						// Increase the buffer size based on the NumberOfEntries returned,
						// with some padding in case the working set is increasing.
						if (GetLastError() == ERROR_BAD_LENGTH)
							numEntries = pwsBuffer->NumberOfEntries + pwsBuffer->NumberOfEntries / 4;
						buffer.resize(sizeof(PSAPI_WORKING_SET_INFORMATION) + numEntries * sizeof(PSAPI_WORKING_SET_BLOCK));
						pwsBuffer = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION*>(&buffer[0]);
						if (!QueryWorkingSet(hProcess, &buffer[0], buffer.size()))
						{
							success = false;
						}
					}

					if (success)
					{
						ULONG_PTR wsPages = pwsBuffer->NumberOfEntries;
						uint64_t PSSPages = 0;
						ULONG_PTR privateWSPages = 0;
						for (ULONG_PTR page = 0; page < wsPages; ++page)
						{
							if (!pwsBuffer->WorkingSetInfo[page].Shared)
							{
								++privateWSPages;
								PSSPages += PSSMultiplier;
							}
							else
							{
								assert(pwsBuffer->WorkingSetInfo[page].ShareCount <= 7);
								PSSPages += PSSMultiplier / pwsBuffer->WorkingSetInfo[page].ShareCount;
							}
						}
						totalWSPages += wsPages;
						totalPSSPages += PSSPages;
						totalPrivateWSPages += privateWSPages;

						char process[MAX_PATH + 100];
						sprintf_s(process, "%s (%u)", processName, processIDs[i]);
						ETWMarkWorkingSet(processName, process, privateWSPages / 256.0f, PSSPages / (256.0f * PSSMultiplier), wsPages / 256.0f);
					}
				}
			}

			CloseHandle(hProcess);
		}
	}
	ETWMarkWorkingSet("Total", "", totalPrivateWSPages / 256.0f, totalPSSPages / (256.0f * PSSMultiplier), totalWSPages / 256.0f);
}

DWORD __stdcall CWorkingSetMonitor::StaticWSMonitorThread(LPVOID param)
{
	CWorkingSetMonitor* pThis = reinterpret_cast<CWorkingSetMonitor*>(param);
	pThis->WSMonitorThread();
	return 0;
}

void CWorkingSetMonitor::WSMonitorThread()
{

	for (;;)
	{
		DWORD result = WaitForSingleObject(hExitEvent_, kSamplingInterval);
		if (result == WAIT_OBJECT_0)
			break;

		SampleWorkingSets();
	}
}

CWorkingSetMonitor::CWorkingSetMonitor()
{
	hExitEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	hThread_ = CreateThread(NULL, 0, StaticWSMonitorThread, this, 0, NULL);
}

CWorkingSetMonitor::~CWorkingSetMonitor()
{
	// Shut down the child thread.
	SetEvent(hExitEvent_);
	WaitForSingleObject(hThread_, INFINITE);
	CloseHandle(hThread_);
	CloseHandle(hExitEvent_);
}

void CWorkingSetMonitor::SetProcessFilter(const std::wstring& processes)
{
	CSingleLock locker(&processesLock_);
	processes_ = split(processes, ';');
}
