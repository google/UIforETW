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

#include "About.h"
#include "afxdialogex.h"
#include "ChildProcess.h"
#include "Settings.h"
#include "Utility.h"
#include "WorkingSet.h"

#include <algorithm>
#include <direct.h>
#include <ETWProviders\etwprof.h>
#include <vector>
#include <map>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

const int kRecordTraceHotKey = 1234;

// This static pointer to the main window is used by the global
// outputPrintf function.
static CUIforETWDlg* pMainWindow;

// This convenient hack function is so that the ChildProcess code can
// print to the main output window. This function can only be called
// from the main thread.
void outputPrintf(_Printf_format_string_ const wchar_t* pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	pMainWindow->vprintf(pFormat, args);
	va_end(args);
}

void CUIforETWDlg::vprintf(const wchar_t* pFormat, va_list args)
{
	wchar_t buffer[5000];
	_vsnwprintf_s(buffer, _TRUNCATE, pFormat, args);

	auto converted = ConvertToCRLF(buffer);
	// Don't add a line separator at the very beginning.
	if (output_.empty() && converted.substr(0, 2) == L"\r\n")
		converted = converted.substr(2);
	output_ += converted;

	SetDlgItemText(IDC_OUTPUT, output_.c_str());

	// Make sure the end of the data is visible.
	btOutput_.SetSel(0, -1);
	btOutput_.SetSel(-1, -1);

	// Display the results immediately.
	UpdateWindow();

	// Fake out the Windows hang detection since otherwise on long-running
	// child-processes such as processing Chrome symbols we will get
	// frosted, a ghost window will be displayed, and none of our updates
	// will be visible.
	MSG msg;
	PeekMessage(&msg, *this, 0, 0, PM_NOREMOVE);
}


CUIforETWDlg::CUIforETWDlg(CWnd* pParent /*=NULL*/)
	: CDialogEx(CUIforETWDlg::IDD, pParent)
	, monitorThread_(this)
{
	pMainWindow = this;
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	TransferSettings(false);
}

CUIforETWDlg::~CUIforETWDlg()
{
	// Shut down key logging.
	SetKeyloggingState(kKeyLoggerOff);

	// Save settings.
	TransferSettings(true);
}

// Shutdown tasks that must be completed before the dialog
// closes should go here.
void CUIforETWDlg::ShutdownTasks()
{
	if (bShutdownCompleted_)
		return;
	bShutdownCompleted_ = true;
	// Save any in-progress trace-notes edits.
	SaveNotesIfNeeded();
	// Stop ETW tracing when we shut down.
	if (bIsTracing_)
	{
		StopTracingAndMaybeRecord(false);
	}

	// Forcibly clear the heap tracing registry keys.
	SetHeapTracing(true);

	// Make sure the sampling speed is set to normal on the way out.
	// Don't change bFastSampling because it needs to get saved.
	if (bFastSampling_)
	{
		bFastSampling_ = false;
		SetSamplingSpeed();
		bFastSampling_ = true;
	}
}

void CUIforETWDlg::OnCancel()
{
	ShutdownTasks();
	CDialog::OnCancel();
}

void CUIforETWDlg::OnClose()
{
	ShutdownTasks();
	CDialog::OnClose();
}

void CUIforETWDlg::OnOK()
{
	ShutdownTasks();
	CDialog::OnOK();
}

// Hook up dialog controls to classes that represent them,
// for easier manipulation of those controls.
void CUIforETWDlg::DoDataExchange(CDataExchange* pDX)
{
	DDX_Control(pDX, IDC_STARTTRACING, btStartTracing_);
	DDX_Control(pDX, IDC_SAVETRACEBUFFERS, btSaveTraceBuffers_);
	DDX_Control(pDX, IDC_STOPTRACING, btStopTracing_);

	DDX_Control(pDX, IDC_COMPRESSTRACE, btCompress_);
	DDX_Control(pDX, IDC_CPUSAMPLINGCALLSTACKS, btSampledStacks_);
	DDX_Control(pDX, IDC_CONTEXTSWITCHCALLSTACKS, btCswitchStacks_);
	DDX_Control(pDX, IDC_FASTSAMPLING, btFastSampling_);
	DDX_Control(pDX, IDC_GPUTRACING, btGPUTracing_);
	DDX_Control(pDX, IDC_SHOWCOMMANDS, btShowCommands_);

	DDX_Control(pDX, IDC_INPUTTRACING, btInputTracing_);
	DDX_Control(pDX, IDC_INPUTTRACING_LABEL, btInputTracingLabel_);
	DDX_Control(pDX, IDC_TRACINGMODE, btTracingMode_);
	DDX_Control(pDX, IDC_TRACELIST, btTraces_);
	DDX_Control(pDX, IDC_TRACENOTES, btTraceNotes_);
	DDX_Control(pDX, IDC_OUTPUT, btOutput_);
	DDX_Control(pDX, IDC_TRACENAMEEDIT, btTraceNameEdit_);

	CDialogEx::DoDataExchange(pDX);
}

// Hook up functions to messages from buttons, menus, etc.
BEGIN_MESSAGE_MAP(CUIforETWDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_STARTTRACING, &CUIforETWDlg::OnBnClickedStarttracing)
	ON_BN_CLICKED(IDC_STOPTRACING, &CUIforETWDlg::OnBnClickedStoptracing)
	ON_BN_CLICKED(IDC_COMPRESSTRACE, &CUIforETWDlg::OnBnClickedCompresstrace)
	ON_BN_CLICKED(IDC_CPUSAMPLINGCALLSTACKS, &CUIforETWDlg::OnBnClickedCpusamplingcallstacks)
	ON_BN_CLICKED(IDC_CONTEXTSWITCHCALLSTACKS, &CUIforETWDlg::OnBnClickedContextswitchcallstacks)
	ON_BN_CLICKED(IDC_SHOWCOMMANDS, &CUIforETWDlg::OnBnClickedShowcommands)
	ON_BN_CLICKED(IDC_FASTSAMPLING, &CUIforETWDlg::OnBnClickedFastsampling)
	ON_CBN_SELCHANGE(IDC_INPUTTRACING, &CUIforETWDlg::OnCbnSelchangeInputtracing)
	ON_MESSAGE(WM_UPDATETRACELIST, UpdateTraceListHandler)
	ON_LBN_DBLCLK(IDC_TRACELIST, &CUIforETWDlg::OnLbnDblclkTracelist)
	ON_WM_GETMINMAXINFO()
	ON_WM_SIZE()
	ON_LBN_SELCHANGE(IDC_TRACELIST, &CUIforETWDlg::OnLbnSelchangeTracelist)
	ON_BN_CLICKED(IDC_ABOUT, &CUIforETWDlg::OnBnClickedAbout)
	ON_BN_CLICKED(IDC_SAVETRACEBUFFERS, &CUIforETWDlg::OnBnClickedSavetracebuffers)
	ON_MESSAGE(WM_HOTKEY, OnHotKey)
	ON_WM_CLOSE()
	ON_CBN_SELCHANGE(IDC_TRACINGMODE, &CUIforETWDlg::OnCbnSelchangeTracingmode)
	ON_BN_CLICKED(IDC_SETTINGS, &CUIforETWDlg::OnBnClickedSettings)
	ON_WM_CONTEXTMENU()
	ON_BN_CLICKED(ID_TRACES_OPENTRACEINWPA, &CUIforETWDlg::OnOpenTraceWPA)
	ON_BN_CLICKED(ID_TRACES_OPENTRACEIN10WPA, &CUIforETWDlg::OnOpenTrace10WPA)
	ON_BN_CLICKED(ID_TRACES_OPENTRACEINGPUVIEW, &CUIforETWDlg::OnOpenTraceGPUView)
	ON_BN_CLICKED(ID_RENAME, &CUIforETWDlg::OnRenameKey)
	ON_BN_CLICKED(ID_RENAMEFULL, &CUIforETWDlg::OnFullRenameKey)
	ON_EN_KILLFOCUS(IDC_TRACENAMEEDIT, &CUIforETWDlg::FinishTraceRename)
	ON_BN_CLICKED(ID_ENDRENAME, &CUIforETWDlg::FinishTraceRename)
	ON_BN_CLICKED(ID_ESCKEY, &CUIforETWDlg::CancelTraceRename)
	ON_BN_CLICKED(IDC_GPUTRACING, &CUIforETWDlg::OnBnClickedGPUtracing)
	ON_BN_CLICKED(ID_COPYTRACENAME, &CUIforETWDlg::CopyTraceName)
	ON_BN_CLICKED(ID_DELETETRACE, &CUIforETWDlg::DeleteTrace)
	ON_BN_CLICKED(ID_SELECTALL, &CUIforETWDlg::NotesSelectAll)
	ON_BN_CLICKED(ID_PASTEOVERRIDE, &CUIforETWDlg::NotesPaste)
	ON_WM_ACTIVATE()
	ON_WM_TIMER()
END_MESSAGE_MAP()


void CUIforETWDlg::SetSymbolPath()
{
	// Make sure that the symbol paths are set.

#pragma warning(suppress : 4996)
	if (bManageSymbolPath_ || !getenv("_NT_SYMBOL_PATH"))
	{
		bManageSymbolPath_ = true;
		std::string symbolPath = "SRV*" + systemDrive_ + "symbols*https://msdl.microsoft.com/download/symbols";
		if (bChromeDeveloper_)
			symbolPath = "SRV*" + systemDrive_ + "symbols*https://msdl.microsoft.com/download/symbols;SRV*" + systemDrive_ + "symbols*https://chromium-browser-symsrv.commondatastorage.googleapis.com";
		(void)_putenv(("_NT_SYMBOL_PATH=" + symbolPath).c_str());
		outputPrintf(L"\nSetting _NT_SYMBOL_PATH to %s (Microsoft%s). "
			L"Set _NT_SYMBOL_PATH yourself or toggle 'Chrome developer' if you want different defaults.\n",
			AnsiToUnicode(symbolPath).c_str(), bChromeDeveloper_ ? L" plus Chrome" : L"");
	}
#pragma warning(suppress : 4996)
	const char* symCachePath = getenv("_NT_SYMCACHE_PATH");
	if (!symCachePath)
		(void)_putenv(("_NT_SYMCACHE_PATH=" + systemDrive_ + "symcache").c_str());
}


