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

	for (const wchar_t* pBuf = buffer; *pBuf; ++pBuf)
	{
		// Need \r\n as a line separator.
		if (pBuf[0] == '\n')
		{
			// Don't add a line separator at the very beginning.
			if (!output_.empty())
				output_ += L"\r\n";
		}
		else
			output_ += pBuf[0];
	}

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
	ON_BN_CLICKED(ID_TRACES_OPENTRACEINGPUVIEW, &CUIforETWDlg::OnOpenTraceGPUView)
	ON_BN_CLICKED(ID_RENAME, &CUIforETWDlg::OnRenameKey)
	ON_BN_CLICKED(ID_RENAMEFULL, &CUIforETWDlg::OnFullRenameKey)
	ON_EN_KILLFOCUS(IDC_TRACENAMEEDIT, &CUIforETWDlg::FinishTraceRename)
	ON_BN_CLICKED(ID_ENDRENAME, &CUIforETWDlg::FinishTraceRename)
	ON_BN_CLICKED(ID_ESCKEY, &CUIforETWDlg::CancelTraceRename)
	ON_BN_CLICKED(IDC_GPUTRACING, &CUIforETWDlg::OnBnClickedGPUtracing)
	ON_BN_CLICKED(ID_COPYTRACENAME, &CUIforETWDlg::CopyTraceName)
	ON_BN_CLICKED(ID_DELETETRACE, &CUIforETWDlg::DeleteTrace)
	ON_BN_CLICKED(ID_SELECTALL, &CUIforETWDlg::SelectAll)
END_MESSAGE_MAP()


void CUIforETWDlg::SetSymbolPath()
{
	// Make sure that the symbol paths are set.

#pragma warning(suppress : 4996)
	if (bManageSymbolPath_ || !getenv("_NT_SYMBOL_PATH"))
	{
		bManageSymbolPath_ = true;
		std::string symbolPath = "SRV*" + systemDrive_ + "symbols*http://msdl.microsoft.com/download/symbols";
		if (bChromeDeveloper_)
			symbolPath = "SRV*" + systemDrive_ + "symbols*http://msdl.microsoft.com/download/symbols;SRV*" + systemDrive_ + "symbols*https://chromium-browser-symsrv.commondatastorage.googleapis.com";
		(void)_putenv(("_NT_SYMBOL_PATH=" + symbolPath).c_str());
		outputPrintf(L"Setting _NT_SYMBOL_PATH to %s (Microsoft%s). "
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

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
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
	VERIFY(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Windows, 0, NULL, &windowsDir)));
	windowsDir_ = windowsDir;
	windowsDir_ += '\\';
	CoTaskMemFree(windowsDir);
	// ANSI string, not unicode.
	systemDrive_ = static_cast<char>(windowsDir_[0]);
	systemDrive_ += ":\\";

	// The WPT 8.1 installer is always a 32-bit installer, so we look for it in
	// ProgramFilesX86, on 32-bit and 64-bit operating systems.
	wchar_t* progFilesx86Dir = nullptr;
	VERIFY(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, NULL, &progFilesx86Dir)));
	wptDir_ = progFilesx86Dir;
	wptDir_ += L"\\Windows Kits\\8.1\\Windows Performance Toolkit\\";
	CoTaskMemFree(progFilesx86Dir);
	if (!PathFileExists(GetXperfPath().c_str()))
	{
		AfxMessageBox((GetXperfPath() + L" does not exist. Please install WPT 8.1. Exiting.").c_str());
		exit(10);
	}

	wchar_t documents[MAX_PATH];
	if (!SHGetSpecialFolderPath(*this, documents, CSIDL_MYDOCUMENTS, TRUE))
	{
		assert(!"Failed to find My Documents directory.\n");
		exit(10);
	}
	std::wstring defaultTraceDir = documents + std::wstring(L"\\etwtraces\\");
	traceDir_ = GetDirectory(L"etwtracedir", defaultTraceDir);

	std::wstring wpaStartup = documents + std::wstring(L"\\WPA Files\\Startup.wpaProfile");
	if (!PathFileExists(wpaStartup.c_str()))
	{
		// Auto-copy a startup profile if there isn't one.
		CopyFile((GetExeDir() + L"Startup.wpaProfile").c_str(), wpaStartup.c_str(), TRUE);
	}

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
		SmartEnableWindow(btCompress_, false);
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
	std::wstring dllSource = GetExeDir() + L"ETWProviders.dll";
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

	// Register chrome.dll if the Chrome Developer option is set.
	if (bChromeDeveloper_)
	{
		std::wstring manifestPath = GetExeDir() + L"chrome_events_win.man";
		std::wstring dllSuffix = L"chrome.dll";
		// Make sure we have a trailing backslash in the path.
		if (chromeDllPath_.back() != L'\\')
			chromeDllPath_ += L'\\';
		std::wstring chromeDllFullPath = chromeDllPath_ + dllSuffix;
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
	SmartEnableWindow(btStartTracing_, !bIsTracing_);
	SmartEnableWindow(btSaveTraceBuffers_, bIsTracing_);
	SmartEnableWindow(btStopTracing_, bIsTracing_);
	SmartEnableWindow(btTracingMode_, !bIsTracing_);

	SmartEnableWindow(btSampledStacks_, !bIsTracing_);
	SmartEnableWindow(btCswitchStacks_, !bIsTracing_);
	SmartEnableWindow(btGPUTracing_, !bIsTracing_);
}

void CUIforETWDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
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
		filePart += L"_" + heapTracingExe_.substr(0, heapTracingExe_.size() - 4);
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
		outputPrintf(L"\nStarting heap tracing to disk of %s...\n", heapTracingExe_.c_str());
	else
		assert(0);

	std::wstring kernelProviders = L" Latency+POWER+DISPATCHER+FILE_IO+FILE_IO_INIT+VIRT_ALLOC+MEMINFO+MEMINFO_WS";
	std::wstring kernelStackWalk = L"";
	if (bSampledStacks_ && bCswitchStacks_)
		kernelStackWalk = L" -stackwalk PROFILE+CSWITCH+READYTHREAD";
	else if (bSampledStacks_)
		kernelStackWalk = L" -stackwalk PROFILE";
	else if (bCswitchStacks_)
		kernelStackWalk = L" -stackwalk CSWITCH+READYTHREAD";
	// Buffer sizes are in KB, so 1024 is actually 1 MB
	// Make this configurable.
	std::wstring kernelBuffers = L" -buffersize 1024 -minbuffers 600 -maxbuffers 600";
	std::wstring kernelFile = L" -f \"" + GetKernelFile() + L"\"";
	if (tracingMode_ == kTracingToMemory)
		kernelFile = L" -buffering";
	std::wstring kernelArgs = L" -start " + GetKernelLogger() + L" -on" + kernelProviders + kernelStackWalk + kernelBuffers + kernelFile;

	WindowsVersion winver = GetWindowsVersion();
	std::wstring userProviders = L"Microsoft-Windows-Win32k";
	if (winver <= kWindowsVersionVista)
		userProviders = L"Microsoft-Windows-LUA"; // Because Microsoft-Windows-Win32k doesn't work on Vista.
	userProviders += L"+Multi-MAIN+Multi-FrameRate+Multi-Input+Multi-Worker";

	// DWM providers can be helpful also. Uncomment to enable.
	//userProviders += L"+Microsoft-Windows-Dwm-Dwm";
	// Theoretically better power monitoring data, Windows 7+, but it doesn't
	// seem to work.
	//userProviders += L"+Microsoft-Windows-Kernel-Processor-Power+Microsoft-Windows-Kernel-Power";

	if (useChromeProviders_)
		userProviders += L"+Chrome";

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

	std::wstring userBuffers = L" -buffersize 1024 -minbuffers 100 -maxbuffers 100";
	// Increase the user buffer sizes when doing graphics tracing.
	if (bGPUTracing_)
		userBuffers = L" -buffersize 1024 -minbuffers 200 -maxbuffers 200";
	std::wstring userFile = L" -f \"" + GetUserFile() + L"\"";
	if (tracingMode_ == kTracingToMemory)
		userFile = L" -buffering";
	std::wstring userArgs = L" -start UIforETWSession -on " + userProviders + userBuffers + userFile;

	// Heap tracing settings -- only used for heap tracing.
	// Could also record stacks on HeapFree
	// Buffer sizes need to be huge for some programs - should be configurable.
	std::wstring heapBuffers = L" -buffersize 1024 -minbuffers 1000";
	std::wstring heapFile = L" -f \"" + GetHeapFile() + L"\"";
	std::wstring heapStackWalk;
	if (bHeapStacks_)
		heapStackWalk = L" -stackwalk HeapCreate+HeapDestroy+HeapAlloc+HeapRealloc";
	std::wstring heapArgs = L" -start xperfHeapSession -heap -Pids 0" + heapStackWalk + heapBuffers + heapFile;

	{
		ChildProcess child(GetXperfPath());
		if (tracingMode_ == kHeapTracingToFile)
			child.Run(bShowCommands_, L"xperf.exe" + kernelArgs + userArgs + heapArgs);
		else
			child.Run(bShowCommands_, L"xperf.exe" + kernelArgs + userArgs);

		DWORD exitCode = child.GetExitCode();
		if (exitCode)
		{
			outputPrintf(L"Error starting tracing. Try stopping tracing and then starting it again?\n");
		}
		else
		{
			outputPrintf(L"Tracing is started.\n");
		}
	}

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
		if (bSaveTrace && tracingMode_ == kTracingToMemory)
		{
			// If we are in memory tracing mode then don't actually stop tracing,
			// just flush the buffers to disk.
			std::wstring args = L" -flush " + GetKernelLogger() + L" -f \"" + GetKernelFile() + L"\" -flush UIforETWSession -f \"" + GetUserFile() + L"\"";
			child.Run(bShowCommands_, L"xperf.exe" + args);
		}
		else
		{
			if (tracingMode_ == kHeapTracingToFile)
				child.Run(bShowCommands_, L"xperf.exe -stop xperfHeapSession -stop UIforETWSession -stop " + GetKernelLogger());
			else
				child.Run(bShowCommands_, L"xperf.exe -stop UIforETWSession -stop " + GetKernelLogger());
		}
	}
	double saveTime = saveTimer.ElapsedSeconds();
	if (bShowCommands_)
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
			LaunchTraceViewer(traceFilename);
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
					L"be because renaming of amcache.hve failed. Please try metatrace.bat "
					L"and share this on "
					L"https://groups.google.com/forum/#!forum/uiforetw\n", mergeTime);
			}
		}

		outputPrintf(L"Finished recording trace.\n");
	}
	else
		outputPrintf(L"Tracing stopped.\n");
}


