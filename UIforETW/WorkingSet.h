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
};
