/*

Copyright © 2014 Bruce Dawson. All Rights Reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. The name of the author may not be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY Bruce Dawson "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "stdafx.h"
#include <Windows.h>
#include "ETWProviders\etwprof.h"

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