void CUIforETWDlg::OnBnClickedSavetracebuffers()
{
	StopTracingAndMaybeRecord(true);
}

void CUIforETWDlg::OnBnClickedStoptracing()
{
	StopTracingAndMaybeRecord(false);
}

void CUIforETWDlg::LaunchTraceViewer(const std::wstring traceFilename, const std::wstring viewer)
{
	if (!PathFileExists(traceFilename.c_str()))
	{
		std::wstring zipPath = traceFilename.substr(0, traceFilename.size() - 4) + L".zip";
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

	std::wstring viewerPath = GetWPTDir() + viewer;
	std::wstring viewerName = GetFilePart(viewer);

	const std::wstring args = std::wstring(viewerName + L" \"") + traceFilename.c_str() + L"\"";

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
	ChildProcess child(GetXperfPath());
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
	outputPrintf(L"%s\n", message);
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
		assert(0);
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
		// Trim off the file extension, which *should* always be in .3 form.
		name = name.substr(0, name.size() - 4);
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
	LaunchTraceViewer(tracename);
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
	}
}

void CUIforETWDlg::UpdateNotesState()
{
	SaveNotesIfNeeded();

	int curSel = btTraces_.GetCurSel();
	if (curSel >= 0 && curSel < (int)traces_.size())
	{
		SmartEnableWindow(btTraceNotes_, true);
		std::wstring traceName = traces_[curSel];
		traceNoteFilename_ = GetTraceDir() + traceName + L".txt";
		traceNotes_ = LoadFileAsText(traceNoteFilename_);
		SetDlgItemText(IDC_TRACENOTES, traceNotes_.c_str());
	}
	else
	{
		SmartEnableWindow(btTraceNotes_, false);
		SetDlgItemText(IDC_TRACENOTES, L"");
	}
}

void CUIforETWDlg::SelectAll()
{
	btTraceNotes_.SetSel(0, -1, TRUE);
}

void CUIforETWDlg::OnLbnSelchangeTracelist()
{
	UpdateNotesState();
}


