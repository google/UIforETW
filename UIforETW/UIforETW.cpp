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
#include "Utility.h"
#include <stdexcept>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


namespace {

struct HMODULEhelper
{
	HMODULE hModule;

	HMODULEhelper(_In_z_ PCSTR const moduleName)
	{
		HMODULE module_temp;
		const BOOL result = GetModuleHandleExA(0, moduleName, &module_temp);
		if (result == 0)
		{
			//Grr. Exceptions don't like wchar_t strings!
			std::string exceptionStr("Failed to load module: " +
									  std::string(moduleName));
			throw std::runtime_error( exceptionStr );
		}
		hModule = module_temp;
	}

	~HMODULEhelper( )
	{
		ATLVERIFY( FreeLibrary(hModule) );
	}

	HMODULEhelper(const HMODULEhelper&) = delete;
	HMODULEhelper(const HMODULEhelper&&) = delete;
	HMODULEhelper& operator=(const HMODULEhelper&) = delete;
	HMODULEhelper& operator=(const HMODULEhelper&&) = delete;
};

typedef BOOL(WINAPI* SetProcessMitigationPolicy_t)(\
	_In_ _Const_ PROCESS_MITIGATION_POLICY MitigationPolicy,
	_In_reads_bytes_( dwLength ) _Const_ PVOID lpBuffer,
	_In_ _Const_ SIZE_T dwLength);

void enableTerminateOnHeapCorruption()
{
	const BOOL result =
		HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0u);
	if ( result != 0 )
	{
		ATLTRACE(L"Enabled HeapEnableTerminationOnCorruption!!\r\n");
		return;
	}
	ATLTRACE(L"Failed to enable HeapEnableTerminationOnCorruption!!\r\n");
}

void enableASLRMitigation(_In_ SetProcessMitigationPolicy_t SetProcessMitigationPolicy_f)
{
	PROCESS_MITIGATION_ASLR_POLICY ASLRpolicy = {0};
	ASLRpolicy.EnableBottomUpRandomization = true;
	ASLRpolicy.EnableForceRelocateImages = true;
#ifdef _WIN64
	ASLRpolicy.EnableHighEntropy = true;
#endif
	ASLRpolicy.DisallowStrippedImages = true;
	const BOOL result =
		SetProcessMitigationPolicy_f(ProcessASLRPolicy, &ASLRpolicy, sizeof(ASLRpolicy));
	if (result == TRUE)
	{
		ATLTRACE(L"Successfully applied aggressive ASLR policy!\r\n");
		return;
	}
	const DWORD lastErr = GetLastError();
	ATLTRACE(L"Failed to apply aggressive ASLR policy! Error code: %u\r\n", lastErr);
}

void enableExtensionPointMitigations(_In_ SetProcessMitigationPolicy_t SetProcessMitigationPolicy_f)
{
	PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY ExtensionPolicy = {0};
	ExtensionPolicy.DisableExtensionPoints = true;
	const BOOL result =
		SetProcessMitigationPolicy_f(ProcessExtensionPointDisablePolicy, &ExtensionPolicy, sizeof(ExtensionPolicy));
	if (result == TRUE)
	{
		ATLTRACE(L"Successfully applied aggressive extension point policy!\r\n");
		return;
	}
	const DWORD lastErr = GetLastError();
	ATLTRACE(L"Failed to apply aggressive extension point policy! Error code: %u\r\n", lastErr);
}

void enableStrictHandleCheckingMitigations(_In_ SetProcessMitigationPolicy_t SetProcessMitigationPolicy_f)
{
	PROCESS_MITIGATION_STRICT_HANDLE_CHECK_POLICY HandlePolicy = {0};
	HandlePolicy.RaiseExceptionOnInvalidHandleReference = true;
	HandlePolicy.HandleExceptionsPermanentlyEnabled = true;
	const BOOL result =
		SetProcessMitigationPolicy_f(ProcessStrictHandleCheckPolicy, &HandlePolicy, sizeof(HandlePolicy));
	if (result == TRUE)
	{
		ATLTRACE(L"Successfully applied strict handle checking policy!\r\n");
		return;
	}
	const DWORD lastErr = GetLastError();
	ATLTRACE(L"Failed to apply strict handle checking policy! Error code: %u\r\n", lastErr);
}

void enableAggressiveProcessMitigations()
{
	if (!IsWindows8OrGreater())
	{
		return;
	}

	HMODULEhelper kern32("kernel32.dll");
	const SetProcessMitigationPolicy_t SetProcessMitigationPolicy_f =
		reinterpret_cast<SetProcessMitigationPolicy_t>(GetProcAddress(kern32.hModule, "SetProcessMitigationPolicy"));
	if (SetProcessMitigationPolicy_f == NULL)
	{
		const DWORD lastErr = GetLastError();
		ATLTRACE(L"Failed to get address of SetProcessMitigationPolicy! Error code: %u\r\n", lastErr);
		return;
	}
	enableASLRMitigation(SetProcessMitigationPolicy_f);
	enableExtensionPointMitigations(SetProcessMitigationPolicy_f);
	enableStrictHandleCheckingMitigations(SetProcessMitigationPolicy_f);
}

} //namespace {


// CUIforETWApp

BEGIN_MESSAGE_MAP(CUIforETWApp, CWinApp)
	ON_COMMAND(ID_HELP, &CUIforETWApp::OnHelp)
END_MESSAGE_MAP()


// CUIforETWApp construction

CUIforETWApp::CUIforETWApp() noexcept
{
	// support Restart Manager
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_RESTART;

	SetCurrentThreadName("Main");
}


// The one and only CUIforETWApp object

CUIforETWApp theApp;


// CUIforETWApp initialization

BOOL CUIforETWApp::InitInstance()
{
	enableAggressiveProcessMitigations( );

	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles. Otherwise, any window creation will fail.
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinApp::InitInstance();

	SetRegistryKey(L"RandomASCII");

	constexpr wchar_t identifier[] = L"{B7D2F8B8-2F28-4366-9D7A-691019D89185}";
	HANDLE mutex = CreateMutexW(nullptr, FALSE, identifier);
	// Only allow one copy to be running at a time.
	if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
		// Activate the previous window if possible. Note that if you have another
		// window with this title (an explorer window for a UI for ETW folder for
		// instance) then the wrong window may be activated. See
		// https://github.com/google/UIforETW/issues/147 for details.
		// google/UIforETW URL used because issues aren't copied when you fork.
		HWND prevWindow = FindWindow(NULL, L"UI for ETW");
		if (prevWindow)
		{
			SetForegroundWindow(prevWindow);
		}

		// Already running.
		return FALSE;
	}

	CUIforETWDlg dlg;
	m_pMainWnd = &dlg;
	const INT_PTR nResponse = dlg.DoModal();
	if (nResponse == -1)
	{
		ATLTRACE("Warning: dialog creation failed, "
					"so application is terminating unexpectedly.\r\n");

		ATLTRACE("Warning: if you are using MFC controls on the dialog, "
					"you cannot #define _AFX_NO_MFC_CONTROLS_IN_DIALOGS.\r\n");
		std::terminate( );
	}

	// Since the dialog has been closed, return FALSE so that we exit the
	// application, rather than start the application's message pump.
	return FALSE;
}

void CUIforETWApp::OnHelp() noexcept
{
	ShellExecute(NULL, NULL, L"https://randomascii.wordpress.com/2015/04/14/uiforetw-windows-performance-made-easier/", NULL, NULL, SW_SHOWNORMAL);
}