BOOL CUIforETWDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Load the F2 (rename) and ESC (silently swallow ESC) accelerators
	hAccelTable_ = LoadAccelerators(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_ACCELERATORS));
	// Load the Enter accelerator for exiting renaming.
	hRenameAccelTable_ = LoadAccelerators(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_RENAMEACCELERATORS));
	// Load the accelerators for when editing trace notes.
	hNotesAccelTable_ = LoadAccelerators(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_NOTESACCELERATORS));
	// Load the accelerators for when the trace list is active.
	hTracesAccelTable_ = LoadAccelerators(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_TRACESACCELERATORS));

	CRect windowRect;
	GetWindowRect(&windowRect);
	initialWidth_ = lastWidth_ = windowRect.Width();
	initialHeight_ = lastHeight_ = windowRect.Height();

	// Win+Ctrl+C is used to trigger recording of traces. This is compatible with
	// wprui. If this is changed then be sure to change the button text.
	if (!RegisterHotKey(*this, kRecordTraceHotKey, MOD_WIN + MOD_CONTROL, 'C'))
	{
		AfxMessageBox(L"Couldn't register hot key.");
		btSaveTraceBuffers_.SetWindowTextW(L"Sa&ve Trace Buffers");
	}

	// Add "About..." menu item to system menu.

	static_assert((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX, "IDM_ABOUTBOX must be in the system command range!");
	static_assert(IDM_ABOUTBOX < 0xF000, "IDM_ABOUTBOX must be in the system command range!");

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu)
	{
		CString strAboutMenu;
		const BOOL bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		UIETWASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	if (GetWindowsVersion() == kWindowsVersionXP)
	{
		AfxMessageBox(L"ETW tracing requires Windows Vista or above.");
		exit(10);
	}

	wchar_t* windowsDir = nullptr;
	if (!SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Windows, 0, NULL, &windowsDir)))
		std::terminate();
	windowsDir_ = windowsDir;
	windowsDir_ += '\\';
	CoTaskMemFree(windowsDir);
	// ANSI string, not unicode.
	systemDrive_ = static_cast<char>(windowsDir_[0]);
	systemDrive_ += ":\\";

	// The WPT 8.1 installer is always a 32-bit installer, so we look for it in
	// ProgramFilesX86, on 32-bit and 64-bit operating systems.
	wchar_t* progFilesx86Dir = nullptr;
	if (!SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, NULL, &progFilesx86Dir)))
		std::terminate();
	windowsKitsDir_ = progFilesx86Dir;
	CoTaskMemFree(progFilesx86Dir);
	progFilesx86Dir = nullptr;

	windowsKitsDir_ += L"\\Windows Kits\\";
	wptDir_ = windowsKitsDir_ + L"8.1\\Windows Performance Toolkit\\";
	wpt10Dir_ = windowsKitsDir_ + L"10\\Windows Performance Toolkit\\";

	// Install WPT 8.1 and WPT 10 if needed and if available.
	// The installers are available as part of etwpackage.zip on https://github.com/google/UIforETW/releases
	if (!PathFileExists(GetXperfPath().c_str()))
	{
		// Windows 7 users need to have WPT 8.1 installed.
		if (GetWindowsVersion() <= kWindowsVersion7)
		{
			const std::wstring installPath81 = GetExeDir() + L"..\\third_party\\wpt81\\WPTx64-x86_en-us.msi";
			if (PathFileExists(installPath81.c_str()))
			{
				HINSTANCE installResult81 = ShellExecute(m_hWnd, L"open", installPath81.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
				if (installResult81 > reinterpret_cast<HINSTANCE>(32))
				{
					outputPrintf(L"WPT version 8.1 was installed.\n");
				}
				else
				{
					outputPrintf(L"Failure code %d while installing WPT 8.1.\n", reinterpret_cast<int>(installResult81));
				}
			}
		}
	}
	// Everybody should have WPT 10 installed.
	if (!PathFileExists((wpt10Dir_ + L"xperf.exe").c_str()))
	{
		const std::wstring installPath10 = GetExeDir() + L"..\\third_party\\wpt10\\WPTx64-x86_en-us.msi";
		if (PathFileExists(installPath10.c_str()))
		{
			HINSTANCE installResult10 = ShellExecute(m_hWnd, L"open", installPath10.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
			if (installResult10 > reinterpret_cast<HINSTANCE>(32))
			{
				outputPrintf(L"WPT version 10 was installed.\n");
			}
			else
			{
				outputPrintf(L"Failure code %d while installing WPT 10.\n", reinterpret_cast<int>(installResult10));
			}
		}
	}
	if (!PathFileExists(GetXperfPath().c_str()))
	{
		if (GetWindowsVersion() <= kWindowsVersion7)
		{
			// WPT 10 (at least the 10240 version) doesn't record image ID information
			// on Windows 7 and below, so the Windows 8.1 version of WPT is needed.
			AfxMessageBox((GetXperfPath() + L" does not exist. Windows 7 and below require that "
				L"WPT 8.1 be installed. If you run UIforETW from etwpackage.zip from\n"
				L"https://github.com/google/UIforETW/releases\n"
				L"then WPT will be automatically installed.").c_str());
			exit(10);
		}
		std::wstring oldXperfPath = GetXperfPath();
		wptDir_ = wpt10Dir_;
		if (!PathFileExists(GetXperfPath().c_str()))
		{
			AfxMessageBox((oldXperfPath + L" and " + GetXperfPath() + L" do not exist. Please install WPT 8.1 or 10. Exiting."
				L" If you run UIforETW from etwpackage.zip from\n"
				L"https://github.com/google/UIforETW/releases\n"
				L"then WPT will be automatically installed.").c_str());
			exit(10);
		}
	}
	wpaPath_ = wptDir_ + L"wpa.exe";
	gpuViewPath_ = wptDir_ + L"gpuview\\gpuview.exe";
	wpa10Path_ = wpt10Dir_ + L"wpa.exe";
	wpaDefaultPath_ = wpaPath_;
	if (PathFileExists(wpa10Path_.c_str()))
		wpaDefaultPath_ = wpa10Path_;

	// The Media Experience Analyzer is a 64-bit installer, so we look for it in
	// ProgramFiles.
	wchar_t* progFilesDir = nullptr;
	if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &progFilesDir)))
	{
		mxaPath_ = progFilesDir;
		mxaPath_ += L"\\Media eXperience Analyzer\\XA.exe";
	}

	wchar_t documents[MAX_PATH];
	const BOOL getMyDocsResult = SHGetSpecialFolderPath(*this, documents, CSIDL_MYDOCUMENTS, TRUE);
	UIETWASSERT(getMyDocsResult);
	if (!getMyDocsResult)
	{
		OutputDebugStringA("Failed to find My Documents directory.\r\n");
		exit(10);
	}
	std::wstring defaultTraceDir = documents + std::wstring(L"\\etwtraces\\");
	traceDir_ = GetDirectory(L"etwtracedir", defaultTraceDir);

	// Copy over the startup profiles if they currently don't exist.
	CopyStartupProfiles(GetExeDir(), false);

	tempTraceDir_ = GetDirectory(L"temp", traceDir_);

	SetSymbolPath();

	btTraceNameEdit_.GetWindowRect(&traceNameEditRect_);
	ScreenToClient(&traceNameEditRect_);

	// Set the icon for this dialog. The framework does this automatically
	// when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	WindowsVersion winver = GetWindowsVersion();
	if (winver <= kWindowsVersion7)
	{
		bCompress_ = false; // ETW trace compression requires Windows 8.0
		SmartEnableWindow(btCompress_.m_hWnd, false);
	}

	CheckDlgButton(IDC_COMPRESSTRACE, bCompress_);
	CheckDlgButton(IDC_CONTEXTSWITCHCALLSTACKS, bCswitchStacks_);
	CheckDlgButton(IDC_CPUSAMPLINGCALLSTACKS, bSampledStacks_);
	CheckDlgButton(IDC_FASTSAMPLING, bFastSampling_);
	CheckDlgButton(IDC_GPUTRACING, bGPUTracing_);
	CheckDlgButton(IDC_SHOWCOMMANDS, bShowCommands_);

	// If a fast sampling speed is requested then set it now. Note that
	// this assumes that the speed will otherwise be normal.
	if (bFastSampling_)
		SetSamplingSpeed();

	btInputTracing_.AddString(L"Off");
	btInputTracing_.AddString(L"Private");
	btInputTracing_.AddString(L"Full");
	btInputTracing_.SetCurSel(InputTracing_);

	btTracingMode_.AddString(L"Circular buffer tracing");
	btTracingMode_.AddString(L"Tracing to file");
	btTracingMode_.AddString(L"Heap tracing to file");
	btTracingMode_.SetCurSel(tracingMode_);

	UpdateEnabling();
	SmartEnableWindow(btTraceNotes_, false); // This window always starts out disabled.

	// Don't change traceDir_ because the monitor thread has a pointer to it.
	monitorThread_.StartThread(&traceDir_);

	// Configure the working set monitor.
	workingSetThread_.SetProcessFilter(WSMonitoredProcesses_, bExpensiveWSMonitoring_);

	DisablePagingExecutive();

	UpdateTraceList();

	if (toolTip_.Create(this))
	{
		toolTip_.SetMaxTipWidth(400);
		toolTip_.Activate(TRUE);

		toolTip_.AddTool(&btStartTracing_, L"Start ETW tracing.");

		toolTip_.AddTool(&btCompress_, L"Only uncheck this if you record traces on Windows 8 and above and want to analyze "
					L"them on Windows 7 and below.\n"
					L"Enable ETW trace compression. On Windows 8 and above this compresses traces "
					L"as they are saved, making them 5-10x smaller. However compressed traces cannot be loaded on "
					L"Windows 7 or earlier. On Windows 7 this setting has no effect.");
		toolTip_.AddTool(&btCswitchStacks_, L"This enables recording of call stacks on context switches, from both "
					L"the thread being switched in and the readying thread. This should only be disabled if the performance "
					L"of functions like WaitForSingleObject and SetEvent appears to be distorted, which can happen when the "
					L"context-switch rate is very high.");
		toolTip_.AddTool(&btSampledStacks_, L"This enables recording of call stacks on CPU sampling events, which "
					L"by default happen at 1 KHz. This should rarely be disabled.");
		toolTip_.AddTool(&btFastSampling_, L"Checking this changes the CPU sampling frequency from the default of "
					L"~1 KHz to the maximum speed of ~8 KHz. This increases the data rate and thus the size of traces "
					L"but can make investigating brief CPU-bound performance problems (such as a single long frame) "
					L"more practical.");
		toolTip_.AddTool(&btGPUTracing_, L"Check this to allow seeing GPU usage "
					L"in WPA, and more data in GPUView.");
		toolTip_.AddTool(&btShowCommands_, L"This tells UIforETW to display the commands being "
					L"executed. This can be helpful for diagnostic purposes but is not normally needed.");

		const TCHAR* pInputTip = L"Input tracing inserts custom ETW events into traces which can be helpful when "
					L"investigating performance problems that are correlated with user input. The default setting of "
					L"'private' records alphabetic keys as 'A' and numeric keys as '0'. The 'full' setting records "
					L"alphanumeric details. Both 'private' and 'full' record mouse movement and button clicks. The "
					L"'off' setting records no input.";
		toolTip_.AddTool(&btInputTracingLabel_, pInputTip);
		toolTip_.AddTool(&btInputTracing_, pInputTip);

		toolTip_.AddTool(&btTracingMode_, L"Select whether to trace straight to disk or to in-memory circular buffers.");

		toolTip_.AddTool(&btTraces_, L"This is a list of all traces found in %etwtracedir%, which defaults to "
					L"documents\\etwtraces.");
		toolTip_.AddTool(&btTraceNotes_, L"Trace notes are intended for recording information about ETW traces, such "
					L"as an analysis of what was discovered in the trace. Trace notes are auto-saved to a parallel text "
					L"file - just type your analysis. The notes files will be renamed when you rename traces "
					L"through the trace-list context menu.");
	}

	SetHeapTracing(false);
	// Start the input logging thread with the current settings.
	SetKeyloggingState(InputTracing_);

	SetTimer(0, 1000, nullptr);

	return TRUE; // return TRUE unless you set the focus to a control
}

