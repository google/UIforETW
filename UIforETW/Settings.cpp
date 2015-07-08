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
#include "afxdialogex.h"

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
// searching for "f:trace_event_etw_export_win.cc" in https://code.google.com/p/chromium/codesearch#/.
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
};

// 1ULL << 61 and 1ULL << 62 are special values that indicate to Chrome to
// enable all enabled-by-default and disabled-by-default categories
// respectively.
const PCWSTR other_events_group_name = L"All enabled-by-default events";  // 0x2000000000000000
const PCWSTR disabled_other_events_group_name = L"All disabled-by-default events";  // 0x4000000000000000
uint64_t other_events_keyword_bit = 1ULL << 61;
uint64_t disabled_other_events_keyword_bit = 1ULL << 62;

// CSettings dialog

IMPLEMENT_DYNAMIC(CSettings, CDialogEx)

CSettings::CSettings(CWnd* pParent /*=NULL*/, const std::wstring& exeDir, const std::wstring& wptDir)
	: CDialogEx(CSettings::IDD, pParent)
	, exeDir_(exeDir)
	, wptDir_(wptDir)
{

}

CSettings::~CSettings()
{
}

void CSettings::DoDataExchange(CDataExchange* pDX)
{
	DDX_Control(pDX, IDC_HEAPEXE, btHeapTracingExe_);
	DDX_Control(pDX, IDC_EXTRAPROVIDERS, btExtraProviders_);
	DDX_Control(pDX, IDC_EXTRASTACKWALKS, btExtraStackwalks_);
	DDX_Control(pDX, IDC_BUFFERSIZES, btBufferSizes_);
	DDX_Control(pDX, IDC_COPYSTARTUPPROFILE, btCopyStartupProfile_);
	DDX_Control(pDX, IDC_COPYSYMBOLDLLS, btCopySymbolDLLs_);
	DDX_Control(pDX, IDC_CHROMEDEVELOPER, btChromeDeveloper_);
	DDX_Control(pDX, IDC_AUTOVIEWTRACES, btAutoViewTraces_);
	DDX_Control(pDX, IDC_HEAPSTACKS, btHeapStacks_);
	DDX_Control(pDX, IDC_CHROMEDLLPATH, btChromeDllPath_);
	DDX_Control(pDX, IDC_WSMONITOREDPROCESSES, btWSMonitoredProcesses_);
	DDX_Control(pDX, IDC_EXPENSIVEWS, btExpensiveWSMonitoring_);
	DDX_Control(pDX, IDC_VIRTUALALLOCSTACKS, btVirtualAllocStacks_);
	DDX_Control(pDX, IDC_CHROME_CATEGORIES, btChromeCategories_);

	CDialogEx::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(CSettings, CDialogEx)
	ON_BN_CLICKED(IDC_COPYSTARTUPPROFILE, &CSettings::OnBnClickedCopystartupprofile)
	ON_BN_CLICKED(IDC_COPYSYMBOLDLLS, &CSettings::OnBnClickedCopysymboldlls)
	ON_BN_CLICKED(IDC_CHROMEDEVELOPER, &CSettings::OnBnClickedChromedeveloper)
	ON_BN_CLICKED(IDC_AUTOVIEWTRACES, &CSettings::OnBnClickedAutoviewtraces)
	ON_BN_CLICKED(IDC_HEAPSTACKS, &CSettings::OnBnClickedHeapstacks)
	ON_BN_CLICKED(IDC_VIRTUALALLOCSTACKS, &CSettings::OnBnClickedVirtualallocstacks)
	ON_BN_CLICKED(IDC_EXPENSIVEWS, &CSettings::OnBnClickedExpensivews)
END_MESSAGE_MAP()

BOOL CSettings::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	SetDlgItemText(IDC_HEAPEXE, heapTracingExes_.c_str());
	CheckDlgButton(IDC_CHROMEDEVELOPER, bChromeDeveloper_);
	CheckDlgButton(IDC_AUTOVIEWTRACES, bAutoViewTraces_);
	CheckDlgButton(IDC_HEAPSTACKS, bHeapStacks_);
	CheckDlgButton(IDC_VIRTUALALLOCSTACKS, bVirtualAllocStacks_);
	SetDlgItemText(IDC_CHROMEDLLPATH, chromeDllPath_.c_str());

	btExtraProviders_.EnableWindow(FALSE);
	btExtraStackwalks_.EnableWindow(FALSE);
	btBufferSizes_.EnableWindow(FALSE);
	btChromeDllPath_.EnableWindow(bChromeDeveloper_);
	// A 32-bit process on 64-bit Windows will not be able to read the
	// full working set of 64-bit processes, so don't even try.
	if (Is64BitWindows() && !Is64BitBuild())
		btWSMonitoredProcesses_.EnableWindow(FALSE);
	else
		SetDlgItemText(IDC_WSMONITOREDPROCESSES, WSMonitoredProcesses_.c_str());
	CheckDlgButton(IDC_EXPENSIVEWS, bExpensiveWSMonitoring_);

	if (toolTip_.Create(this))
	{
		toolTip_.SetMaxTipWidth(400);
		toolTip_.Activate(TRUE);

		toolTip_.AddTool(&btHeapTracingExe_, L"Specify the file names of the exes to be heap traced, "
					L"separated by semi-colons. "
					L"Enter just the file parts (with the .exe extension) not a full path. For example, "
					L"'chrome.exe;notepad.exe'. This is for use with the heap-tracing-to-file mode.");
		toolTip_.AddTool(&btCopyStartupProfile_, L"Copies startup.wpaProfile files for WPA 8.1 and "
					L"10 to the appropriate destinations so that the next time WPA starts up it will have "
					L"reasonable analysis defaults.");
		toolTip_.AddTool(&btCopySymbolDLLs_, L"Copy dbghelp.dll and symsrv.dll to the xperf directory to "
					L"try to resolve slow or failed symbol loading in WPA. See "
					L"https://randomascii.wordpress.com/2012/10/04/xperf-symbol-loading-pitfalls/ "
					L"for details.");
		toolTip_.AddTool(&btChromeDeveloper_, L"Check this to enable Chrome specific behavior such as "
					L"setting the Chrome symbol server path, and preprocessing of Chrome symbols and "
					L"traces.");
		toolTip_.AddTool(&btAutoViewTraces_, L"Check this to have UIforETW launch the trace viewer "
					L"immediately after a trace is recorded.");
		toolTip_.AddTool(&btHeapStacks_, L"Check this to record call stacks on HeapAlloc, HeapRealloc, "
					L"and similar calls, when doing heap traces.");
		toolTip_.AddTool(&btChromeDllPath_, L"Specify the path where chrome.dll resides. This is used "
					L"to register the Chrome providers if the Chrome developer option is set.");
		toolTip_.AddTool(&btWSMonitoredProcesses_, L"Names of processes whose working sets will be "
					L"monitored, separated by semi-colons. An empty string means no monitoring. A '*' means "
					L"that all processes will be monitored. For instance 'chrome.exe;notepad.exe'");
		toolTip_.AddTool(&btExpensiveWSMonitoring_, L"Check this to have private working set and PSS "
					L"(proportional set size) calculated for monitored processes. This may consume "
					L"dozens or hundreds of ms each time. Without this checked only full working "
					L"set is calculated, which is cheap.");
		toolTip_.AddTool(&btVirtualAllocStacks_, L"Check this to record call stacks on VirtualAlloc on all "
					L"traces instead of just heap traces.");
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
	chromeDllPath_ = GetEditControlText(btChromeDllPath_);
	WSMonitoredProcesses_ = GetEditControlText(btWSMonitoredProcesses_);

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


void CSettings::OnBnClickedCopysymboldlls()
{
	// Attempt to deal with these problems:
	// https://randomascii.wordpress.com/2012/10/04/xperf-symbol-loading-pitfalls/
	const wchar_t* fileNames[] =
	{
		L"dbghelp.dll",
		L"symsrv.dll",
	};

	bool bIsWin64 = Is64BitWindows();

	bool failed = false;
	std::wstring third_party = exeDir_ + L"..\\third_party\\";
	for (size_t i = 0; i < ARRAYSIZE(fileNames); ++i)
	{
		std::wstring source = third_party + fileNames[i];
		if (bIsWin64)
			source = third_party + L"x64\\" + fileNames[i];

		std::wstring dest = wptDir_ + fileNames[i];
		if (!CopyFile(source.c_str(), dest.c_str(), FALSE))
			failed = true;
	}

	if (failed)
		AfxMessageBox(L"Failed to copy dbghelp.dll and symsrv.dll to the WPT directory. Is WPA running?");
	else
		AfxMessageBox(L"Copied dbghelp.dll and symsrv.dll to the WPT directory. If this doesn't help "
				L"with symbol loading then consider deleting them to restore the previous state.");
}


void CSettings::OnBnClickedChromedeveloper()
{
	bChromeDeveloper_ = !bChromeDeveloper_;
	btChromeDllPath_.EnableWindow(bChromeDeveloper_);
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
