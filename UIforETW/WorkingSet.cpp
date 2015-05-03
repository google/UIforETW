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

#pragma comment(lib, "psapi.lib")

static HANDLE s_hThread;
static HANDLE s_hExitEvent;

const DWORD kSamplingInterval = 1000;

void SampleWorkingSets()
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
						ULONG_PTR privateWSPages = 0;
						for (ULONG_PTR page = 0; page < wsPages; ++page)
						{
							if (!pwsBuffer->WorkingSetInfo[page].Shared)
							{
								++privateWSPages;
							}
						}
						totalWSPages += wsPages;
						totalPrivateWSPages += privateWSPages;

						char buffer[MAX_PATH + 100];
						sprintf_s(buffer, "%s (%u) WS/private-WS in MB", processName, processIDs[i]);
						ETWMark2F(buffer, wsPages / 256.0f, privateWSPages / 256.0f);
					}
				}
			}

			CloseHandle(hProcess);
		}
	}
	ETWMark2F("Total:", totalWSPages / 256.0f, totalPrivateWSPages / 256.0f);
}

static DWORD __stdcall WSMonitorThread(LPVOID)
{
	for (;;)
	{
		DWORD result = WaitForSingleObject(s_hExitEvent, kSamplingInterval);
		if (result == WAIT_OBJECT_0)
			break;

		SampleWorkingSets();
	}

	return 0;
}

void StartWorkingSetMonitor()
{
	s_hExitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	s_hThread = CreateThread(NULL, 0, WSMonitorThread, NULL, 0, NULL);
}

void StopWorkingSetMonitor()
{
	SetEvent(s_hExitEvent);
	WaitForSingleObject(s_hThread, INFINITE);
	CloseHandle(s_hThread);
	CloseHandle(s_hExitEvent);
}