std::wstring CUIforETWDlg::GetDirectory(const wchar_t* env, const std::wstring& default)
{
	// Get a directory (from an environment variable, if set) and make sure it exists.
	std::wstring result = default;
#pragma warning(suppress : 4996)
	const wchar_t* traceDir = _wgetenv(env);
	if (traceDir)
	{
		result = traceDir;
	}
	// Make sure the name ends with a backslash.
	if (!result.empty() && result[result.size() - 1] != '\\')
		result += '\\';
	if (!PathFileExists(result.c_str()))
	{
		(void)_wmkdir(result.c_str());
	}
	if (!PathIsDirectory(result.c_str()))
	{
		AfxMessageBox((result + L" is not a directory. Exiting.").c_str());
		exit(10);
	}
	return result;
}

void CUIforETWDlg::RegisterProviders()
{
	// Assume failure. This assures that when we say
	// "Chrome providers will not be recorded." it will actually be true.
	useChromeProviders_ = false;

	std::wstring dllSource = GetExeDir();
	// Be sure to register the version of the DLLs that we are actually using.
	// This is important when adding new provider tasks, but should not otherwise
	// matter.
	if (Is64BitBuild())
		dllSource += L"ETWProviders64.dll";
	else
		dllSource += L"ETWProviders.dll";
#pragma warning(suppress:4996)
	const wchar_t* temp = _wgetenv(L"temp");
	if (!temp)
		return;
	std::wstring dllDest = temp;
	dllDest += L"\\ETWProviders.dll";
	if (!CopyFile(dllSource.c_str(), dllDest.c_str(), FALSE))
	{
		outputPrintf(L"Registering of ETW providers failed due to copy error.\n");
		return;
	}
	wchar_t systemDir[MAX_PATH];
	systemDir[0] = 0;
	GetSystemDirectory(systemDir, ARRAYSIZE(systemDir));
	std::wstring wevtPath = systemDir + std::wstring(L"\\wevtutil.exe");

	// Register ETWProviders.dll
	for (int pass = 0; pass < 2; ++pass)
	{
		ChildProcess child(wevtPath);
		std::wstring args = pass ? L" im" : L" um";
		args += L" \"" + GetExeDir() + L"etwproviders.man\"";
		child.Run(bShowCommands_, L"wevtutil.exe" + args);
	}

	// Register chrome.dll if some chrome keywords are selected to be recorded.
	if (chromeKeywords_ != 0)
	{
		std::wstring manifestPath = GetExeDir() + L"chrome_events_win.man";
		std::wstring dllSuffix = L"chrome.dll";
		// DummyChrome.dll has the Chrome manifest compiled into it which is all that
		// is actually needed.
		std::wstring chromeDllFullPath = GetExeDir() + L"DummyChrome.dll";
		if (!PathFileExists(chromeDllFullPath.c_str()))
		{
			outputPrintf(L"Couldn't find %s.\n", chromeDllFullPath.c_str());
			outputPrintf(L"Chrome providers will not be recorded.\n");
			return;
		}
		for (int pass = 0; pass < 2; ++pass)
		{
			ChildProcess child(wevtPath);
			std::wstring args = pass ? L" im" : L" um";
			args += L" \"" + manifestPath + L"\"";
			if (pass)
			{
				args += L" \"/mf:" + chromeDllFullPath + L"\" \"/rf:" + chromeDllFullPath + L"\"";
			}
			child.Run(bShowCommands_, L"wevtutil.exe" + args);
			if (pass)
			{
				DWORD exitCode = child.GetExitCode();
				if (!exitCode)
				{
					useChromeProviders_ = true;
					outputPrintf(L"Chrome providers registered. Chrome providers will be recorded.\n");
				}
			}
		}
	}
}


// Tell Windows to keep 64-bit kernel metadata in memory so that
// stack walking will work. Just do it -- don't ask.
void CUIforETWDlg::DisablePagingExecutive()
{
	if (Is64BitWindows())
	{
		const wchar_t* keyName = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
		SetRegistryDWORD(HKEY_LOCAL_MACHINE, keyName, L"DisablePagingExecutive", 1);
	}
}

void CUIforETWDlg::UpdateEnabling()
{
	SmartEnableWindow(btStartTracing_.m_hWnd, !bIsTracing_);
	SmartEnableWindow(btSaveTraceBuffers_.m_hWnd, bIsTracing_);
	SmartEnableWindow(btStopTracing_.m_hWnd, bIsTracing_);
	SmartEnableWindow(btTracingMode_.m_hWnd, !bIsTracing_);

	SmartEnableWindow(btSampledStacks_.m_hWnd, !bIsTracing_);
	SmartEnableWindow(btCswitchStacks_.m_hWnd, !bIsTracing_);
	SmartEnableWindow(btGPUTracing_.m_hWnd, !bIsTracing_);
}

void CUIforETWDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CATLAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
// to draw the icon. For MFC applications using the document/view model,
// this is automatically done for you by the framework.

void CUIforETWDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
// the minimized window.
HCURSOR CUIforETWDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


std::wstring CUIforETWDlg::GetExeDir() const
{
	wchar_t exePath[MAX_PATH];
	if (GetModuleFileName(0, exePath, ARRAYSIZE(exePath)))
	{
		wchar_t* lastSlash = wcsrchr(exePath, '\\');
		if (lastSlash)
		{
			lastSlash[1] = 0;
			return exePath;
		}
	}

	exit(10);
}

std::wstring CUIforETWDlg::GenerateResultFilename() const
{
	std::wstring traceDir = GetTraceDir();

	char time[10];
	_strtime_s(time);
	char date[10];
	_strdate_s(date);
	int hour, min, sec;
	int year, month, day;
#pragma warning(suppress : 4996)
	const wchar_t* username = _wgetenv(L"USERNAME");
	if (!username)
		username = L"";
	wchar_t fileName[MAX_PATH];
	// Hilarious /analyze warning on this line from bug in _strtime_s annotation!
	// warning C6054: String 'time' might not be zero-terminated.
#pragma warning(suppress : 6054)
	if (3 == sscanf_s(time, "%d:%d:%d", &hour, &min, &sec) &&
		3 == sscanf_s(date, "%d/%d/%d", &month, &day, &year))
	{
		// The filenames are chosen to sort by date, with username as the LSB.
		swprintf_s(fileName, L"%04d-%02d-%02d_%02d-%02d-%02d_%s", year + 2000, month, day, hour, min, sec, username);
	}
	else
	{
		wcscpy_s(fileName, L"UIforETW");
	}

	std::wstring filePart = fileName;

	if (tracingMode_ == kHeapTracingToFile)
	{
		for (const auto& tracingName : split(heapTracingExes_, ';'))
			filePart += L"_" + CrackFilePart(tracingName);
		filePart += L"_heap";
	}

	return GetTraceDir() + filePart + L".etl";
}

