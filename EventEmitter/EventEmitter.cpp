// Stand-alone tool for emitting ETW events to be recorded by
// scripts.

#include "stdafx.h"

#include "EventEmitter.h"
#include "UIforETW\CPUFrequency.h"
#include "UIforETW\KeyLoggerThread.h"
#include "UIforETW\PowerStatus.h"
#include "UIforETW\WorkingSet.h"

#include <assert.h>

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
	// There should really only ever be one instance of this process emitting events at
	// a time, but if there are many - kill them all!
	const LONG kKillCount = 1000;

	HANDLE requestStop = CreateSemaphore(nullptr, 0, kKillCount, L"UIforETWEventEmitterSemaphore");
	assert(requestStop);
	bool alreadyExisted = false;
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		// See if another instance of this process is already running, and
		// presumably emitting ETW evens.
		alreadyExisted = true;
	}

	if (wcsstr(GetCommandLine(), L"-kill"))
	{
		// Tell the already running process to stop. If there isn't an already
		// running process then this is a harmless no-op. Not checking for errors
		// here explicitly allows a 'safety kill' - running EventEmitter -kill
		// before running EventEmitter in order to get a known clean state.
		ReleaseSemaphore(requestStop, kKillCount, nullptr);
		return 0;
	}

	// If we weren't asked to kill an existing emitter then the assumption is
	// that we should run as an emitter. Do so.

	// Configuration stuff. Should really come from command-line parameters.
	std::wstring perfCounters = L"";
	std::wstring WSMonitoredProcesses = L"chrome.exe";
	bool bExpensiveWSMonitoring = true;

	bool bMonitorPowerStuff = true;
	bool bMonitorFrequencyStuff = false;
	bool bMonitorInput = true;

	CPowerStatusMonitor powerMonitor;
	powerMonitor.SetPerfCounters(perfCounters);
	if (bMonitorPowerStuff)
		powerMonitor.StartThreads();

	CCPUFrequencyMonitor CPUFrequencyMonitor;
	if (bMonitorFrequencyStuff)
		CPUFrequencyMonitor.StartThreads();

	CWorkingSetMonitor workingSetThread;
	if (!WSMonitoredProcesses.empty())
	{
		workingSetThread.SetProcessFilter(WSMonitoredProcesses, bExpensiveWSMonitoring);
		workingSetThread.StartThreads();
	}

	if (bMonitorInput)
		SetKeyloggingState(kKeyLoggerAnonymized);

	// Wait until a kill request comes in.
	WaitForSingleObject(requestStop, INFINITE);

	// Destructors will cleanly end all other threads, but the key logger needs
	// to be explicitly shut down. Well, not really since our process is going
	// away, but I prefer politeness.
	SetKeyloggingState(kKeyLoggerOff);

	return 0;
}
