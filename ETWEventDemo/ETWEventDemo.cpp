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
#include <Windows.h>
#include "ETWProviders\etwprof.h"

/*
This demonstrates how to use ETWProviders*.dll to emit custom ETW events.
These events will be recorded by UIforETW, and the default WPA startup
profile configured by UIforETW will display them in the Generic Events
section.

See this blog post for an early mention of these providers.

https://randomascii.wordpress.com/2011/08/18/xperf-basics-recording-a-trace/
*/

// Pause for a short period of time with the CPU idle.
__declspec(noinline) void IdleDelay()
{
	Sleep(10);
}

// Pause for a short period of time with the CPU busy.
__declspec(noinline) void BusyDelay()
{
	DWORD startTick = GetTickCount();

	for (;;)
	{
		DWORD elapsed = GetTickCount() - startTick;
		if (elapsed > 10)
			break;
	}
}

int _tmain(int argc, _TCHAR* argv[])
{
  printf("Emitting custom ETW events that can be recorded with etwrecord.bat\n");
	CETWScope timer("main");
	for (int i = 0; i < 40; ++i)
	{
		ETWMarkPrintf("This is loop %d", i);
		ETWRenderFrameMark();
		// Simulating code that does something.
		IdleDelay();
		BusyDelay();
	}

	return 0;
}