void CUIforETWDlg::OnBnClickedStarttracing()
{
	RegisterProviders();
	if (tracingMode_ == kTracingToMemory)
		outputPrintf(L"\nStarting tracing to in-memory circular buffers...\n");
	else if (tracingMode_ == kTracingToFile)
		outputPrintf(L"\nStarting tracing to disk...\n");
	else if (tracingMode_ == kHeapTracingToFile)
		outputPrintf(L"\nStarting heap tracing to disk of %s...\n", heapTracingExes_.c_str());
	else
		UIETWASSERT(0);

	std::wstring kernelProviders = L" Latency+POWER+DISPATCHER+DISK_IO_INIT+FILE_IO+FILE_IO_INIT+VIRT_ALLOC+MEMINFO";
	if (!extraKernelFlags_.empty())
		kernelProviders += L"+" + extraKernelFlags_;
	std::wstring kernelStackWalk;
	// Record CPU sampling call stacks, from the PROFILE provider
	if (bSampledStacks_)
		kernelStackWalk += L"+PROFILE";
	// Record context-switch (switch in) and readying-thread (SetEvent, etc.)
	// call stacks from DISPATCHER provider.
	if (bCswitchStacks_)
		kernelStackWalk += L"+CSWITCH+READYTHREAD";
	// Record VirtualAlloc call stacks from the VIRT_ALLOC provider. Could
	// also record VirtualFree.
	if (bVirtualAllocStacks_ || tracingMode_ == kHeapTracingToFile)
		kernelStackWalk += L"+VirtualAlloc";
	// Add in any manually requested call stack flags.
	if (!extraKernelStacks_.empty())
		kernelStackWalk += L"+" + extraKernelStacks_;
	// Set up a -stackwalk configuration, removing the leading '+' sign.
	if (!kernelStackWalk.empty())
		kernelStackWalk = L" -stackwalk " + kernelStackWalk.substr(1);
	// Buffer sizes are in KB, so 1024 is actually 1 MB
	// Make this configurable.
	const int numKernelBuffers = BufferCountBoost(600);
	std::wstring kernelBuffers = stringPrintf(L" -buffersize 1024 -minbuffers %d -maxbuffers %d", numKernelBuffers, numKernelBuffers);
	std::wstring kernelFile = L" -f \"" + GetKernelFile() + L"\"";
	if (tracingMode_ == kTracingToMemory)
		kernelFile = L" -buffering";
	std::wstring kernelArgs = L" -start " + GetKernelLogger() + L" -on" + kernelProviders + kernelStackWalk + kernelBuffers + kernelFile;

	WindowsVersion winver = GetWindowsVersion();
	// The ReleaseUserCrit, ExclusiveUserCrit, and SharedUserCrit events generate
	// 75% of the events for this provider - 33,000/s in one test. They account for
	// more than 75% of the space used, according to System Configuration-> Trace
	// Statistics. That table also shows their Keyword (aka flags) which are
	// 0x0200000010000000. By specifying a flag of ~0x0200000010000000 we can
	// reduce the fill-rate of the user buffers by a factor of four, allowing much
	// longer time periods to be captured with lower overhead.
	// This avoids the problem where the user buffers wrap around so quickly that
	// their timer period doesn't overlap that of the kernel buffers. Specifying
	// this flag is equivalent to quadrupling the size of the user buffers!
	// This should also make the UI Delays and window-in-focus graphs more
	// reliable, by not having them lose messages so frequently, although it is not
	// clear that it actually helps.
	const uint64_t kCritFlags = 0x0200000010000000;
	std::wstring userProviders = stringPrintf(L"Microsoft-Windows-Win32k:0x%llx", ~kCritFlags);
	if (winver <= kWindowsVersionVista)
		userProviders = L"Microsoft-Windows-LUA"; // Because Microsoft-Windows-Win32k doesn't work on Vista.
	userProviders += L"+Multi-MAIN+Multi-FrameRate+Multi-Input+Multi-Worker";

	// DWM providers can be helpful also. Uncomment to enable.
	//userProviders += L"+Microsoft-Windows-Dwm-Dwm";
	// Theoretically better power monitoring data, Windows 7+, but it doesn't
	// seem to work.
	//userProviders += L"+Microsoft-Windows-Kernel-Processor-Power+Microsoft-Windows-Kernel-Power";

	// If the Chrome providers were successfully registered and if the user has requested tracing
	// some of Chrome's categories (keywords/flags) then add chrome:flags to the list of user
	// providers to monitor. See https://codereview.chromium.org/1176243016 for details.
	if (useChromeProviders_)
		userProviders += stringPrintf(L"+chrome:0x%llx", 0x8000000000000000 | chromeKeywords_);

	if (bGPUTracing_)
	{
		// Apparently we need a different provider for graphics profiling
		// on Windows 8 and above.
		if (winver >= kWindowsVersion8)
		{
			// This provider is needed for GPU profiling on Windows 8+
			userProviders += L"+Microsoft-Windows-DxgKrnl:0xFFFF:5";
			if (!IsWindowsServer())
			{
				// Present events on Windows 8 + -- non-server SKUs only.
				userProviders += L"+Microsoft-Windows-MediaEngine";
			}
		}
		else
		{
			// Necessary providers for a minimal GPU profiling setup.
			// DirectX logger:
			userProviders += L"+DX:0x2F";
		}
	}

	// Increase the user buffer sizes when doing graphics tracing or Chrome tracing.
	const int numUserBuffers = BufferCountBoost(bGPUTracing_ ? 200 : 100) + BufferCountBoost(useChromeProviders_ ? 300 : 0);
	std::wstring userBuffers = stringPrintf(L" -buffersize 1024 -minbuffers %d -maxbuffers %d", numUserBuffers, numUserBuffers);
	std::wstring userFile = L" -f \"" + GetUserFile() + L"\"";
	if (tracingMode_ == kTracingToMemory)
		userFile = L" -buffering";
	std::wstring userArgs = L" -start UIforETWSession -on " + userProviders + userBuffers + userFile;

	// Heap tracing settings -- only used for heap tracing.
	// Could also record stacks on HeapFree
	// Buffer sizes need to be huge for some programs - should be configurable.
	const int numHeapBuffers = BufferCountBoost(1000);
	std::wstring heapBuffers = stringPrintf(L" -buffersize 1024 -minbuffers %d -maxBuffers %d", numHeapBuffers, numHeapBuffers);
	std::wstring heapFile = L" -f \"" + GetHeapFile() + L"\"";
	std::wstring heapStackWalk;
	if (bHeapStacks_)
		heapStackWalk = L" -stackwalk HeapCreate+HeapDestroy+HeapAlloc+HeapRealloc";
	std::wstring heapArgs = L" -start xperfHeapSession -heap -Pids 0" + heapStackWalk + heapBuffers + heapFile;

	DWORD exitCode = 0;
	{
		ChildProcess child(GetXperfPath());

		if (tracingMode_ == kHeapTracingToFile)
			startupCommand_ = L"xperf.exe" + kernelArgs + userArgs + heapArgs;
		else
			startupCommand_ = L"xperf.exe" + kernelArgs + userArgs;
		child.Run(bShowCommands_, startupCommand_);

		exitCode = child.GetExitCode();
		if (exitCode)
		{
			outputPrintf(L"Error starting tracing. Try stopping tracing and then starting it again?\n");
			//  NT Kernel Logger: Cannot create a file when that file already exists. (0xb7).
			if (exitCode == 0x800700b7)
			{
				outputPrintf(L"The kernel logger is already running. Probably some other program such as procmon is using it.\n");
			}
			// NT Kernel Logger: Invalid flags. (0x3ec).
			if (exitCode == 0x800703ec)
			{
				if (!extraKernelFlags_.empty())
				{
					outputPrintf(L"Check your extra kernel flags in the settings dialog for typos.\n");
				}
			}
			if (exitCode == 0x8000ffff)
			{
				if (!extraKernelStacks_.empty())
				{
					outputPrintf(L"Check your extra kernel stack walks in the settings dialog for typos.\n");
				}
			}
		}
		else
		{
			outputPrintf(L"Tracing is started.\n");
		}
	}

	// Don't run the -capturestate step unless the previous step succeeded.
	// Otherwise the error messages build up and get ever more confusing.
	if (exitCode == 0)
	{
		// Run -capturestate on the user-mode loggers, for reliable captures.
		// If this step is skipped then GPU usage data will not be recorded on
		// Windows 8. Contrary to the xperf documentation this is needed for file
		// based recording as well as when -buffering is used.
		ChildProcess child(GetXperfPath());
		std::wstring captureArgs = L" -capturestate UIforETWSession " + userProviders;
		child.Run(bShowCommands_, L"xperf.exe" + captureArgs);
	}

	// Set this whether starting succeeds or not, to allow forced-stopping.
	bIsTracing_ = true;
	UpdateEnabling();

	traceStartTime_ = GetTickCount64();
}

