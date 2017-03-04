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
#include "ChildProcess.h"
#include "Settings.h"
#include "Utility.h"
#include "WorkingSet.h"
#include "Version.h"
#include "TraceLoggingSupport.h"

#include <algorithm>
#include <direct.h>
#include <ETWProviders\etwprof.h>
#include <vector>
#include <map>
#include <ShlObj.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

const int kRecordTraceHotKey = 1234;
const int kTimerID = 5678;

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


static std::wstring TranslateTraceLoggingProvider(const std::wstring& provider)
{
	std::wstring providerOptions;
	std::wstring justProviderName(provider);
	auto endOfProvider = justProviderName.find(L':');
	if (endOfProvider != std::wstring::npos)
	{
		providerOptions = justProviderName.substr(endOfProvider);
		justProviderName.resize(endOfProvider);
	}

	std::wstring providerGUID = TraceLoggingProviderNameToGUID(justProviderName);

	providerGUID += providerOptions;
	return providerGUID;
}

static std::wstring TranslateUserModeProviders(const std::wstring& providers)
{
	std::wstring translatedProviders;
	translatedProviders.reserve(providers.size());
	for (const auto& provider : split(providers, '+'))
	{
		if (provider.empty())
		{
			continue;
		}
		translatedProviders += '+';
		if (provider.front() != '*')
		{
			translatedProviders += provider;
			continue;
		}
		// if the provider name begins with a *, it follows the EventSource / TraceLogging
		// convention and must be translated to a GUID.
		// remove the leading '*' before calling the function
		translatedProviders += TranslateTraceLoggingProvider(provider.substr(1));
	}

	return translatedProviders;
}

CUIforETWDlg::CUIforETWDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CUIforETWDlg::IDD, pParent)
	, monitorThread_(this)
{
	pMainWindow = this;
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	TransferSettings(false);
}

CUIforETWDlg::~CUIforETWDlg()
{
	// Shut down key logging. Ideally this would be managed by an object so
	// that CUIforETWDlg didn't have to do this, as the other threads are.
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
	DDX_Control(pDX, IDC_CLRTRACING, btCLRTracing_ );
	DDX_Control(pDX, IDC_SHOWCOMMANDS, btShowCommands_);

	DDX_Control(pDX, IDC_INPUTTRACING, btInputTracing_);
	DDX_Control(pDX, IDC_INPUTTRACING_LABEL, btInputTracingLabel_);
	DDX_Control(pDX, IDC_TRACINGMODE, btTracingMode_);
	DDX_Control(pDX, IDC_TRACELIST, btTraces_);
	DDX_Control(pDX, IDC_TRACENOTES, btTraceNotes_);
	DDX_Control(pDX, IDC_OUTPUT, btOutput_);
	DDX_Control(pDX, IDC_TRACENAMEEDIT, btTraceNameEdit_);

	CDialog::DoDataExchange(pDX);
}

// Hook up functions to messages from buttons, menus, etc.
BEGIN_MESSAGE_MAP(CUIforETWDlg, CDialog)
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
	ON_MESSAGE(WM_UPDATETRACELIST, &CUIforETWDlg::UpdateTraceListHandler)
	ON_MESSAGE(WM_NEWVERSIONAVAILABLE, &CUIforETWDlg::NewVersionAvailable)
	ON_LBN_DBLCLK(IDC_TRACELIST, &CUIforETWDlg::OnLbnDblclkTracelist)
	ON_WM_GETMINMAXINFO()
	ON_WM_SIZE()
	ON_LBN_SELCHANGE(IDC_TRACELIST, &CUIforETWDlg::OnLbnSelchangeTracelist)
	ON_BN_CLICKED(IDC_ABOUT, &CUIforETWDlg::OnBnClickedAbout)
	ON_BN_CLICKED(IDC_SAVETRACEBUFFERS, &CUIforETWDlg::OnBnClickedSavetracebuffers)
	ON_MESSAGE(WM_HOTKEY, &CUIforETWDlg::OnHotKey)
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
	ON_BN_CLICKED( IDC_CLRTRACING, &CUIforETWDlg::OnBnClickedClrtracing )
END_MESSAGE_MAP()


void CUIforETWDlg::SetSymbolPath()
{
	// Make sure that the symbol paths are set.

	if (bManageSymbolPath_ || GetEnvironmentVariableString(L"_NT_SYMBOL_PATH").empty())
	{
		bManageSymbolPath_ = true;
		std::string symbolPath = "SRV*" + systemDrive_ + "symbols*https://msdl.microsoft.com/download/symbols";
		if (bChromeDeveloper_)
			symbolPath = "SRV*" + systemDrive_ + "symbols*https://msdl.microsoft.com/download/symbols;SRV*" + systemDrive_ + "symbols*https://chromium-browser-symsrv.commondatastorage.googleapis.com";
		(void)_putenv(("_NT_SYMBOL_PATH=" + symbolPath).c_str());
		outputPrintf(L"\nSetting _NT_SYMBOL_PATH=%s (Microsoft%s). "
			L"Set _NT_SYMBOL_PATH yourself or toggle 'Chrome developer' if you want different defaults.\n",
			AnsiToUnicode(symbolPath).c_str(), bChromeDeveloper_ ? L" plus Chrome" : L"");
	}
	const std::wstring symCachePath = GetEnvironmentVariableString(L"_NT_SYMCACHE_PATH");
	if (symCachePath.empty())
		(void)_putenv(("_NT_SYMCACHE_PATH=" + systemDrive_ + "symcache").c_str());
}