void CUIforETWDlg::OnBnClickedAbout()
{
	CAboutDlg dlgAbout;
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
	std::wstring targetKey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options";
	DWORD tracingFlags = tracingMode_ == kHeapTracingToFile ? 1 : 0;
	if (forceOff)
		tracingFlags = 0;
	CreateRegistryKey(HKEY_LOCAL_MACHINE, targetKey, heapTracingExe_);
	targetKey += L"\\" + heapTracingExe_;
	SetRegistryDWORD(HKEY_LOCAL_MACHINE, targetKey, L"TracingFlags", tracingFlags);
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
			L"started after this is selected will be traced. Note that %s processes started now "
			L"may run slightly slower even if not being traced.\n"
			L"To keep trace sizes manageable you may want to turn off context switch and CPU "
			L"sampling call stacks.\n", heapTracingExe_.c_str(),
			heapTracingExe_.c_str(), heapTracingExe_.c_str());
		break;
	}
	SetHeapTracing(false);
}


void CUIforETWDlg::OnBnClickedSettings()
{
	CSettings dlgAbout(nullptr, GetExeDir(), GetWPTDir());
	dlgAbout.heapTracingExe_ = heapTracingExe_;
	dlgAbout.chromeDllPath_ = chromeDllPath_;
	dlgAbout.bChromeDeveloper_ = bChromeDeveloper_;
	dlgAbout.bAutoViewTraces_ = bAutoViewTraces_;
	dlgAbout.bHeapStacks_ = bHeapStacks_;
	if (dlgAbout.DoModal() == IDOK)
	{
		heapTracingExe_ = dlgAbout.heapTracingExe_;
		chromeDllPath_ = dlgAbout.chromeDllPath_;
		if (bChromeDeveloper_ != dlgAbout.bChromeDeveloper_)
		{
			bChromeDeveloper_ = dlgAbout.bChromeDeveloper_;
			SetSymbolPath();
		}
		bAutoViewTraces_ = dlgAbout.bAutoViewTraces_;
		bHeapStacks_ = dlgAbout.bHeapStacks_;
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
			pContextMenu->SetDefaultItem(ID_TRACES_OPENTRACEINWPA);
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
				ID_TRACES_OPENTRACEINGPUVIEW,
				ID_TRACES_DELETETRACE,
				ID_TRACES_RENAMETRACE,
				ID_TRACES_COMPRESSTRACE,
				ID_TRACES_ZIPCOMPRESSTRACE,
				//ID_TRACES_COMPRESSTRACES,
				//ID_TRACES_ZIPCOMPRESSALLTRACES,
				//ID_TRACES_BROWSEFOLDER,
				ID_TRACES_STRIPCHROMESYMBOLS,
				ID_TRACES_TRACEPATHTOCLIPBOARD,
			};

			for (auto id : disableList)
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
				LaunchTraceViewer(tracePath);
				break;
			case ID_TRACES_OPENTRACEINGPUVIEW:
				LaunchTraceViewer(tracePath, L"gpuview\\GPUView.exe");
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
				outputPrintf(L"\nCompressing all traces - this may take a while:\n");
				for (auto traceName : traces_)
				{
					CompressTrace(GetTraceDir() + traceName + L".etl");
				}
				outputPrintf(L"Finished compressing traces.\n");
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
		LaunchTraceViewer(tracePath);
	}
}

void CUIforETWDlg::OnOpenTraceGPUView()
{
	int selIndex = btTraces_.GetCurSel();

	if (selIndex >= 0)
	{
		std::wstring tracePath = GetTraceDir() + traces_[selIndex] + L".etl";
		LaunchTraceViewer(tracePath, L"gpuview\\GPUView.exe");
	}
}