void CUIforETWDlg::StopTracingAndMaybeRecord(bool bSaveTrace)
{
	std::wstring traceFilename = GenerateResultFilename();
	if (bSaveTrace)
		outputPrintf(L"\nSaving trace to disk...\n");
	else
		outputPrintf(L"\nStopping tracing...\n");

	// Rename Amcache.hve to work around a merge hang that can last up to six
	// minutes.
	// https://randomascii.wordpress.com/2015/03/02/profiling-the-profiler-working-around-a-six-minute-xperf-hang/
	const std::wstring compatFile = windowsDir_ + L"AppCompat\\Programs\\Amcache.hve";
	const std::wstring compatFileTemp = windowsDir_ + L"AppCompat\\Programs\\Amcache_temp.hve";
	// Delete any previously existing Amcache_temp.hve file that might have
	// been left behind by a previous failed tracing attempt.
	// Note that this has to be done before the -flush step -- it makes both it
	// and the -merge step painfully slow.
	DeleteFile(compatFileTemp.c_str());
	BOOL moveSuccess = MoveFile(compatFile.c_str(), compatFileTemp.c_str());
	if (bShowCommands_ && !moveSuccess)
		outputPrintf(L"Failed to rename %s to %s\n", compatFile.c_str(), compatFileTemp.c_str());

	ElapsedTimer saveTimer;
	{
		// Stop the kernel and user sessions.
		ChildProcess child(GetXperfPath());
		if (bSaveTrace)
		{
			if (useChromeProviders_)
				ETWMarkPrintf("Chrome ETW events were requested with keyword 0x%llx", chromeKeywords_);
			// Record the entire xperf startup command to the trace.
			ETWMarkWPrintf(L"Tracing startup command was: %s", startupCommand_.c_str());
		}
		if (bSaveTrace && tracingMode_ == kTracingToMemory)
		{
			ETWMark("Flushing trace buffers to disk.");
			ETWMark("Tracing type was circular buffer tracing.");
			// If we are in memory tracing mode then don't actually stop tracing,
			// just flush the buffers to disk.
			std::wstring args = L" -flush " + GetKernelLogger() + L" -f \"" + GetKernelFile() + L"\" -flush UIforETWSession -f \"" + GetUserFile() + L"\"";
			child.Run(bShowCommands_, L"xperf.exe" + args);
		}
		else
		{
			if (bSaveTrace)
				ETWMark("Stopping tracing.");
			if (tracingMode_ == kHeapTracingToFile)
			{
				ETWMark("Tracing type was heap tracing to file.");
				child.Run(bShowCommands_, L"xperf.exe -stop xperfHeapSession -stop UIforETWSession -stop " + GetKernelLogger());
			}
			else
			{
				ETWMark("Tracing type was tracing to file.");
				child.Run(bShowCommands_, L"xperf.exe -stop UIforETWSession -stop " + GetKernelLogger());
			}
		}
	}
	double saveTime = saveTimer.ElapsedSeconds();
	if (bShowCommands_ && bSaveTrace)
		outputPrintf(L"Trace save took %1.1f s\n", saveTime);

	double mergeTime = 0.0;
	if (bSaveTrace)
	{
		outputPrintf(L"Merging trace...\n");
		ElapsedTimer mergeTimer;
		{
			// Separate merge step to allow compression on Windows 8+
			// https://randomascii.wordpress.com/2015/03/02/etw-trace-compression-and-xperf-syntax-refresher/
			ChildProcess merge(GetXperfPath());
			std::wstring args = L" -merge \"" + GetKernelFile() + L"\" \"" + GetUserFile() + L"\"";
			if (tracingMode_ == kHeapTracingToFile)
				args += L" \"" + GetHeapFile() + L"\"";
			args += L" \"" + traceFilename + L"\"";
			if (bCompress_)
				args += L" -compress";
			merge.Run(bShowCommands_, L"xperf.exe" + args);
		}
		mergeTime = mergeTimer.ElapsedSeconds();
		if (bShowCommands_)
			outputPrintf(L"Trace merge took %1.1f s\n", mergeTime);
	}

	if (moveSuccess)
		MoveFile(compatFileTemp.c_str(), compatFile.c_str());

	// Delete the temporary files.
	DeleteFile(GetKernelFile().c_str());
	DeleteFile(GetUserFile().c_str());
	if (tracingMode_ == kHeapTracingToFile)
		DeleteFile(GetHeapFile().c_str());

	if (!bSaveTrace || tracingMode_ != kTracingToMemory)
	{
		bIsTracing_ = false;
		UpdateEnabling();
	}

	if (bSaveTrace)
	{
		if (bChromeDeveloper_)
			StripChromeSymbols(traceFilename);
		PreprocessTrace(traceFilename);

		if (bAutoViewTraces_)
			LaunchTraceViewer(traceFilename, wpaDefaultPath_);
		// Record the name so that it gets selected.
		lastTraceFilename_ = CrackFilePart(traceFilename);

		if (saveTime > 100.0 && tracingMode_ == kTracingToMemory)
		{
			// Some machines (one so far?) can take 5-10 minutes to do the trace
			// saving stage.
			outputPrintf(L"Saving the trace took %1.1f s, which is unusually long. Please "
				L"try metatrace.bat, and share your results on "
				L"https://groups.google.com/forum/#!forum/uiforetw.\n", saveTime);
		}
		if (mergeTime > 100.0)
		{
			// See the Amcache.hve comments above for details or to instrument.
			if (moveSuccess)
			{
				outputPrintf(L"Merging the trace took %1.1fs, which is unusually long. This is surprising "
					L"because renaming of amcache.hve to avoid this worked. Please try metatrace.bat "
					L"and share this on "
					L"https://groups.google.com/forum/#!forum/uiforetw\n", mergeTime);
			}
			else
			{
				outputPrintf(L"Merging the trace took %1.1fs, which is unusually long. This is probably "
					L"because renaming of amcache.hve failed. Please try metatrace.bat "
					L"and share this on "
					L"https://groups.google.com/forum/#!forum/uiforetw\n", mergeTime);
			}
		}

		outputPrintf(L"Finished recording trace.\n");
	}
	else
		outputPrintf(L"Tracing stopped.\n");
}

void CUIforETWDlg::OnTimer(UINT_PTR)
{
	if (bIsTracing_ && (tracingMode_ == kTracingToFile || tracingMode_ == kHeapTracingToFile))
	{
		ULONGLONG elapsed = GetTickCount64() - traceStartTime_;
		if (elapsed > kMaxFileTraceMs)
		{
			outputPrintf(L"\nTracing to disk ran excessively long. Auto-saving and stopping.\n");
			ETWMark("Auto-saving trace.");
			StopTracingAndMaybeRecord(true);
		}
	}
}

void CUIforETWDlg::OnBnClickedSavetracebuffers()
{
	StopTracingAndMaybeRecord(true);
}

void CUIforETWDlg::OnBnClickedStoptracing()
{
	StopTracingAndMaybeRecord(false);
}

void CUIforETWDlg::LaunchTraceViewer(const std::wstring traceFilename, const std::wstring viewerPath)
{
	if (!PathFileExists(traceFilename.c_str()))
	{
		const std::wstring zipPath = StripExtensionFromPath(traceFilename) + L".zip";
		if (PathFileExists(zipPath.c_str()))
		{
			AfxMessageBox(L"Viewing of zipped ETL files is not yet supported.\n"
				L"Please manually unzip the trace file.");
		}
		else
		{
			AfxMessageBox(L"That trace file does not exist.");
		}
		return;
	}

	const std::wstring viewerName = GetFilePart(viewerPath);

	std::wstring args = std::wstring(viewerName + L" \"") + traceFilename.c_str() + L"\"";

	// Wacky CreateProcess rules say args has to be writable!
	std::vector<wchar_t> argsCopy(args.size() + 1);
	wcscpy_s(&argsCopy[0], argsCopy.size(), args.c_str());
	STARTUPINFO startupInfo = {};
	PROCESS_INFORMATION processInfo = {};
	BOOL result = CreateProcess(viewerPath.c_str(), &argsCopy[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo);
	if (result)
	{
		// Close the handles to avoid leaks.
		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);
	}
	else
	{
		AfxMessageBox(L"Failed to start trace viewer.");
	}
}

void CUIforETWDlg::OnBnClickedCompresstrace()
{
	bCompress_ = !bCompress_;
}


void CUIforETWDlg::OnBnClickedCpusamplingcallstacks()
{
	bSampledStacks_ = !bSampledStacks_;
}


void CUIforETWDlg::OnBnClickedContextswitchcallstacks()
{
	bCswitchStacks_ = !bCswitchStacks_;
}


void CUIforETWDlg::OnBnClickedShowcommands()
{
	bShowCommands_ = !bShowCommands_;
}


void CUIforETWDlg::SetSamplingSpeed()
{
	std::wstring xperfPath = GetXperfPath();
	if (GetWindowsVersion() >= kWindowsVersion10)
	{
		xperfPath = wpt10Dir_ + L"xperf.exe";
		if (!PathFileExists(xperfPath.c_str()))
		{
			AfxMessageBox(L"Setting the sampling speed on Windows 10+ requires WPT 10, which is not installed.");
			return;
		}
	}
	ChildProcess child(xperfPath);
	std::wstring profInt = bFastSampling_ ? L"1221" : L"9001";
	std::wstring args = L" -setprofint " + profInt + L" cached";
	child.Run(bShowCommands_, L"xperf.exe" + args);
}

void CUIforETWDlg::OnBnClickedFastsampling()
{
	bFastSampling_ = !bFastSampling_;
	const wchar_t* message = nullptr;
	if (bFastSampling_)
	{
		message = L"Setting CPU sampling speed to 8 KHz, for finer resolution.";
	}
	else
	{
		message = L"Setting CPU sampling speed to 1 KHz, for lower overhead.";
	}
	outputPrintf(L"\n%s\n", message);
	SetSamplingSpeed();
}


void CUIforETWDlg::OnBnClickedGPUtracing()
{
	bGPUTracing_ = !bGPUTracing_;
}


void CUIforETWDlg::OnCbnSelchangeInputtracing()
{
	InputTracing_ = (KeyLoggerState)btInputTracing_.GetCurSel();
	switch (InputTracing_)
	{
	case kKeyLoggerOff:
		outputPrintf(L"Key logging disabled.\n");
		break;
	case kKeyLoggerAnonymized:
		outputPrintf(L"Key logging enabled. Number and letter keys will be recorded generically.\n");
		break;
	case kKeyLoggerFull:
		outputPrintf(L"Key logging enabled. Full keyboard information recorded - beware of private information being recorded.\n");
		break;
	default:
		UIETWASSERT(0);
		InputTracing_ = kKeyLoggerOff;
		break;
	}
	SetKeyloggingState(InputTracing_);
}