void CUIforETWDlg::CheckSymbolDLLs()
{
	// Starting with the 10.0.14393.33 (Windows 10 Anniversary) edition of
	// WPT the latest version of symsrv.dll *must* be used. So, old copies
	// in the WPT directory have to be deleted. Failing to do this will cause
	// heap corruption and other crashes because WPT expects a thread-safe
	// symsrv.dll, and the old versions aren't. Also, dbghelp.dll is rarely
	// needed so it is deleted at the same time.
	// Previously the old copies were used to handle these issues:
	// https://randomascii.wordpress.com/2012/10/04/xperf-symbol-loading-pitfalls/
	const wchar_t* fileNames[] =
	{
		L"dbghelp.dll",
		L"symsrv.dll",
	};

	std::vector<std::wstring> filePaths;
	for (size_t i = 0; i < ARRAYSIZE(fileNames); ++i)
	{
		std::wstring filepath = wpt10Dir_ + fileNames[i];
		if (PathFileExists(filepath.c_str()))
			filePaths.push_back(std::move(filepath));
	}

	if (!filePaths.empty())
		DeleteFiles(*this, filePaths);

#if defined(_WIN64)
	const std::wstring symsrv_path = CanonicalizePath(wpt10Dir_ + L"..\\Debuggers\\x64\\symsrv.dll");
#else
	const std::wstring symsrv_path = CanonicalizePath(wpt10Dir_ + L"..\\Debuggers\\x86\\symsrv.dll");
#endif
	if (!PathFileExists(symsrv_path.c_str()))
	{
		AfxMessageBox((L"symsrv.dll (" + symsrv_path +
			L") not found. Be sure to install the Windows 10 Anniversary Edition Debuggers "
			L"or else symbol servers will not work.").c_str());
	}

	// Previous versions of symsrv.dll may not be multithreading safe and therefore can't
	// be used with the latest WPA.
	const int64_t requiredSymsrvVersion = (10llu << 48) + 0 + (14321llu << 16) + (1024llu << 0);
	auto symsrvVersion = GetFileVersion(symsrv_path);
	if (symsrvVersion < requiredSymsrvVersion)
	{
		AfxMessageBox((L"symsrv.dll (" + symsrv_path +
			L") is not the required version (10.0.14321.1024 or higher). "
			L"Be sure to install the Windows 10 Anniversary Edition Debuggers "
			L"or else symbol servers will not work.").c_str());
	}
}

