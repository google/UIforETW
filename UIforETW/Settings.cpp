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
#include "Settings.h"
#include "Utility.h"

#include <pdh.h>
#include <pdhmsg.h>

#pragma comment(lib, "pdh.lib")

/*
When doing Chrome profilng it is possible to ask various Chrome tracing
categories to emit ETW events. The filtered_event_group_names array
documents what categories map to what flag/keyword values.

The table below is subject to change, but probably not very frequently.

For more details see this Chrome change that added the initial version of the
ETW event filtering:
https://codereview.chromium.org/1176243016
*/

// Copied from Chrome's trace_event_etw_export_win.cc. This file can be found by
// searching for "f:trace_event_etw_export_win.cc" or kFilteredEventGroupNames
// in https://code.google.com/p/chromium/codesearch#/.
const PCWSTR filtered_event_group_names[] =
{
	L"benchmark",                                       // 0x1
	L"blink",                                           // 0x2
	L"browser",                                         // 0x4
	L"cc",                                              // 0x8
	L"evdev",                                           // 0x10
	L"gpu",                                             // 0x20
	L"input",                                           // 0x40
	L"netlog",                                          // 0x80
	L"renderer.scheduler",                              // 0x100
	L"toplevel",                                        // 0x200
	L"v8",                                              // 0x400
	L"disabled-by-default-cc.debug",                    // 0x800
	L"disabled-by-default-cc.debug.picture",            // 0x1000
	L"disabled-by-default-toplevel.flow",               // 0x2000
	L"startup",                                         // 0x4000
};

// 1ULL << 61 and 1ULL << 62 are special values that indicate to Chrome to
// enable all enabled-by-default and disabled-by-default categories
// respectively.
const PCWSTR other_events_group_name = L"All enabled-by-default events";  // 0x2000000000000000
const PCWSTR disabled_other_events_group_name = L"All disabled-by-default events";  // 0x4000000000000000
uint64_t other_events_keyword_bit = 1ULL << 61;
uint64_t disabled_other_events_keyword_bit = 1ULL << 62;

// CSettings dialog

IMPLEMENT_DYNAMIC(CSettings, CDialog)

CSettings::CSettings(CWnd* pParent /*=NULL*/, const std::wstring& exeDir, const std::wstring& wpt81Dir, const std::wstring& wpt10Dir)
	: CDialog(CSettings::IDD, pParent)
	, exeDir_(exeDir)
	, wpt81Dir_(wpt81Dir)
	, wpt10Dir_(wpt10Dir)
{

}

CSettings::~CSettings()
{
}

void CSettings::DoDataExchange(CDataExchange* pDX)
{
	// This is needed to get tooltips working.
	DDX_Control(pDX, IDC_HEAPEXE, btHeapTracingExe_);
	DDX_Control(pDX, IDC_WSMONITOREDPROCESSES, btWSMonitoredProcesses_);
	DDX_Control(pDX, IDC_EXPENSIVEWS, btExpensiveWSMonitoring_);
	DDX_Control(pDX, IDC_EXTRAKERNELFLAGS, btExtraKernelFlags_);
	DDX_Control(pDX, IDC_EXTRASTACKWALKS, btExtraStackwalks_);;
	DDX_Control(pDX, IDC_EXTRAUSERMODEPROVIDERS, btExtraUserProviders_);
	DDX_Control(pDX, IDC_PERFORMANCECOUNTERS, btPerfCounters_);
	DDX_Control(pDX, IDC_COPYSTARTUPPROFILE, btCopyStartupProfile_);
	DDX_Control(pDX, IDC_USE_OTHER_KERNEL_LOGGER, btUseOtherKernelLogger_);
	DDX_Control(pDX, IDC_CHROMEDEVELOPER, btChromeDeveloper_);
	DDX_Control(pDX, IDC_AUTOVIEWTRACES, btAutoViewTraces_);
	DDX_Control(pDX, IDC_HEAPSTACKS, btHeapStacks_);
	DDX_Control(pDX, IDC_VIRTUALALLOCSTACKS, btVirtualAllocStacks_);
	DDX_Control(pDX, IDC_CHECKFORNEWVERSIONS, btVersionChecks_);
	DDX_Control(pDX, IDC_CHROME_CATEGORIES, btChromeCategories_);

	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CSettings, CDialog)
	ON_BN_CLICKED(IDC_COPYSTARTUPPROFILE, &CSettings::OnBnClickedCopystartupprofile)
	ON_BN_CLICKED(IDC_CHROMEDEVELOPER, &CSettings::OnBnClickedChromedeveloper)
	ON_BN_CLICKED(IDC_AUTOVIEWTRACES, &CSettings::OnBnClickedAutoviewtraces)
	ON_BN_CLICKED(IDC_HEAPSTACKS, &CSettings::OnBnClickedHeapstacks)
	ON_BN_CLICKED(IDC_VIRTUALALLOCSTACKS, &CSettings::OnBnClickedVirtualallocstacks)
	ON_BN_CLICKED(IDC_EXPENSIVEWS, &CSettings::OnBnClickedExpensivews)
	ON_BN_CLICKED(IDC_CHECKFORNEWVERSIONS, &CSettings::OnBnClickedCheckfornewversions)
	ON_BN_CLICKED(IDC_SELECT_PERF_COUNTERS, &CSettings::OnBnClickedSelectPerfCounters)
	ON_BN_CLICKED(IDC_USE_OTHER_KERNEL_LOGGER, &CSettings::OnBnClickedUseOtherKernelLogger)
