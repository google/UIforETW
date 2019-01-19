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
#include "UIforETW.h"
#include "UIforETWDlg.h"

/*
Helper file to put random big messy functions in, to avoid clutter
in UIforETWDlg.cpp. In other words, a second implementation file
for CUIforETWDlg.
*/

static const wchar_t* pSettings = L"Settings";

struct NameToBoolSetting
{
	const wchar_t* pName;
	bool* pSetting;
};

struct NameToRangedInt
{
	const wchar_t* pName;
	int* pSetting;
	int min, max;
};

struct NameToUInt64
{
	const wchar_t* pName;
	uint64_t* pSetting;
};

struct NameToString
{
	const wchar_t* pName;
	std::wstring* pSetting;
};

void CUIforETWDlg::TransferSettings(bool saving)
{
	CWinApp* pApp = AfxGetApp();

	NameToBoolSetting bools[] =
	{
		{ L"CompressTraces", &bCompress_ },
		{ L"CswitchStacks", &bCswitchStacks_ },
		{ L"SampledStacks", &bSampledStacks_ },
		{ L"FastSampling", &bFastSampling_ },
		{ L"GPUTracing", &bGPUTracing_ },
		{ L"CLRTracing", &bCLRTracing_ },
		{ L"ShowCommands", &bShowCommands_ },
		{ L"UseOtherKernelLogger", &bUseOtherKernelLogger_ },
		{ L"BackgroundMonitoring", &bBackgroundMonitoring_ },
		{ L"ChromeDeveloper", &bChromeDeveloper_ },
		{ L"IdentifyChromeProcessesCPU", &bIdentifyChromeProcessesCPU_ },
		{ L"AutoViewTraces", &bAutoViewTraces_ },
		{ L"RecordPreTrace", &bRecordPreTrace_ },
		{ L"HeapStacks", &bHeapStacks_ },
		{ L"VirtualAllocStacks", &bVirtualAllocStacks_ },
		{ L"VersionChecks", &bVersionChecks_ },
	};

	for (auto& m : bools)
	{
		if (saving)
			pApp->WriteProfileInt(pSettings, m.pName, *m.pSetting);
		else
			*m.pSetting = pApp->GetProfileIntW(pSettings, m.pName, *m.pSetting) != false;
	}

	static_assert(sizeof(InputTracing_) == sizeof(int), "Size mismatch.");
	static_assert(sizeof(tracingMode_) == sizeof(int), "Size mismatch.");
	NameToRangedInt ints[] =
	{
		// Note that a setting of kKeyLoggerFull cannot be restored from
		// settings, to avoid privacy problems.
		{ L"InputTracing", reinterpret_cast<int*>(&InputTracing_), kKeyLoggerOff, kKeyLoggerAnonymized },
		{ L"TracingMode", reinterpret_cast<int*>(&tracingMode_), kTracingToMemory, kHeapTracingToFile },
		{ L"PreviousWidth", &previousWidth_, minWidth_, maxWidth_ },
		{ L"PreviousHeight", &previousHeight_, minHeight_, maxHeight_ },
	};

	for (auto& m : ints)
	{
		if (saving)
			pApp->WriteProfileInt(pSettings, m.pName, *m.pSetting);
		else
		{
			int temp = pApp->GetProfileIntW(pSettings, m.pName, *m.pSetting);
			if (temp < m.min)
				temp = m.min;
			if (temp > m.max)
				temp = m.max;
			*m.pSetting = temp;
		}
	}

	NameToUInt64 bigInts[] =
	{
		{ L"ChromeKeywords", &chromeKeywords_ },
	};

	for (auto& m : bigInts)
	{
		if (saving)
		{
			pApp->WriteProfileBinary(pSettings, m.pName, reinterpret_cast<LPBYTE>(m.pSetting), sizeof(*m.pSetting));
		}
		else
		{
			LPBYTE temp = 0;
			UINT byteCount = sizeof(uint64_t);
			pApp->GetProfileBinary(pSettings, m.pName, &temp, &byteCount);
			if (byteCount == sizeof(uint64_t))
				*m.pSetting = *reinterpret_cast<uint64_t*>(temp);
			delete [] temp;
		}
	}

	NameToString strings[] =
	{
		{ L"HeapProfiledProcess", &heapTracingExes_ },
		{ L"WSMonitoredProcesses", &WSMonitoredProcesses_ },
		{ L"ExtraKernelFlags", &extraKernelFlags_ },
		{ L"ExtraStackWalks", &extraKernelStacks_ },
		{ L"ExtraUserProviders", &extraUserProviders_ },
		{ L"PerfCounters", &perfCounters_ },
	};

	for (auto& m : strings)
	{
		if (saving)
			pApp->WriteProfileStringW(pSettings, m.pName, m.pSetting->c_str());
		else
		{
			CString result = pApp->GetProfileStringW(pSettings, m.pName, m.pSetting->c_str());
			*m.pSetting = result;
		}
	}
}

// Increase the buffer count by some proportion when tracing to a file
// on a large-memory machine, in order to ensure that no events are lost
// even when tracing high-volume scenarios like multi-threaded builds.
// Buffer counts should not be adjusted when tracing to memory because
// these scenarios don't lose events, and increasing the buffer sizes
// will make the resultant files larger.
// In the worst-case (heap tracing) this function may be called three
// times for a trace and will request a total of about 2,000 buffers,
// each of which is 1 MB. Therefore we should only boost the number of
// buffers if the machine has enough memory to give up 2-4 GB of physical
// RAM without ill effects.
// Machines with over 15 GB of RAM (16 GB minus some overhead) should
// get a modest boost, and machines with over 30 GB of RAM should get
// a larger boost.
int CUIforETWDlg::BufferCountBoost(int requestCount) const noexcept
{
	// Saving traces from circular buffers in memory seems to be really
	// slow on some (dual socket?) machines and the 600 MB on medium to
	// large memory machines is excessive anyway - who wants traces that
	// big. So, this neatly haves the buffer sizes.
	if (tracingMode_ == kTracingToMemory)
		return requestCount / 2;

	int numerator = 1;
	int denominator = 1;
	MEMORYSTATUSEX memoryStatus = { sizeof(MEMORYSTATUSEX) };
	if (!GlobalMemoryStatusEx(&memoryStatus))
		return requestCount;

	const int64_t oneGB = static_cast<int64_t>(1024) * 1024 * 1024;
	const int64_t physicalRam = memoryStatus.ullTotalPhys;
	if (physicalRam > 15 * oneGB)
	{
		numerator = 3;
		denominator = 2;
	}
	if (physicalRam > 30 * oneGB)
	{
		numerator = 2;
		denominator = 1;
	}

	return (requestCount * numerator) / denominator;
}