BOOL CUIforETWDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

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
		btStartTracing_.SetWindowTextW(L"Start &Tracing");
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

	if (IsWindowsXPOrLesser())
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
	wpt81Dir_ = windowsKitsDir_ + L"8.1\\Windows Performance Toolkit\\";
	wpt10Dir_ = windowsKitsDir_ + L"10\\Windows Performance Toolkit\\";

	auto xperfVersion = GetFileVersion(GetXperfPath());
	const int64_t requiredXperfVersion = (10llu << 48) + 0 + (10586llu << 16) + (15llu << 0);
	// Windows 10 Anniversary Edition version (August 2016) - requires Windows 8 or higher.
	const int64_t preferredXperfVersion = (10llu << 48) + 0 + (14393llu << 16) + (33llu << 0);

	wchar_t systemDir[MAX_PATH];
	systemDir[0] = 0;
	GetSystemDirectory(systemDir, ARRAYSIZE(systemDir));
	std::wstring msiExecPath = systemDir + std::wstring(L"\\msiexec.exe");

	if (Is64BitWindows() && PathFileExists(msiExecPath.c_str()))
	{
		// The installers are available as part of etwpackage.zip on
		// https://github.com/google/UIforETW/releases
		if (IsWindowsSevenOrLesser())
		{
			// The newest (Anniversary Edition or beyond) WPT doesn't work on Windows 7.
			// Install the older 64-bit WPT 10 if needed and if available.
			if (xperfVersion < requiredXperfVersion)
			{
				const std::wstring installPathOld10 = CanonicalizePath(GetExeDir() + L"..\\third_party\\oldwpt10\\WPTx64-x86_en-us.msi");
				if (PathFileExists(installPathOld10.c_str()))
				{
					ChildProcess child(msiExecPath);
					std::wstring args = L" /i \"" + installPathOld10 + L"\"";
					child.Run(true, L"msiexec.exe" + args);
					DWORD installResult10 = child.GetExitCode();
					if (!installResult10)
					{
						outputPrintf(L"WPT version 10.0.10586 was installed.\n");
					}
					else
					{
						outputPrintf(L"Failure code %u while installing WPT 10.\n", installResult10);
					}
				}
			}
		}
		else
		{
			// Install 64-bit WPT 10 if needed and if available.
			if (xperfVersion < preferredXperfVersion)
			{
				const std::wstring installPath10 = CanonicalizePath(GetExeDir() + L"..\\third_party\\wpt10\\WPTx64-x86_en-us.msi");
				if (PathFileExists(installPath10.c_str()))
				{
					ChildProcess child(msiExecPath);
					std::wstring args = L" /i \"" + installPath10 + L"\"";
					child.Run(true, L"msiexec.exe" + args);
					DWORD installResult10 = child.GetExitCode();
					if (!installResult10)
					{
						outputPrintf(L"WPT version 10.0.14393 was installed.\n");
					}
					else
					{
						outputPrintf(L"Failure code %u while installing WPT 10.\n", installResult10);
					}
				}
			}
			xperfVersion = GetFileVersion(GetXperfPath());
		}
	}

	// Because of bugs in the initial WPT 10 we require the TH2 version.
	if (xperfVersion < requiredXperfVersion)
	{
		if (Is64BitWindows())
		{
			if (xperfVersion)
				AfxMessageBox((GetXperfPath() + L" must be version 10.0.10586.15 or higher. If you run UIforETW from etwpackage.zip\n"
					L"from https://github.com/google/UIforETW/releases\n"
					L"then WPT will be automatically installed. Exiting.").c_str());
			else
				AfxMessageBox((GetXperfPath() + L" does not exist. If you run UIforETW from etwpackage.zip\n"
					L"from https://github.com/google/UIforETW/releases\n"
					L"then WPT will be automatically installed. Exiting.").c_str());
		}
		else
		{ 
			if (xperfVersion)
				AfxMessageBox((GetXperfPath() + L" must be version 10.0.10586.15 or higher. You'll need to find the installer in the Windows "
					L"Windows 10 SDK or you can xcopy install it. Exiting.").c_str());
			else
				AfxMessageBox((GetXperfPath() + L" does not exist. You'll need to find the installer in the Windows "
				L"Windows 10 SDK or you can xcopy install it. Exiting.").c_str());
		}
		exit(10);
	}

	if (xperfVersion >= preferredXperfVersion)
	{
		if (IsWindowsSevenOrLesser())
		{
			AfxMessageBox(L"The installed version of Windows Performance Toolkit is not compatible with Windows 7. "
										L"Please uninstall it and run UIforETW again.");
		}
		else
		{
			CheckSymbolDLLs();
		}
	}

	if (!PathFileExists((wpt81Dir_ + L"xperf.exe").c_str()))
		wpt81Dir_ = L"";

	if (!wpt81Dir_.empty())
		wpa81Path_ = wpt81Dir_ + L"wpa.exe";
	gpuViewPath_ = wpt10Dir_ + L"gpuview\\gpuview.exe";
	wpa10Path_ = wpt10Dir_ + L"wpa.exe";

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

	if (IsWindowsSevenOrLesser())
	{
		bCompress_ = false; // ETW trace compression requires Windows 8.0
		SmartEnableWindow(btCompress_.m_hWnd, false);
	}

	CheckDlgButton(IDC_COMPRESSTRACE, bCompress_);
	CheckDlgButton(IDC_CONTEXTSWITCHCALLSTACKS, bCswitchStacks_);
	CheckDlgButton(IDC_CPUSAMPLINGCALLSTACKS, bSampledStacks_);
	CheckDlgButton(IDC_FASTSAMPLING, bFastSampling_);
	CheckDlgButton(IDC_GPUTRACING, bGPUTracing_);
	CheckDlgButton(IDC_CLRTRACING, bCLRTracing_);
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

	// Fill in the traces list.
	UpdateTraceList();
	const int numTraces = btTraces_.GetCount();
	if (numTraces > 0)
	{
		// Select the most recent trace.
		btTraces_.SetCurSel(numTraces - 1);
		UpdateNotesState();
	}

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
		toolTip_.AddTool(&btCLRTracing_, L"Check this to record CLR call stacks" );
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

	CheckProcesses();

	if (bVersionChecks_)
		versionCheckerThread_.StartVersionCheckerThread(this);

	return TRUE; // return TRUE unless you set the focus to a control
}

std::wstring CUIforETWDlg::wpaDefaultPath() const
{
	if (PathFileExists(wpa10Path_.c_str()))
		return wpa10Path_;
	return wpa81Path_;
}