END_MESSAGE_MAP()

BOOL CSettings::OnInitDialog()
{
	CDialog::OnInitDialog();

	SetDlgItemText(IDC_HEAPEXE, heapTracingExes_.c_str());
	CheckDlgButton(IDC_USE_OTHER_KERNEL_LOGGER, bUseOtherKernelLogger_);
	CheckDlgButton(IDC_CHROMEDEVELOPER, bChromeDeveloper_);
	CheckDlgButton(IDC_AUTOVIEWTRACES, bAutoViewTraces_);
	CheckDlgButton(IDC_HEAPSTACKS, bHeapStacks_);
	CheckDlgButton(IDC_VIRTUALALLOCSTACKS, bVirtualAllocStacks_);
	CheckDlgButton(IDC_CHECKFORNEWVERSIONS, bVersionChecks_);

	if (IsWindows8Point1OrGreater())
	{
		// The working set monitoring is not needed on Windows 8.1 and above because
		// of the Microsoft-Windows-Kernel-Memory provider.
		btWSMonitoredProcesses_.EnableWindow(FALSE);
		btExpensiveWSMonitoring_.EnableWindow(FALSE);
		GetDlgItem(IDC_WS_MONITOR_STATIC)->EnableWindow(FALSE);
	}
	else
	{
		// A 32-bit process on 64-bit Windows will not be able to read the
		// full working set of 64-bit processes, so don't even try.
		if (Is64BitWindows() && !Is64BitBuild())
			btWSMonitoredProcesses_.EnableWindow(FALSE);
		else
			SetDlgItemText(IDC_WSMONITOREDPROCESSES, WSMonitoredProcesses_.c_str());
		CheckDlgButton(IDC_EXPENSIVEWS, bExpensiveWSMonitoring_);
	}
	btExtraKernelFlags_.SetWindowTextW(extraKernelFlags_.c_str());
	btExtraStackwalks_.SetWindowTextW(extraKernelStacks_.c_str());
	btExtraUserProviders_.SetWindowTextW(extraUserProviders_.c_str());
	btPerfCounters_.SetWindowTextW(perfCounters_.c_str());

	if (toolTip_.Create(this))
	{
		toolTip_.SetMaxTipWidth(400);
		toolTip_.Activate(TRUE);

		toolTip_.AddTool(&btHeapTracingExe_, L"Specify the file names of the exes to be heap traced, "
					L"separated by semi-colons. "
					L"Enter just the file parts (with the .exe extension) not a full path. For example, "
					L"'chrome.exe;notepad.exe'. This is for use with the heap-tracing-to-file mode.");
		toolTip_.AddTool(&btExtraKernelFlags_, L"Extra kernel flags, separated by '+', such as "
					L"\"REGISTRY+PERF_COUNTER\". See \"xperf -providers k\" for the full list. "
					L"Note that incorrect kernel flags will cause tracing to fail to start.");
		toolTip_.AddTool(&btExtraStackwalks_, L"List of extra stacks to collect from the kernel "
					L"kernel provider. For example, \n\"DiskReadInit+DiskWriteInit+DiskFlushInit\". "
					L"Run \"xperf -help stackwalk\" to see the full list. Note that incorrect stack "
					L"walk flags will cause tracing to fail to start. Note also that stack walk flags "
					L"are ignored if the corresponding kernel flag is not enabled.");
		toolTip_.AddTool(&btExtraUserProviders_, L"Extra user providers, separated by '+', such as "
					L"\n\"Microsoft-Windows-Audio+Microsoft-Windows-HttpLog\". See \"xperf -providers\" "
					L"for the full list. "
					L"TraceLogging and EventSource providers must be prefixed by '*'. "
					L"Note that incorrect user providers will cause tracing to fail to start.");
		toolTip_.AddTool(&btPerfCounters_, L"Arbitrary performance counters to be logged occasionally.");
		toolTip_.AddTool(&btWSMonitoredProcesses_, L"Names of processes whose working sets will be "
					L"monitored, separated by semi-colons. An empty string means no monitoring. A '*' means "
					L"that all processes will be monitored. For instance 'chrome.exe;notepad.exe'");
		toolTip_.AddTool(&btExpensiveWSMonitoring_, L"Check this to have private working set and PSS "
					L"(proportional set size) calculated for monitored processes. This may consume "
					L"dozens or hundreds of ms each time. Without this checked only full working "
					L"set is calculated, which is cheap.");
		toolTip_.AddTool(&btCopyStartupProfile_, L"Copies startup.wpaProfile files for WPA 8.1 and "
					L"10 to the appropriate destinations so that the next time WPA starts up it will have "
					L"reasonable analysis defaults.");
		toolTip_.AddTool(&btUseOtherKernelLogger_, L"Check this to have UIforETW use the alternate kernel "
					L"logger. This is needed on some machines where the main kernel logger is in use.");
		toolTip_.AddTool(&btChromeDeveloper_, L"Check this to enable Chrome specific behavior such as "
					L"setting the Chrome symbol server path, and preprocessing of Chrome symbols and "
					L"traces.");
		toolTip_.AddTool(&btAutoViewTraces_, L"Check this to have UIforETW launch the trace viewer "
					L"immediately after a trace is recorded.");
		toolTip_.AddTool(&btHeapStacks_, L"Check this to record call stacks on HeapAlloc, HeapRealloc, "
					L"and similar calls, when doing heap traces.");
		toolTip_.AddTool(&btVirtualAllocStacks_, L"Check this to record call stacks on VirtualAlloc on all "
					L"traces instead of just heap traces.");
		toolTip_.AddTool(&btVersionChecks_, L"Check this to have UIforETW check for new versions at startup.");
		toolTip_.AddTool(&btChromeCategories_, L"Check the chrome tracing categories that you want Chrome "
					L"to emit ETW events for. This requires running Chrome version 46 or later, and "
					L"using chrome://flags/ to \"Enable exporting of tracing events to ETW\" - search for "
					L"trace-export on the chrome://flags/ page.");
	}

	// Initialize the list of check boxes with all of the Chrome categories which
	// we can enable individually.
	btChromeCategories_.SetCheckStyle(BS_AUTOCHECKBOX);
	int index = 0;
	for (auto category : filtered_event_group_names)
	{
		btChromeCategories_.AddString(category);
		btChromeCategories_.SetCheck(index, (chromeKeywords_ & (1LL << index)) != 0);
		++index;
	}
	// Manually add the two special Chrome category options.
	btChromeCategories_.AddString(other_events_group_name);
	btChromeCategories_.SetCheck(index, (chromeKeywords_ & other_events_keyword_bit) != 0);
	++index;
	btChromeCategories_.AddString(disabled_other_events_group_name);
	btChromeCategories_.SetCheck(index, (chromeKeywords_ & disabled_other_events_keyword_bit) != 0);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CSettings::OnOK()
{
	heapTracingExes_ = GetEditControlText(btHeapTracingExe_);
	WSMonitoredProcesses_ = GetEditControlText(btWSMonitoredProcesses_);
	extraKernelStacks_ = GetEditControlText(btExtraStackwalks_);
	extraKernelFlags_ = GetEditControlText(btExtraKernelFlags_);
	extraUserProviders_ = GetEditControlText(btExtraUserProviders_);
	perfCounters_ = GetEditControlText(btPerfCounters_);

	// Extract the Chrome categories settings and put the result in chromeKeywords_.
	chromeKeywords_ = 0;
	int index = 0;
	for (/**/; index < ARRAYSIZE(filtered_event_group_names); ++index)
	{
		if (btChromeCategories_.GetCheck(index))
			chromeKeywords_ |= 1LL << index;
	}
	// Manually grab values for the two special Chrome category options
	if (btChromeCategories_.GetCheck(index))
		chromeKeywords_ |= other_events_keyword_bit;
	++index;
	if (btChromeCategories_.GetCheck(index))
		chromeKeywords_ |= disabled_other_events_keyword_bit;

	CDialog::OnOK();
}


BOOL CSettings::PreTranslateMessage(MSG* pMsg)
{
	toolTip_.RelayEvent(pMsg);
	return CDialog::PreTranslateMessage(pMsg);
}


void CSettings::OnBnClickedCopystartupprofile()
{
	CopyStartupProfiles(exeDir_, true);
}


void CSettings::OnBnClickedChromedeveloper()
{
	bChromeDeveloper_ = !bChromeDeveloper_;
}


void CSettings::OnBnClickedAutoviewtraces()
{
	bAutoViewTraces_ = !bAutoViewTraces_;
}


void CSettings::OnBnClickedHeapstacks()
{
	bHeapStacks_ = !bHeapStacks_;
}


void CSettings::OnBnClickedVirtualallocstacks()
{
	bVirtualAllocStacks_ = !bVirtualAllocStacks_;
}


void CSettings::OnBnClickedExpensivews()
{
	bExpensiveWSMonitoring_ = !bExpensiveWSMonitoring_;
}


void CSettings::OnBnClickedCheckfornewversions()
{
	bVersionChecks_ = !bVersionChecks_;
}

struct CallbackArg {
	std::vector<std::wstring>* counters;
	PDH_BROWSE_DLG_CONFIG* config;
	std::vector<wchar_t> counter_names;
};

PDH_STATUS __stdcall CounterCallBack(
	_In_ DWORD_PTR dwArg
) {
	CallbackArg* arg = reinterpret_cast<CallbackArg*>(dwArg);

	/*
	// The documentation suggests that this code will be used, but I have
	// never been able to invoke it.
	if (arg->config->CallBackStatus == PDH_MORE_DATA) {
		arg->counter_names.resize(arg->counter_names.size() * 2); // Annoyingly, the callback does not tell us how much space is needed
		arg->config->szReturnPathBuffer = arg->counter_names.data();
		arg->config->cchReturnPathLength = (DWORD)arg->counter_names.size();
		return PDH_RETRY;
	}
	*/

	wchar_t* counters = arg->config->szReturnPathBuffer;
	while (*counters != '\0') {
		arg->counters->push_back(counters);
		counters += arg->counters->back().length();
		counters++; // Skip the null terminator to move to the first character of the next counter, or second null terminator
	}
	return ERROR_SUCCESS;
}

void CSettings::OnBnClickedSelectPerfCounters()
{
	std::vector<std::wstring> counters;
	PDH_BROWSE_DLG_CONFIG config = {};
	CallbackArg arg = {};
	const size_t arbitrary_magic_buffer_size = 10000;
	arg.counter_names.resize(arbitrary_magic_buffer_size);
	arg.config = &config;
	arg.counters = &counters;
	config.bSingleCounterPerDialog = false;
	config.bIncludeCostlyObjects = false;
	config.bIncludeInstanceIndex = true;

	// I get crashes deep inside pdhui.dll if I set this to false and select
	// something (All Instances) that results in a wildcard. WTF?
	config.bWildCardInstances = true;
	config.bDisableMachineSelection = true;
	config.szDialogBoxCaption = const_cast<wchar_t*>(L"Select performance counters");

	config.hWndOwner = *this;

	config.pCallBack = &CounterCallBack;
	config.dwCallBackArg = reinterpret_cast<DWORD_PTR>(&arg);
	config.dwDefaultDetailLevel = PERF_DETAIL_EXPERT;
	config.szReturnPathBuffer = arg.counter_names.data();
	config.cchReturnPathLength = (DWORD)arg.counter_names.size();;
	// Need some way to initialize the dialog with the previous settings?
	PDH_STATUS status = PdhBrowseCounters(&config);
	if (status == ERROR_SUCCESS || status == PDH_DIALOG_CANCELLED)
	{
		std::wstring counters_string;
		for (auto& counter : counters)
		{
			counters_string += counter;
			counters_string += ';'; // I hope that is a valid separator.
		}
		// Trim the trailing ';'
		if (!counters_string.empty())
			counters_string.resize(counters_string.size() - 1);
		SetDlgItemTextW(IDC_PERFORMANCECOUNTERS, counters_string.c_str());
	}
}


void CSettings::OnBnClickedUseOtherKernelLogger()
{
	bUseOtherKernelLogger_ = !bUseOtherKernelLogger_;
}