void CUIforETWDlg::UpdateTraceList()
{
	std::wstring selectedTraceName = lastTraceFilename_;
	lastTraceFilename_.clear();

	int curSel = btTraces_.GetCurSel();
	if (selectedTraceName.empty() && curSel >= 0 && curSel < (int)traces_.size())
	{
		selectedTraceName = traces_[curSel];
	}

	// Note that these will also pull in files like *.etlabc and *.zipabc.
	// I don't want that. Filter them out later?
	auto tempTraces = GetFileList(GetTraceDir() + L"\\*.etl");
	auto tempZips = GetFileList(GetTraceDir() + L"\\*.zip");
	// Why can't I use += to concatenate these?
	tempTraces.insert(tempTraces.end(), tempZips.begin(), tempZips.end());
	std::sort(tempTraces.begin(), tempTraces.end());
	// Function to stop the temporary traces from showing up.
	auto ifInvalid = [](const std::wstring& name) { return name == L"kernel.etl" || name == L"user.etl" || name == L"heap.etl"; };
	tempTraces.erase(std::remove_if(tempTraces.begin(), tempTraces.end(), ifInvalid), tempTraces.end());
	for (auto& name : tempTraces)
	{
		// Trim off the file extension. Yes, this is mutating the vector.
		name = CrackFilePart(name);
	}
	// The same trace may show up as .etl and as .zip (compressed). Delete
	// one copy.
	tempTraces.erase(std::unique(tempTraces.begin(), tempTraces.end()), tempTraces.end());

	// If nothing has changed, do nothing. This avoids redrawing when nothing
	// important has happened.
	if (tempTraces != traces_)
	{
		traces_ = tempTraces;

		// Avoid flicker by disabling redraws until the list has been rebuilt.
		btTraces_.SetRedraw(FALSE);
		// Erase all entries and replace them.
		btTraces_.ResetContent();
		for (int curIndex = 0; curIndex < (int)traces_.size(); ++curIndex)
		{
			const auto& name = traces_[curIndex];
			btTraces_.AddString(name.c_str());
			if (name == selectedTraceName)
			{
				// We compare trimmed traceNames (thus ignoring extensions) so
				// that if compressing traces changes the extension (from .etl
				// to .zip) we won't lose our current selection.
				curSel = curIndex;
			}
		}
		if (curSel >= (int)traces_.size())
			curSel = (int)traces_.size() - 1;
		btTraces_.SetCurSel(curSel);
		btTraces_.SetRedraw(TRUE);
	}

	// The change may have been an edit to the current traces notes, so
	// reload if necessary. If the trace notes have been edited in UIforETW
	// as well as on disk, then, well, somebody will lose.
	UpdateNotesState();
}

LRESULT CUIforETWDlg::UpdateTraceListHandler(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	UpdateTraceList();

	return 0;
}


void CUIforETWDlg::OnLbnDblclkTracelist()
{
	int selIndex = btTraces_.GetCurSel();
	// This check shouldn't be necessary, but who knows?
	if (selIndex < 0 || selIndex >= (int)traces_.size())
		return;
	std::wstring tracename = GetTraceDir() + traces_[selIndex] + L".etl";
	LaunchTraceViewer(tracename, wpaDefaultPath_);
}

void CUIforETWDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (!initialWidth_)
		return;

	// Don't let the dialog be smaller than its initial size.
	lpMMI->ptMinTrackSize.x = initialWidth_;
	lpMMI->ptMinTrackSize.y = initialHeight_;
}


void CUIforETWDlg::OnSize(UINT nType, int /*cx*/, int /*cy*/)
{
	if (nType == SIZE_RESTORED && initialWidth_)
	{
		FinishTraceRename();
		// Calculate xDelta and yDelta -- the change in the window's size.
		CRect windowRect;
		GetWindowRect(&windowRect);
		int xDelta = windowRect.Width() - lastWidth_;
		lastWidth_ += xDelta;
		int yDelta = windowRect.Height() - lastHeight_;
		lastHeight_ += yDelta;

		UINT flags = SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE;

		// Resize the trace list and notes control.
		CRect listRect;
		btTraces_.GetWindowRect(&listRect);
		btTraces_.SetWindowPos(nullptr, 0, 0, listRect.Width(), listRect.Height() + yDelta, flags);
		int curSel = btTraces_.GetCurSel();
		if (curSel != LB_ERR)
		{
			// Make the selected line visible.
			btTraces_.SetTopIndex(curSel);
		}

		CRect editRect;
		btTraceNotes_.GetWindowRect(&editRect);
		btTraceNotes_.SetWindowPos(nullptr, 0, 0, editRect.Width() + xDelta, editRect.Height() + yDelta, flags);
	}
}

void CUIforETWDlg::SaveNotesIfNeeded()
{
	// Get the currently selected text, which might have been edited.
	std::wstring editedNotes = GetEditControlText(btTraceNotes_);
	if (editedNotes != traceNotes_)
	{
		if (!traceNoteFilename_.empty())
		{
			WriteTextAsFile(traceNoteFilename_, editedNotes);
		}
		traceNotes_ = editedNotes;
	}
}

void CUIforETWDlg::UpdateNotesState()
{
	SaveNotesIfNeeded();

	int curSel = btTraces_.GetCurSel();
	if (curSel >= 0 && curSel < (int)traces_.size())
	{
		SmartEnableWindow(btTraceNotes_.m_hWnd, true);
		std::wstring traceName = traces_[curSel];
		traceNoteFilename_ = GetTraceDir() + traceName + L".txt";
		traceNotes_ = LoadFileAsText(traceNoteFilename_);
		SetDlgItemText(IDC_TRACENOTES, traceNotes_.c_str());
	}
	else
	{
		SmartEnableWindow(btTraceNotes_.m_hWnd, false);
		SetDlgItemText(IDC_TRACENOTES, L"");
	}
}

void CUIforETWDlg::NotesSelectAll()
{
	btTraceNotes_.SetSel(0, -1, TRUE);
}

void CUIforETWDlg::NotesPaste()
{
	const auto clipText = ConvertToCRLF(GetClipboardText());
	if (clipText.empty())
		return;
	btTraceNotes_.ReplaceSel(clipText.c_str());
}

void CUIforETWDlg::OnLbnSelchangeTracelist()
{
	UpdateNotesState();
}


void CUIforETWDlg::OnBnClickedAbout()
{
	CATLAboutDlg dlgAbout;
	dlgAbout.DoModal();
}

LRESULT CUIforETWDlg::OnHotKey(WPARAM wParam, LPARAM /*lParam*/)
{
	switch (wParam)
	{
	case kRecordTraceHotKey:
		// Don't record a trace if we haven't started tracing.
		if (bIsTracing_)
			StopTracingAndMaybeRecord(true);
		break;
	}

	return 0;
}


// Magic sauce to make tooltips work.
BOOL CUIforETWDlg::PreTranslateMessage(MSG* pMsg)
{
	toolTip_.RelayEvent(pMsg);
	// Handle always-present keyboard shortcuts.
	if (::TranslateAccelerator(m_hWnd, hAccelTable_, pMsg))
		return TRUE;
	if (CWnd::GetFocus() == &btTraceNotes_)
	{
		// This accelerator table is only available when editing trace notes.
		if (::TranslateAccelerator(m_hWnd, hNotesAccelTable_, pMsg))
			return TRUE;
	}
	if (CWnd::GetFocus() == &btTraces_)
	{
		// This accelerator table is only available when the trace list is active.
		if (::TranslateAccelerator(m_hWnd, hTracesAccelTable_, pMsg))
			return TRUE;
	}
	// This accelerator table is only available when renaming.
	if (btTraceNameEdit_.IsWindowVisible())
	{
		if (::TranslateAccelerator(m_hWnd, hRenameAccelTable_, pMsg))
			return TRUE;
	}
	return CDialog::PreTranslateMessage(pMsg);
}


void CUIforETWDlg::SetHeapTracing(bool forceOff)
{
	DWORD tracingFlags = tracingMode_ == kHeapTracingToFile ? 1 : 0;
	if (forceOff)
		tracingFlags = 0;
	for (const auto& tracingName : split(heapTracingExes_, ';'))
	{
		std::wstring targetKey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options";
		CreateRegistryKey(HKEY_LOCAL_MACHINE, targetKey, tracingName);
		targetKey += L"\\" + tracingName;
		SetRegistryDWORD(HKEY_LOCAL_MACHINE, targetKey, L"TracingFlags", tracingFlags);
	}
}

void CUIforETWDlg::OnCbnSelchangeTracingmode()
{
	tracingMode_ = (TracingMode)btTracingMode_.GetCurSel();
	switch (tracingMode_)
	{
	case kTracingToMemory:
		outputPrintf(L"Traces will be recorded to in-memory circular buffers. Tracing can be enabled "
			L"indefinitely long, and will record the last ~10-60 seconds.\n");
		break;
	case kTracingToFile:
		outputPrintf(L"Traces will be recorded to disk to allow arbitrarily long recordings.\n");
		break;
	case kHeapTracingToFile:
		outputPrintf(L"Heap traces will be recorded to disk for %s. Note that only %s processes "
			L"started after this is selected will be traced. \n"
			L"To keep trace sizes manageable you may want to turn off context switch and CPU "
			L"sampling call stacks.\n", heapTracingExes_.c_str(), heapTracingExes_.c_str());
		break;
	}
	SetHeapTracing(false);
}


