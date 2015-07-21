#include "stdafx.h"
#include <string>

#include "PowerStatus.h"

const int kSamplingInterval = 2000;

int _tmain(int argc, _TCHAR* argv[])
{
	CPowerStatusMonitor monitor;
	if (!monitor.IsInitialized())
	{
		printf("Couldn't find Intel Power Gadget. Make sure it is installed and IPG_Dir environment variable is visible to this process.\n");
		return 0;
	}

	for (;;)
	{
		monitor.SampleCPUPowerState();
		Sleep(kSamplingInterval);
		wprintf(L"\n");
	}

	return 0;
}
