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
		{ L"ShowCommands", &bShowCommands_ },
		{ L"ChromeDeveloper", &bChromeDeveloper_ },
		{ L"AutoViewTraces", &bAutoViewTraces_ },
		{ L"HeapStacks", &bHeapStacks_ },
	};

	for (auto& m : bools)
	{
		if (saving)
			pApp->WriteProfileInt(pSettings, m.pName, *m.pSetting);
		else
			*m.pSetting = pApp->GetProfileIntW(pSettings, m.pName, *m.pSetting) != false;
	}

	NameToRangedInt ints[] =
	{
		// Note that a setting of kKeyLoggerFull cannot be restored from
		// settings, to avoid privacy problems.
		{ L"InputTracing", (int*)&InputTracing_, kKeyLoggerOff, kKeyLoggerAnonymized },
		{ L"TracingMode", (int*)&tracingMode_, kTracingToFile, kHeapTracingToFile },
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

	NameToString strings[] =
	{
		{ L"HeapProfiledProcess", &heapTracingExe_ },
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