void CUIforETWDlg::OnBnClickedSettings()
{
	CSettings dlgSettings(nullptr, GetExeDir(), GetWPTDir());
	dlgSettings.heapTracingExes_ = heapTracingExes_;
	dlgSettings.WSMonitoredProcesses_ = WSMonitoredProcesses_;
	dlgSettings.bExpensiveWSMonitoring_ = bExpensiveWSMonitoring_;
	dlgSettings.extraKernelFlags_ = extraKernelFlags_;
	dlgSettings.extraKernelStacks_ = extraKernelStacks_;
	dlgSettings.bChromeDeveloper_ = bChromeDeveloper_;
	dlgSettings.bAutoViewTraces_ = bAutoViewTraces_;
	dlgSettings.bHeapStacks_ = bHeapStacks_;
	dlgSettings.bVirtualAllocStacks_ = bVirtualAllocStacks_;
	dlgSettings.chromeKeywords_ = chromeKeywords_;
	if (dlgSettings.DoModal() == IDOK)
	{
		// If the heap tracing executable name has changed then clear and
		// then (potentially) reset the registry key, otherwise the other
		// executable may end up with heap tracing enabled indefinitely.
		if (heapTracingExes_ != dlgSettings.heapTracingExes_)
		{
			// Force heap tracing off
			SetHeapTracing(true);
			heapTracingExes_ = dlgSettings.heapTracingExes_;
			// Potentially re-enable heap tracing with the new name.
			SetHeapTracing(false);
		}

		// Update the symbol path if the chrome developer flag has changed.
		// This will be a NOP if the user set _NT_SYMBOL_PATH outside of
		// UIforETW.
		if (bChromeDeveloper_ != dlgSettings.bChromeDeveloper_)
		{
			bChromeDeveloper_ = dlgSettings.bChromeDeveloper_;
			SetSymbolPath();
		}

		// Copy over the remaining settings.
		WSMonitoredProcesses_ = dlgSettings.WSMonitoredProcesses_;
		bExpensiveWSMonitoring_ = dlgSettings.bExpensiveWSMonitoring_;
		extraKernelFlags_ = dlgSettings.extraKernelFlags_;
		extraKernelStacks_ = dlgSettings.extraKernelStacks_;
		workingSetThread_.SetProcessFilter(WSMonitoredProcesses_, bExpensiveWSMonitoring_);

		bAutoViewTraces_ = dlgSettings.bAutoViewTraces_;
		bHeapStacks_ = dlgSettings.bHeapStacks_;
		bVirtualAllocStacks_ = dlgSettings.bVirtualAllocStacks_;
		chromeKeywords_ = dlgSettings.chromeKeywords_;
	}
}

void CUIforETWDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	// See if we right-clicked on the trace list.
	if (pWnd == &btTraces_)
	{
		int selIndex = btTraces_.GetCurSel();

		CMenu PopupMenu;
		PopupMenu.LoadMenu(IDR_TRACESCONTEXTMENU);

		CMenu *pContextMenu = PopupMenu.GetSubMenu(0);

		std::wstring traceFile;
		std::wstring tracePath;
		if (selIndex >= 0)
		{
			// Either 8.1, 10, or both must be installed or else UIforETW won't run.
			if (wptDir_ == wpt10Dir_)
			{
				// If the two variables match then WPT 8.1 is not installed, so disable it.
				pContextMenu->SetDefaultItem(ID_TRACES_OPENTRACEIN10WPA);
				pContextMenu->EnableMenuItem(ID_TRACES_OPENTRACEINWPA, MF_BYCOMMAND | MF_GRAYED);
			}
			else
			{
				// 8.1 is definitely installed, but is 10 installed?
				if (PathFileExists(wpa10Path_.c_str()))
				{
					// If WPT 10 is installed then make it the default.
					pContextMenu->SetDefaultItem(ID_TRACES_OPENTRACEIN10WPA);
				}
				else
				{
					// If WPT 10 is not installed then disable it and make WPT 8.1
					// the default.
					pContextMenu->EnableMenuItem(ID_TRACES_OPENTRACEIN10WPA, MF_BYCOMMAND | MF_GRAYED);
					pContextMenu->SetDefaultItem(ID_TRACES_OPENTRACEINWPA);
				}
			}
			if (!PathFileExists(mxaPath_.c_str()))
			{
				pContextMenu->EnableMenuItem(ID_TRACES_OPENTRACEINEXPERIENCEANALYZER, MF_BYCOMMAND | MF_GRAYED);
			}
			traceFile = traces_[selIndex];
			tracePath = GetTraceDir() + traceFile + L".etl";
		}
		else
		{
			// List of menu items that are disabled when no trace is selected.
			// Those that are always available are commented out in this list.
			int disableList[] =
			{
				ID_TRACES_OPENTRACEINWPA,
				ID_TRACES_OPENTRACEIN10WPA,
				ID_TRACES_OPENTRACEINGPUVIEW,
				ID_TRACES_OPENTRACEINEXPERIENCEANALYZER,
				ID_TRACES_DELETETRACE,
				ID_TRACES_RENAMETRACE,
				ID_TRACES_COMPRESSTRACE,
				ID_TRACES_ZIPCOMPRESSTRACE,
				//ID_TRACES_COMPRESSTRACES,
				//ID_TRACES_ZIPCOMPRESSALLTRACES,
				//ID_TRACES_BROWSEFOLDER,
				ID_TRACES_STRIPCHROMESYMBOLS,
				ID_TRACES_IDENTIFYCHROMEPROCESSES,
				ID_TRACES_TRACEPATHTOCLIPBOARD,
			};

			for (const auto& id : disableList)
				pContextMenu->EnableMenuItem(id, MF_BYCOMMAND | MF_GRAYED);
		}

		if (GetWindowsVersion() < kWindowsVersion8)
		{
			// Disable ETW trace compress options on Windows 7 and below
			// since they don't work there.
			pContextMenu->EnableMenuItem(ID_TRACES_COMPRESSTRACE, MF_BYCOMMAND | MF_GRAYED);
			pContextMenu->EnableMenuItem(ID_TRACES_COMPRESSTRACES, MF_BYCOMMAND | MF_GRAYED);
		}

		int selection = pContextMenu->TrackPopupMenu(TPM_LEFTALIGN | TPM_LEFTBUTTON |
			TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
			point.x, point.y, pWnd, NULL);

		switch (selection)
		{
			case ID_TRACES_OPENTRACEINWPA:
				LaunchTraceViewer(tracePath, wpaPath_);
				break;
			case ID_TRACES_OPENTRACEIN10WPA:
				LaunchTraceViewer(tracePath, wpa10Path_);
				break;
			case ID_TRACES_OPENTRACEINGPUVIEW:
				LaunchTraceViewer(tracePath, gpuViewPath_);
				break;
			case ID_TRACES_OPENTRACEINEXPERIENCEANALYZER:
				LaunchTraceViewer(tracePath, mxaPath_);
				break;
			case ID_TRACES_DELETETRACE:
				DeleteTrace();
				break;
			case ID_TRACES_RENAMETRACE:
				StartRenameTrace(false);
				break;
			case ID_TRACES_COMPRESSTRACE:
				CompressTrace(tracePath);
				break;
			case ID_TRACES_ZIPCOMPRESSTRACE:
				AfxMessageBox(L"Not implemented yet.");
				break;
			case ID_TRACES_COMPRESSTRACES:
				CompressAllTraces();
				break;
			case ID_TRACES_ZIPCOMPRESSALLTRACES:
				AfxMessageBox(L"Not implemented yet.");
				break;
			case ID_TRACES_BROWSEFOLDER:
				ShellExecute(NULL, L"open", GetTraceDir().c_str(), NULL, GetTraceDir().c_str(), SW_SHOW);
				break;
			case ID_TRACES_STRIPCHROMESYMBOLS:
				outputPrintf(L"\n");
				StripChromeSymbols(tracePath);
				break;
			case ID_TRACES_IDENTIFYCHROMEPROCESSES:
				SaveNotesIfNeeded();
				outputPrintf(L"\n");
				IdentifyChromeProcesses(tracePath);
				UpdateNotesState();
				break;
			case ID_TRACES_TRACEPATHTOCLIPBOARD:
				// Comma delimited for easy pasting into DOS commands.
				SetClipboardText(L"\"" + tracePath + L"\"");
				break;
		}
	}
	else
	{
		CDialog::OnContextMenu(pWnd, point);
	}
}


void CUIforETWDlg::DeleteTrace()
{
	int selIndex = btTraces_.GetCurSel();

	if (selIndex >= 0)
	{
		std::wstring traceFile = traces_[selIndex];
		std::wstring tracePath = GetTraceDir() + traceFile + L".etl";
		if (AfxMessageBox((L"Are you sure you want to delete " + traceFile + L"?").c_str(), MB_YESNO) == IDYES)
		{
			std::wstring pattern = GetTraceDir() + traceFile + L".*";
			if (DeleteFiles(*this, GetFileList(pattern, true)))
			{
				outputPrintf(L"\nFile deletion failed.\n");
			}
			// Record that the trace notes don't need saving, even if they have changed.
			traceNoteFilename_ = L"";
		}
	}
}


void CUIforETWDlg::CopyTraceName()
{
	int selIndex = btTraces_.GetCurSel();

	if (selIndex >= 0)
	{
		std::wstring tracePath = traces_[selIndex] + L".etl";
		// If the shift key is held down, just put the file path in the clipboard.
		// If not, put the entire path in the clipboard. This is an undocumented
		// but very handy option.
		if (GetKeyState(VK_SHIFT) >= 0)
			tracePath = GetTraceDir() + tracePath;
		SetClipboardText(L"\"" + tracePath + L"\"");
	}
}


void CUIforETWDlg::OnOpenTraceWPA()
{
	int selIndex = btTraces_.GetCurSel();

	if (selIndex >= 0)
	{
		std::wstring tracePath = GetTraceDir() + traces_[selIndex] + L".etl";
		LaunchTraceViewer(tracePath, wpaPath_);
	}
}

void CUIforETWDlg::OnOpenTrace10WPA()
{
	int selIndex = btTraces_.GetCurSel();

	if (selIndex >= 0)
	{
		std::wstring tracePath = GetTraceDir() + traces_[selIndex] + L".etl";
		LaunchTraceViewer(tracePath, wpa10Path_);
	}
}

