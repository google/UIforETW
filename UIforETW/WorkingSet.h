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
#include <string>

class CWorkingSetMonitor
{
public:
	CWorkingSetMonitor();
	~CWorkingSetMonitor();

	// Pass in a semi-colon separated list of process names that
	// the working set display should monitor. If this is the
	// empty string then no processes will be monitored.
	// If this is '*' then all processes will be monitored.
	void SetProcessFilter(const std::wstring& processes);
private:
	static DWORD __stdcall StaticWSMonitorThread(LPVOID);
	void WSMonitorThread();

	void SampleWorkingSets();

	HANDLE hThread_;
	HANDLE hExitEvent_;

	CCriticalSection processesLock_;
	// This is a list of process names to monitor. If the list is
	// empty then no processes are monitored. If this list has one
	// entry that is '*' then all processes are monitored. Otherwise
	// processes are monitored if their name (without path, case
	// insensitive) matches one of the entries.
	// This variable is protected by processesLock_;
	std::vector<std::wstring> processes_;
	// This variable is protected by processesLock_;
	bool processAll_ = false;

	// Incrementing counter that will be the same for all samples recorded
	// at the same time.
	unsigned counter_ = 0;
};