std::wstring CUIforETWDlg::GetDirectory(PCWSTR env, const std::wstring& defaultDir)
{
	// Get a directory (from an environment variable, if set) and make sure it exists.
	std::wstring result = defaultDir;
	const std::wstring traceDir = GetEnvironmentVariableString(env);
	if (!traceDir.empty())
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
	outputPrintf(L"\n");
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
	const std::wstring temp = GetEnvironmentVariableString(L"temp");
	if (temp.empty())
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
		if (pass)
		{
			args += L" /mf:\"" + dllDest + L"\" /rf:\"" + dllDest + L"\"";
		}
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
	SmartEnableWindow(btCLRTracing_.m_hWnd, !bIsTracing_ );
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
		CDialog::OnSysCommand(nID, lParam);
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
		CDialog::OnPaint();
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
	const std::wstring username = GetEnvironmentVariableString(L"USERNAME");
	wchar_t fileName[MAX_PATH];
	// Hilarious /analyze warning on this line from bug in _strtime_s annotation!
	// warning C6054: String 'time' might not be zero-terminated.
#pragma warning(suppress : 6054)
	if (3 == sscanf_s(time, "%d:%d:%d", &hour, &min, &sec) &&
		3 == sscanf_s(date, "%d/%d/%d", &month, &day, &year))
	{
		// The filenames are chosen to sort by date, with username as the LSB.
		swprintf_s(fileName, L"%04d-%02d-%02d_%02d-%02d-%02d_%s", year + 2000, month, day, hour, min, sec, username.c_str());
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

void CUIforETWDlg::StartEventThreads()
{
	// Start the input logging thread with the current settings.
	SetKeyloggingState(InputTracing_);

	// Send occasional timer messages so that we can check for tracing to file
	// that has run "too long". Checking every thirty seconds should be fine.
	SetTimer(kTimerID, 30000, nullptr);

	CPUFrequencyMonitor_.StartThreads();
	PowerMonitor_.SetPerfCounters(perfCounters_);
	PowerMonitor_.StartThreads();
	workingSetThread_.StartThreads();
}

void CUIforETWDlg::StopEventThreads()
{
	// Stop the input logging thread.
	SetKeyloggingState(kKeyLoggerOff);

	KillTimer(kTimerID);

	CPUFrequencyMonitor_.StopThreads();
	PowerMonitor_.StopThreads();
	workingSetThread_.StopThreads();
}

void CUIforETWDlg::OnBnClickedStarttracing()
{
	RegisterProviders();
	StartEventThreads();
	if (tracingMode_ == kTracingToMemory)
		outputPrintf(L"\nStarting tracing to in-memory circular buffers...\n");
	else if (tracingMode_ == kTracingToFile)
		outputPrintf(L"\nStarting tracing to disk...\n");
	else if (tracingMode_ == kHeapTracingToFile)
		outputPrintf(L"\nStarting heap tracing to disk of %s...\n", heapTracingExes_.c_str());
	else
		UIETWASSERT(0);

	{
		// Force any existing sessions to stop. This makes starting tracing much more robust.
		// Never show the commands executing, and never print the exit code.
		ChildProcess child(GetXperfPath(), false);
		child.Run(false, L"xperf.exe -stop UIforETWHeapSession -stop UIforETWSession -stop " + GetKernelLogger());
		// Swallow all of the output so that the normal failures will be silent.
		child.GetOutput();
	}

	std::wstring kernelProviders = L" Latency+POWER+DISPATCHER+DISK_IO_INIT+FILE_IO+FILE_IO_INIT+VIRT_ALLOC+MEMINFO";
	if (!extraKernelFlags_.empty())
		kernelProviders += L"+" + extraKernelFlags_;
	std::wstring kernelStackWalk;
	// Record CPU sampling call stacks, from the PROFILE provider
	if (bSampledStacks_)
		kernelStackWalk += L"+Profile";
	// Record context-switch (switch in) and readying-thread (SetEvent, etc.)
	// call stacks from DISPATCHER provider.
	if (bCswitchStacks_)
		kernelStackWalk += L"+CSwitch+ReadyThread";
	// Record VirtualAlloc call stacks from the VIRT_ALLOC provider. Also
	// record VirtualFree to allow investigation of memory leaks, even though
	// WPA fails to display these stacks.
	if (bVirtualAllocStacks_ || tracingMode_ == kHeapTracingToFile)
		kernelStackWalk += L"+VirtualAlloc+VirtualFree";
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

	// The Windows 10 ReleaseUserCrit, ExclusiveUserCrit, and SharedUserCrit events generate
	// 75% of the events for this provider - 33,000/s in one test. They account for
	// more than 75% of the space used, according to System Configuration-> Trace
	// Statistics. That table also shows their Keyword (aka flags) which are
	// 0x0200000010000000. By specifying a flag of ~0x0200000010000000 we can
	// reduce the fill-rate of the user buffers by a factor of four, allowing much
	// longer time periods to be captured with lower overhead.
	// This avoids the problem where the user buffers wrap around so quickly that
	// their time period doesn't overlap that of the kernel buffers. Specifying
	// this flag is equivalent to quadrupling the size of the user buffers!
	// This should also make the UI Delays and window-in-focus graphs more
	// reliable, by not having them lose messages so frequently, although it is not
	// clear that it actually helps.
	const uint64_t kCritFlags = 0x0200000010000000;
	std::wstring userProviders = stringPrintf(L"Microsoft-Windows-Win32k:0x%llx", ~kCritFlags);
	if (IsWindowsVistaOrLesser())
		userProviders = L"Microsoft-Windows-LUA"; // Because Microsoft-Windows-Win32k doesn't work on Vista.
	userProviders += L"+Multi-MAIN+Multi-FrameRate+Multi-Input+Multi-Worker";
	// Suggested in https://github.com/google/UIforETW/issues/80. The data shows up in WPA in
	// Memory-> Virtual Memory Snapshots. On windows 8.1 and above this makes the working set
	// scanning in UIforETW unnecessary.
	userProviders += L"+Microsoft-Windows-Kernel-Memory:0xE0";

	if (!extraUserProviders_.empty())
	{
		try
		{
			userProviders += TranslateUserModeProviders(extraUserProviders_);
		}
		catch (const std::exception& e)
		{
			outputPrintf(L"Check the extra user providers; failed to translate them from the TraceLogging name to a GUID.\n%hs\n", e.what());
			StopEventThreads();
			return;
		}
	}

	// DWM providers can be helpful also. Uncomment to enable.
	//userProviders += L"+Microsoft-Windows-Dwm-Dwm";

	// Monitoring of timer frequency changes from timeBeginPeriod and
	// NtSetTimerResolution. Windows 7 and above.
	// Like most providers the information provide by this one is undocumented and
	// therefore only easily usable by those who created it. However, a bit of
	// exploration will find useful information. The timer changes are contained
	// in events such as SystemTimeResolutionChange and SystemTimeResolutionKernelChange
	// for changes by normal processes and the kernel.
	// The "Randomascii System Time Resolution" preset for the Generic Events is set up
	// with a filter to show just these events. To configure this filtering I used the
	// View Editor, Advanced, and the syntax that is lightly documented here:
	// https://msdn.microsoft.com/en-us/windows/hardware/commercialize/test/wpt/wpa-query-syntax
	// Look at SystemTimeResolutionChange and SystemTimeResolutionKernelChange, and in
	// particular at the RequestedResolution field (units are 0.1 microseconds, so 
	// 0x2710 == 10,000 = 1 ms).
	userProviders += L"+Microsoft-Windows-Kernel-Power";

	// If the Chrome providers were successfully registered and if the user has requested tracing
	// some of Chrome's categories (keywords/flags) then add chrome:flags to the list of user
	// providers to monitor. See https://codereview.chromium.org/1176243016 for details.
	if (useChromeProviders_)
		userProviders += stringPrintf(L"+chrome:0x%llx", (0x8000000000000000 | chromeKeywords_));

	if (bGPUTracing_)
	{
		// Apparently we need a different provider for graphics profiling
		// on Windows 8 and above.
		if (IsWindows8OrGreater())
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

	if (bCLRTracing_)
	{
		// CLR runtime provider
		// https://msdn.microsoft.com/en-us/library/ff357718(v=vs.100).aspx
		userProviders += L"+e13c0d23-ccbc-4e12-931b-d9cc2eee27e4:0x1CCBD:0x5";
        
		// note: this seems to be an updated version of
		// userProviders += L"+ClrAll:0x98:5";
		// which results in Invalid flags. (0x3ec) when I run it
		// https://msdn.microsoft.com/en-us/library/windows/hardware/hh448186.aspx
	}

	// Increase the user buffer sizes when doing graphics tracing or Chrome tracing.
	const int numUserBuffers = BufferCountBoost(bGPUTracing_ ? 200 : 100) + BufferCountBoost(useChromeProviders_ ? 100 : 0);
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
	std::wstring heapArgs = L" -start UIforETWHeapSession -heap -Pids 0" + heapStackWalk + heapBuffers + heapFile;

	bPreTraceRecorded_ = false;

	DWORD exitCode = 0;
	bool started = true;
	if (tracingMode_ == kTracingToFile && bChromeDeveloper_)
	{
		// Implement the fix to https://github.com/google/UIforETW/issues/97
		// Grab an initial trace that will contain imageID, fileversion, etc., so that ETW
		// traces that cover a Chrome upgrade will get before and after information. This
		// *could* be applicable to non-Chrome developers, but it is unlikely, so rather than
		// creating yet-another-obscure-setting I just piggyback off of the bChromeDeveloper_
		// flag, in order to keep things simple.
		const std::wstring imageIDCommands[] = {
			// Start tracing with minimal flags
			L"xperf.exe -start " + GetKernelLogger() + L" -on PROC_THREAD+LOADER -f \"" + GetTempImageTraceFile() + L"\"",
			// Immediately stop tracing
			L"xperf.exe -stop " + GetKernelLogger(),
			// Merge just the image ID information over to GetFinalImageTraceFile()
			L"xperf.exe -merge \"" + GetTempImageTraceFile() + L"\" \"" + GetFinalImageTraceFile() + L"\" -injectonly",
		};

		outputPrintf(L"Recording pre-trace image data...\n");
		for (auto& command : imageIDCommands)
		{
			ChildProcess child(GetXperfPath());

			started = child.Run(bShowCommands_, command);
			(void)child.GetOutput(); // Swallow output - failures and status are to be ignored.
			exitCode = child.GetExitCode();
			if (!started || exitCode)
				break;
		}
		if (started && !exitCode)
			bPreTraceRecorded_ = true;
	}

	{
		ChildProcess child(GetXperfPath());

		if (tracingMode_ == kHeapTracingToFile)
			startupCommand_ = L"xperf.exe" + kernelArgs + userArgs + heapArgs;
		else
			startupCommand_ = L"xperf.exe" + kernelArgs + userArgs;
		started = child.Run(bShowCommands_, startupCommand_);

		exitCode = child.GetExitCode();
		if (exitCode || !started)
		{
			started = false;
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
				if (!extraUserProviders_.empty())
				{
					outputPrintf(L"Check your extra user providers in the settings dialog for typos.\n");
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
	if (started)
	{
		// Run -capturestate on the user-mode loggers, for reliable captures.
		// If this step is skipped then GPU usage data will not be recorded on
		// Windows 8. Contrary to the xperf documentation this is needed for file
		// based recording as well as when -buffering is used.
		ChildProcess child(GetXperfPath());
		std::wstring captureArgs = L" -capturestate UIforETWSession " + userProviders;
		child.Run(bShowCommands_, L"xperf.exe" + captureArgs);
	}
	else
	{
		// No sense leaving these running if tracing failed to start.
		StopEventThreads();
	}

	// Set this whether starting succeeds or not, to allow forced-stopping.
	bIsTracing_ = true;
	UpdateEnabling();

	traceStartTime_ = GetTickCount64();
}

// This should probably be named MaybeStopTracingAndMaybeRecord because it
// doesn't always do either. The behavior depends on the type of tracing and
// on the bSaveTrace flag. Circular buffer tracing continues after this call.
void CUIforETWDlg::StopTracingAndMaybeRecord(bool bSaveTrace)
{
	std::wstring traceFilename = GenerateResultFilename();
	if (bSaveTrace)
		outputPrintf(L"\nSaving trace to disk...\n");
	else
		outputPrintf(L"\nStopping tracing...\n");

	// Rename Amcache.hve to work around a merge hang that can last up to six
	// minutes. This was seen on two Windows 7 machines.
	// https://randomascii.wordpress.com/2015/03/02/profiling-the-profiler-working-around-a-six-minute-xperf-hang/
	const std::wstring compatFile = windowsDir_ + L"AppCompat\\Programs\\Amcache.hve";
	const std::wstring compatFileTemp = windowsDir_ + L"AppCompat\\Programs\\Amcache_temp.hve";
	// Delete any previously existing Amcache_temp.hve file that might have
	// been left behind by a previous failed tracing attempt.
	// Note that this has to be done before the -flush step -- it makes both it
	// and the -merge step painfully slow.
	DeleteFile(compatFileTemp.c_str());
	BOOL moveSuccess = MoveFile(compatFile.c_str(), compatFileTemp.c_str());
	// Don't print this message - it just cause confusion, especially since the renaming fails
	// on most machines without it mattering.
	//if (bShowCommands_ && !moveSuccess)
	//	outputPrintf(L"Failed to rename %s to %s\n", compatFile.c_str(), compatFileTemp.c_str());

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
				child.Run(bShowCommands_, L"xperf.exe -stop UIforETWHeapSession -stop UIforETWSession -stop " + GetKernelLogger());
			}
			else
			{
				ETWMark("Tracing type was tracing to file.");
				child.Run(bShowCommands_, L"xperf.exe -stop UIforETWSession -stop " + GetKernelLogger());
			}
			// Stop the event monitoring threads now that tracing is stopped.
			StopEventThreads();
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
			if (bPreTraceRecorded_)
				args += L" \"" + GetFinalImageTraceFile() + L"\"";
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
			LaunchTraceViewer(traceFilename, wpaDefaultPath());
		// Record the name so that it gets selected.
		lastTraceFilename_ = CrackFilePart(traceFilename);

		if (saveTime > 100.0 && tracingMode_ == kTracingToMemory)
		{
			// Some machines (one so far?) can take 5-10 minutes to do the trace
			// saving stage.
			outputPrintf(L"Saving the trace took %1.1f s, which is unusually long. Consider "
				L"changing the trace mode to \"Tracing to file\" to see if this works better. "
				L"Alternately, try using metatrace.bat to record a trace of UIforETW saving "
				L"the trace, and share your results on "
				L"https://groups.google.com/forum/#!forum/uiforetw.\n", saveTime);
		}
		if (mergeTime > 100.0)
		{
			// See the Amcache.hve comments above for details or to instrument.
			if (moveSuccess)
			{
				outputPrintf(L"Merging the trace took %1.1fs, which is unusually long. This is surprising "
					L"because renaming of amcache.hve to avoid this worked. Please try using metatrace.bat "
					L"to record a trace of UIforETW saving the trace, and share this on "
					L"https://groups.google.com/forum/#!forum/uiforetw\n", mergeTime);
			}
			else
			{
				outputPrintf(L"Merging the trace took %1.1fs, which is unusually long. This might be "
					L"because renaming of amcache.hve failed. Please try using metatrace.bat "
					L"to record a trace of UIforETW saving the trace, and share this on "
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
	if (IsWindowsTenOrGreater())
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

void CUIforETWDlg::OnBnClickedClrtracing()
{
	bCLRTracing_ = !bCLRTracing_;
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
	if (bIsTracing_)
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
	auto tempTraces = GetFileList(GetTraceDir() + L"*.etl");
	auto tempZips = GetFileList(GetTraceDir() + L"*.zip");
	// Why can't I use += to concatenate these?
	tempTraces.insert(tempTraces.end(), tempZips.begin(), tempZips.end());
	std::sort(tempTraces.begin(), tempTraces.end());
	// Function to stop the temporary traces from showing up.
	auto ifInvalid = [](const std::wstring& name) { return name == L"UIForETWkernel.etl" || name == L"UIForETWuser.etl" || name == L"UIForETWheap.etl"; };
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

LRESULT CUIforETWDlg::NewVersionAvailable(WPARAM wParam, LPARAM /*lParam*/)
{
	PackagedFloatVersion newVersion;
	newVersion.u = static_cast<unsigned>(wParam);
	std::wstring message = stringPrintf(L"A newer version of UIforETW is available from https://github.com/google/UIforETW/releases/\n"
		"The version you have installed is %1.2f and the new version is %1.2f.\n"
		"Would you like to download the new version?", kCurrentVersion, newVersion.f);

	int result = AfxMessageBox(message.c_str(), MB_YESNO);
	if (result == IDYES)
		ShellExecute(*this, L"open", L"https://github.com/google/UIforETW/releases/", 0, L".", SW_SHOWNORMAL);
	if (result == IDNO)
		AfxMessageBox(L"Version checking can be turned off in the settings dialog.");

	return 0;
}


void CUIforETWDlg::OnLbnDblclkTracelist()
{
	int selIndex = btTraces_.GetCurSel();
	// This check shouldn't be necessary, but who knows?
	if (selIndex < 0 || selIndex >= (int)traces_.size())
		return;
	std::wstring tracename = GetTraceDir() + traces_[selIndex] + L".etl";
	LaunchTraceViewer(tracename, wpaDefaultPath());
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
	if ((nType == SIZE_RESTORED || nType == SIZE_MAXIMIZED) && initialWidth_)
	{
		FinishTraceRename();
		// Calculate xDelta and yDelta -- the change in the window's size.
		CRect windowRect;
		GetWindowRect(&windowRect);
		int xDelta = windowRect.Width() - lastWidth_;
		lastWidth_ += xDelta;
		int yDelta = windowRect.Height() - lastHeight_;
		lastHeight_ += yDelta;

		const UINT flags = SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE;

		// Resize the output window, trace list, and notes control.

		// Output window sizes horizontally only.
		CRect outputRect;
		btOutput_.GetWindowRect(&outputRect);
		btOutput_.SetWindowPos(nullptr, 0, 0, outputRect.Width() + xDelta, outputRect.Height(), flags);

		// Trace list sizes vertically only.
		CRect listRect;
		btTraces_.GetWindowRect(&listRect);
		btTraces_.SetWindowPos(nullptr, 0, 0, listRect.Width(), listRect.Height() + yDelta, flags);
		int curSel = btTraces_.GetCurSel();
		if (curSel != LB_ERR)
		{
			// Make the selected line visible.
			btTraces_.SetTopIndex(curSel);
		}

		// Notes control sizes horizontally and vertically.
		CRect editRect;
		btTraceNotes_.GetWindowRect(&editRect);
		btTraceNotes_.SetWindowPos(nullptr, 0, 0, editRect.Width() + xDelta, editRect.Height() + yDelta, flags);

		// Option buttons move horizontally.
		if (xDelta)
		{
			MoveControl(this, btCompress_, xDelta, 0);
			MoveControl(this, btCswitchStacks_, xDelta, 0);
			MoveControl(this, btSampledStacks_, xDelta, 0);
			MoveControl(this, btFastSampling_, xDelta, 0);
			MoveControl(this, btGPUTracing_, xDelta, 0);
			MoveControl(this, btCLRTracing_, xDelta, 0 );
			MoveControl(this, btInputTracingLabel_, xDelta, 0);
			MoveControl(this, btInputTracing_, xDelta, 0);
			MoveControl(this, btTracingMode_, xDelta, 0);
			MoveControl(this, btShowCommands_, xDelta, 0);
		}
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
		std::wstring newTraceNotes = LoadFileAsText(traceNoteFilename_);
		if (newTraceNotes != traceNotes_)
		{
			// Only call SetDlgItemText if something has actually changed, to
			// avoid resetting state such as the cursor position.
			traceNotes_ = newTraceNotes;
			SetDlgItemText(IDC_TRACENOTES, traceNotes_.c_str());
		}
		auto trace_size = GetFileSize(GetTraceDir() + traceName + L".etl");
		wchar_t buffer[500];
		// Yep, base-10 MB. The numbers will not match what Windows File Explorer
		// shows, but that's because it is wrong. Using base-10 means you can
		// trivially change to bytes, or kB, or estimate how many traces will fit
		// in a (base-10) 640 GB drive.
		// https://randomascii.wordpress.com/2016/02/13/base-ten-for-almost-everything/
		swprintf_s(buffer, L"Trace size: %.1f MB", trace_size / 1e6);
		SetDlgItemText(IDC_TRACESIZE, buffer);
	}
	else
	{
		SmartEnableWindow(btTraceNotes_.m_hWnd, false);
		SetDlgItemText(IDC_TRACENOTES, L"");
		SetDlgItemText(IDC_TRACESIZE, L"Trace size:");
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
		// The magic hot key can be used to start or stop tracing. Doing both
		// of these without having to change away from the app being profiled
		// has been shown to be useful.
		if (bIsTracing_)
			StopTracingAndMaybeRecord(true);
		else
			OnBnClickedStarttracing();
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
		if (tracingFlags)
			outputPrintf(L"\"TracingFlags\" in \"HKEY_LOCAL_MACHINE\\%s\" set to %lu.\n", targetKey.c_str(), tracingFlags);
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
	CSettings dlgSettings(nullptr, GetExeDir(), wpt81Dir_, wpt10Dir_);
	dlgSettings.heapTracingExes_ = heapTracingExes_;
	dlgSettings.WSMonitoredProcesses_ = WSMonitoredProcesses_;
	dlgSettings.bExpensiveWSMonitoring_ = bExpensiveWSMonitoring_;
	dlgSettings.extraKernelFlags_ = extraKernelFlags_;
	dlgSettings.extraKernelStacks_ = extraKernelStacks_;
	dlgSettings.extraUserProviders_ = extraUserProviders_;
	dlgSettings.perfCounters_ = perfCounters_;
	dlgSettings.bUseOtherKernelLogger_ = bUseOtherKernelLogger_;
	dlgSettings.bChromeDeveloper_ = bChromeDeveloper_;
	dlgSettings.bAutoViewTraces_ = bAutoViewTraces_;
	dlgSettings.bHeapStacks_ = bHeapStacks_;
	dlgSettings.bVirtualAllocStacks_ = bVirtualAllocStacks_;
	dlgSettings.bVersionChecks_ = bVersionChecks_;
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
		bUseOtherKernelLogger_ = dlgSettings.bUseOtherKernelLogger_;
		WSMonitoredProcesses_ = dlgSettings.WSMonitoredProcesses_;
		bExpensiveWSMonitoring_ = dlgSettings.bExpensiveWSMonitoring_;
		extraKernelFlags_ = dlgSettings.extraKernelFlags_;
		extraKernelStacks_ = dlgSettings.extraKernelStacks_;
		extraUserProviders_ = dlgSettings.extraUserProviders_;
		perfCounters_ = dlgSettings.perfCounters_;
		workingSetThread_.SetProcessFilter(WSMonitoredProcesses_, bExpensiveWSMonitoring_);

		bAutoViewTraces_ = dlgSettings.bAutoViewTraces_;
		bHeapStacks_ = dlgSettings.bHeapStacks_;
		bVirtualAllocStacks_ = dlgSettings.bVirtualAllocStacks_;
		bVersionChecks_ = dlgSettings.bVersionChecks_;
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
			// WPT 10 must be installed and 8.1 may be.
			if (wpt81Dir_.empty())
			{
				// If WPT 8.1 is not installed then disable it.
				pContextMenu->SetDefaultItem(ID_TRACES_OPENTRACEIN10WPA);
				pContextMenu->EnableMenuItem(ID_TRACES_OPENTRACEINWPA, MF_BYCOMMAND | MF_GRAYED);
			}
			else
			{
				// Make WPT 10 the default.
				pContextMenu->SetDefaultItem(ID_TRACES_OPENTRACEIN10WPA);
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
				ID_SCRIPTS_CREATEFLAMEGRAPH,
			};

			for (const auto& id : disableList)
				pContextMenu->EnableMenuItem(id, MF_BYCOMMAND | MF_GRAYED);
		}

		if (IsWindowsSevenOrLesser())
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
				LaunchTraceViewer(tracePath, wpa81Path_);
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
			case ID_SCRIPTS_CREATEFLAMEGRAPH:
				CreateFlameGraph(tracePath);
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
		// The default viewer is WPA 10
		LaunchTraceViewer(tracePath, wpa10Path_);
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


void CUIforETWDlg::CreateFlameGraph(const std::wstring& traceFilename)
{
	outputPrintf(L"\nCreating CPU Usage (Sampled) flame graph of busiest process in %s "
				 L"(requires python, perl and flamegraph.pl). UIforETW will hang while "
				 L"this is calculated...\n", traceFilename.c_str());
	std::wstring pythonPath = FindPython();
	if (!pythonPath.empty())
	{
		ChildProcess child(pythonPath);
		std::wstring args = L" -u \"" + GetExeDir() + L"xperf_to_collapsedstacks.py\" \"" + traceFilename + L"\"";
		child.Run(bShowCommands_, GetFilePart(pythonPath) + args);
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