void CUIforETWDlg::OnOpenTraceGPUView()
{
	int selIndex = btTraces_.GetCurSel();

	if (selIndex >= 0)
	{
		std::wstring tracePath = GetTraceDir() + traces_[selIndex] + L".etl";
		LaunchTraceViewer(tracePath, gpuViewPath_);
	}
}

std::pair<uint64_t, uint64_t> CUIforETWDlg::CompressTrace(const std::wstring& tracePath) const
{
	std::wstring compressedPath = tracePath + L".compressed";
	DWORD exitCode = 0;
	{
		ChildProcess child(GetXperfPath());
		std::wstring args = L" -merge \"" + tracePath + L"\" \"" + compressedPath + L"\" -compress";
		child.Run(bShowCommands_, L"xperf.exe" + args);
		exitCode = child.GetExitCode();
	}

	if (exitCode)
	{
		DeleteOneFile(*this, compressedPath);
		return std::pair<uint64_t, uint64_t>();
	}

	int64_t originalSize = GetFileSize(tracePath);
	int64_t compressedSize = GetFileSize(compressedPath);
	// Require a minimum of 1% compression
	if (compressedSize > 0 && compressedSize < (originalSize - originalSize / 100))
	{
		DeleteOneFile(*this, tracePath);
		MoveFile(compressedPath.c_str(), tracePath.c_str());
		outputPrintf(L"%s was compressed from %1.1f MB to %1.1f MB.\n",
			tracePath.c_str(), originalSize / 1e6, compressedSize / 1e6);
	}
	else
	{
		outputPrintf(L"%s was not compressed.\n", tracePath.c_str());
		DeleteOneFile(*this, compressedPath);
		compressedSize = originalSize; // So that callers will know that nothing happened.
	}
	return std::pair<uint64_t, uint64_t>(originalSize, compressedSize);
}


void CUIforETWDlg::CompressAllTraces() const
{
	outputPrintf(L"\nCompressing all traces - this may take a while:\n");
	int64_t initialTotalSize = 0;
	int64_t finalTotalSize = 0;
	int notCompressedCount = 0;
	int compressedCount = 0;
	for (const auto& traceName : traces_)
	{
		auto result = CompressTrace(GetTraceDir() + traceName + L".etl");
		if (result.first == result.second)
		{
			++notCompressedCount;
		}
		else
		{
			++compressedCount;
			initialTotalSize += result.first;
			finalTotalSize += result.second;
		}
	}
	outputPrintf(L"Finished compressing traces.\n");
	outputPrintf(L"%d traces not compressed. %d traces compressed from %1.1f MB to %1.1f MB.\n",
		notCompressedCount, compressedCount, initialTotalSize / 1e6, finalTotalSize / 1e6);
}


void CUIforETWDlg::StripChromeSymbols(const std::wstring& traceFilename)
{
	// Some private symbols, particularly Chrome's, must be stripped and
	// then converted to .symcache files in order to avoid ~25 minute
	// conversion times for the full private symbols.
	// https://randomascii.wordpress.com/2014/11/04/slow-symbol-loading-in-microsofts-profiler-take-two/
	// Call Python script here, or recreate it in C++.
	std::wstring pythonPath = FindPython();
	if (!pythonPath.empty())
	{
		outputPrintf(L"Stripping Chrome symbols - this may take a while...\n");
		ElapsedTimer stripTimer;
		{
			ChildProcess child(pythonPath);
			// Must pass -u to disable Python's output buffering when printing to
			// a pipe, in order to get timely feedback.
			std::wstring args = L" -u \"" + GetExeDir() + L"StripChromeSymbols.py\" \"" + traceFilename + L"\"";
			child.Run(bShowCommands_, GetFilePart(pythonPath) + args);
		}
		if (bShowCommands_)
			outputPrintf(L"Stripping Chrome symbols took %1.1f s\n", stripTimer.ElapsedSeconds());
	}
	else
	{
		outputPrintf(L"Can't find Python. Chrome symbol stripping disabled.\n");
	}
}


void CUIforETWDlg::IdentifyChromeProcesses(const std::wstring& traceFilename)
{
	outputPrintf(L"Preprocessing trace to identify Chrome processes...\n");
	// There was a version of this that was written in C++. See the history for details.
	std::wstring pythonPath = FindPython();
	if (!pythonPath.empty())
	{
		ChildProcess child(pythonPath);
		std::wstring args = L" -u \"" + GetExeDir() + L"IdentifyChromeProcesses.py\" \"" + traceFilename + L"\"";
		child.Run(bShowCommands_, GetFilePart(pythonPath) + args);
		std::wstring output = child.GetOutput();
		// The output of the script is appended to the trace description file.
		std::wstring textFilename = StripExtensionFromPath(traceFilename) + L".txt";
		std::wstring data = LoadFileAsText(textFilename) + output;
		WriteTextAsFile(textFilename, data);
	}
	else
	{
		outputPrintf(L"Couldn't find python.\n");
	}
}


void CUIforETWDlg::PreprocessTrace(const std::wstring& traceFilename)
{
	if (bChromeDeveloper_)
	{
		IdentifyChromeProcesses(traceFilename);
	}
}


void CUIforETWDlg::StartRenameTrace(bool fullRename)
{
	SaveNotesIfNeeded();
	int curSel = btTraces_.GetCurSel();
	if (curSel >= 0 && curSel < (int)traces_.size())
	{
		std::wstring traceName = traces_[curSel];
		// If the trace name starts with the default date/time pattern
		// then just allow editing the suffix. Othewise allow editing
		// the entire name.
		validRenameDate_ = false;
		if (traceName.size() >= kPrefixLength && !fullRename)
		{
			validRenameDate_ = true;
			for (size_t i = 0; i < kPrefixLength; ++i)
			{
				wchar_t c = traceName[i];
				if (c != '-' && c != '_' && c != '.' && !iswdigit(c))
					validRenameDate_ = false;
			}
		}
		std::wstring editablePart = traceName;
		if (validRenameDate_)
		{
			editablePart = traceName.substr(kPrefixLength, traceName.size());
		}
		// This gets the location of the selected item relative to the
		// list box.
		CRect itemRect;
		btTraces_.GetItemRect(curSel, &itemRect);
		CRect newEditRect = traceNameEditRect_;
		newEditRect.MoveToY(traceNameEditRect_.top + itemRect.top);
		if (!validRenameDate_)
		{
			// Extend the edit box to the full list box width.
			CRect listBoxRect;
			btTraces_.GetWindowRect(&listBoxRect);
			ScreenToClient(&listBoxRect);
			newEditRect.left = listBoxRect.left;
		}
		btTraceNameEdit_.MoveWindow(newEditRect, TRUE);
		btTraceNameEdit_.SetFocus();
		btTraceNameEdit_.SetWindowTextW(editablePart.c_str());
		// Select the text for easy replacing.
		btTraceNameEdit_.SetSel(0, -1);
		btTraceNameEdit_.ShowWindow(SW_SHOWNORMAL);
		preRenameTraceName_ = traceName;
	}
}

void CUIforETWDlg::OnRenameKey()
{
	if (!btTraceNameEdit_.IsWindowVisible())
		StartRenameTrace(false);
}

void CUIforETWDlg::OnFullRenameKey()
{
	// Undocumented option to allow renaming of the entire trace, instead
	// of just the post date/time portion, by using Shift+F2.
	if (!btTraceNameEdit_.IsWindowVisible())
		StartRenameTrace(true);
}


void CUIforETWDlg::FinishTraceRename()
{
	// Make sure this doesn't get double-called.
	if (!btTraceNameEdit_.IsWindowVisible())
		return;
	std::wstring newText = GetEditControlText(btTraceNameEdit_);
	std::wstring newTraceName = newText;
	if (validRenameDate_)
		newTraceName = preRenameTraceName_.substr(0, kPrefixLength) + newText;
	btTraceNameEdit_.ShowWindow(SW_HIDE);

	if (newTraceName != preRenameTraceName_)
	{
		auto oldNames = GetFileList(GetTraceDir() + preRenameTraceName_ + L".*");
		std::vector<std::pair<std::wstring, std::wstring>> renamed;
		renamed.reserve(oldNames.size());
		std::wstring failedSource;
		for (const auto& oldName : oldNames)
		{
			std::wstring extension = GetFileExt(oldName);
			std::wstring newName = newTraceName + extension;
			BOOL result = MoveFile((GetTraceDir() + oldName).c_str(), (GetTraceDir() + newName).c_str());
			if (!result)
			{
				failedSource = oldName;
				break;
			}
			renamed.emplace_back(std::make_pair(oldName, newName));
		}
		// If any of the renaming steps fail then undo the renames that
		// succeeded. This should usually fail. If not, there isn't much that
		// can be done anyway.
		if (failedSource.empty())
		{
			// Record that the notes don't need saving -- the
			// traceNoteFilename_ is out of date now. It will be updated
			// when the directory notification fires.
			traceNoteFilename_ = L"";
		}
		else
		{
			for (const auto& renamePair : renamed)
			{
				(void)MoveFile((GetTraceDir() + renamePair.second).c_str(), (GetTraceDir() + renamePair.first).c_str());
			}
			AfxMessageBox((L"Error renaming file '" + failedSource + L"'.").c_str());
		}
	}
	btTraces_.SetFocus();
}

void CUIforETWDlg::CancelTraceRename()
{
	if (!btTraceNameEdit_.IsWindowVisible())
		return;
	// If the trace name edit window is visible then hide it.
	// That's it.
	btTraceNameEdit_.ShowWindow(SW_HIDE);
	btTraces_.SetFocus();
}

void CUIforETWDlg::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	if (nState == WA_INACTIVE)
	{
		// When the window goes inactive then the user may be switching to
		// explorer/cmd to copy the notes file, so we need to make sure it
		// is up-to-date.
		SaveNotesIfNeeded();
	}
	CDialog::OnActivate(nState, pWndOther, bMinimized);
}