void CUIforETWDlg::CompressTrace(const std::wstring& tracePath)
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
		return;
	}

	int64_t originalSize = GetFileSize(tracePath);
	int64_t compressedSize = GetFileSize(compressedPath);
	// Require a minimum of 1% compression
	if (compressedSize > 0 && compressedSize < (originalSize - originalSize / 100))
	{
		DeleteOneFile(*this, tracePath);
		MoveFile(compressedPath.c_str(), tracePath.c_str());
		outputPrintf(L"%s was compressed from %1.1f MB to %1.1f MB.\n",
			tracePath.c_str(), originalSize / 1000000.0, compressedSize / 1000000.0);
	}
	else
	{
		outputPrintf(L"%s was not compressed.\n", tracePath.c_str());
		DeleteOneFile(*this, compressedPath);
	}
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
			child.Run(bShowCommands_, L"python.exe" + args);
		}
		if (bShowCommands_)
			outputPrintf(L"Stripping Chrome symbols took %1.1f s\n", stripTimer.ElapsedSeconds());
	}
	else
	{
		outputPrintf(L"Can't find Python. Chrome symbol stripping disabled.");
	}
}


void CUIforETWDlg::PreprocessTrace(const std::wstring& traceFilename)
{
	if (bChromeDeveloper_)
	{
		outputPrintf(L"Preprocessing trace to identify Chrome processes...\n");
#ifdef IDENTIFY_CHROME_PROCESSES_IN_PYTHON
		std::wstring pythonPath = FindPython();
		if (!pythonPath.empty())
		{
			outputPrintf(L"Preprocessing Chrome trace...\n");
			ChildProcess child(pythonPath);
			std::wstring args = L" -u \"" + GetExeDir() + L"IdentifyChromeProcesses.py\" \"" + traceFilename + L"\"";
			child.Run(bShowCommands_, L"python.exe" + args);
			std::wstring output = child.GetOutput();
			// The output of the script is written to the trace description file.
			// Ideally it would be appended, but good enough for now.
			std::wstring textFilename = traceFilename.substr(0, traceFilename.size() - 4) + L".txt";
			WriteTextAsFile(textFilename, output);
		}
#else
		ChildProcess child(GetXperfPath());
		// Typical output of the process action looks like this (large parts
		// elided):
		// ... chrome.exe (3456), ... \chrome.exe" --type=renderer ...
		std::wstring args = L" -i \"" + traceFilename + L"\" -tle -tti -a process -withcmdline";
		child.Run(bShowCommands_, L"xperf.exe" + args);
		std::wstring output = child.GetOutput();
		std::map<std::wstring, std::vector<DWORD>> pidsByType;
		for (auto& line : split(output, '\n'))
		{
			if (wcsstr(line.c_str(), L"chrome.exe"))
			{
				std::wstring type = L"browser";
				const wchar_t* typeLabel = L" --type=";
				const wchar_t* typeFound = wcsstr(line.c_str(), typeLabel);
				if (typeFound)
				{
					typeFound += wcslen(typeLabel);
					const wchar_t* typeEnd = wcschr(typeFound, ' ');
					if (typeEnd)
					{
						type = std::wstring(typeFound).substr(0, typeEnd - typeFound);
					}
				}
				DWORD pid = 0;
				const wchar_t* pidstr = wcschr(line.c_str(), '(');
				if (pidstr)
				{
					swscanf_s(pidstr + 1, L"%lu", &pid);
				}
				if (pid)
				{
					pidsByType[type].push_back(pid);
				}
			}
		}
#pragma warning(suppress : 4996)
		FILE* pFile = _wfopen((traceFilename.substr(0, traceFilename.size() - 4) + L".txt").c_str(), L"a");
		if (pFile)
		{
			fwprintf(pFile, L"Chrome PIDs by process type:\n");
			for (auto& types : pidsByType)
			{
				fwprintf(pFile, L"%-10s:", types.first.c_str());
				for (auto& pid : types.second)
				{
					fwprintf(pFile, L" %lu", pid);
				}
				fwprintf(pFile, L"\n");
			}
			fclose(pFile);
		}
#endif
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
		btTraceNameEdit_.ShowWindow(SW_SHOWNORMAL);
		btTraceNameEdit_.SetFocus();
		btTraceNameEdit_.SetWindowTextW(editablePart.c_str());
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
		std::wstring failedSource;
		for (auto oldName : oldNames)
		{
			std::wstring extension = GetFileExt(oldName);;
			std::wstring newName = newTraceName + extension;
			BOOL result = MoveFile((GetTraceDir() + oldName).c_str(), (GetTraceDir() + newName).c_str());
			if (!result)
			{
				failedSource = oldName;
				break;
			}
			renamed.push_back(std::pair<std::wstring, std::wstring>(oldName, newName));
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
			for (auto& renamePair : renamed)
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
