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

namespace {

_Null_terminated_ const wchar_t StartEtwTracingString[ ] =
	L"Start ETW tracing.";

_Null_terminated_ const wchar_t btCompressToolTipString[ ] =
	L"Only uncheck this if you record traces on Windows 8 and above and want to analyze "
	L"them on Windows 7 and below.\n"
	L"Enable ETW trace compression. On Windows 8 and above this compresses traces "
	L"as they are saved, making them 5-10x smaller. However compressed traces cannot be loaded on "
	L"Windows 7 or earlier. On Windows 7 this setting has no effect.";

_Null_terminated_ const wchar_t btCswitchStacksString[ ] = 
	L"This enables recording of call stacks on context switches, from both "
	L"the thread being switched in and the readying thread. This should only be disabled if the performance "
	L"of functions like WaitForSingleObject and SetEvent appears to be distorted, which can happen when the "
	L"context-switch rate is very high.";

_Null_terminated_ const wchar_t InputTipString[ ] =
	L"Input tracing inserts custom ETW events into traces which can be helpful when "
	L"investigating performance problems that are correlated with user input. The default setting of "
	L"'private' records alphabetic keys as 'A' and numeric keys as '0'. The 'full' setting records "
	L"alphanumeric details. Both 'private' and 'full' record mouse movement and button clicks. The "
	L"'off' setting records no input.";

_Null_terminated_ const wchar_t SampledStacksString[ ] = 
	L"This enables recording of call stacks on CPU sampling events, which "
	L"by default happen at 1 KHz. This should rarely be disabled.";

_Null_terminated_ const wchar_t SampleRateString[ ] = 
	L"Checking this changes the CPU sampling frequency from the default of "
	L"~1 KHz to the maximum speed of ~8 KHz. This increases the data rate and thus the size of traces "
	L"but can make investigating brief CPU-bound performance problems (such as a single long frame) "
	L"more practical.";

_Null_terminated_ const wchar_t GPUTracingString[ ] =
	L"Check this to allow seeing GPU usage "
	L"in WPA, and more data in GPUView.";

_Null_terminated_ const wchar_t ShowCommandsString[ ] =
	L"This tells UIforETW to display the commands being "
	L"executed. This can be helpful for diagnostic purposes but is not normally needed.";

_Null_terminated_ const wchar_t TracingModeString[ ] =
	L"Select whether to trace straight to disk or to in-memory circular buffers.";

_Null_terminated_ const wchar_t TracesString[ ] =
	L"This is a list of all traces found in %etwtracedir%, which defaults to "
	L"documents\\etwtraces.";

_Null_terminated_ const wchar_t TraceNotesString[ ] =
	L"Trace notes are intended for recording information about ETW traces, such "
	L"as an analysis of what was discovered in the trace. "
	L"Trace notes are auto-saved to a parallel text "
	L"file - just type your analysis. "
	L"The notes files will be renamed when you rename traces "
	L"through the trace-list context menu.";

_Null_terminated_ const char NtSymbolEnvironmentVariableName[ ] =
	"_NT_SYMBOL_PATH";

_Null_terminated_ const char NtSymCacheEnvironmentVariableName[ ] =
	"_NT_SYMCACHE_PATH";

_Null_terminated_ const char DefaultSymbolPath[ ] =
	"SRV*c:\\symbols*http://msdl.microsoft.com/download/symbols";

_Null_terminated_ const char ChromiumSymbolPath[ ] =
	"SRV*c:\\symbols*http://msdl.microsoft.com/download/symbols;"
	"SRV*c:\\symbols*https://chromium-browser-symsrv.commondatastorage.googleapis.com";

_Null_terminated_ const char DefaultSymbolCachePath[ ] =
	"c:\\symcache";


void handle_vprintfFailure( _In_ const HRESULT fmtResult, _In_ const rsize_t bufferCount )
{
	if ( fmtResult == STRSAFE_E_INSUFFICIENT_BUFFER )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"CUIforETWDlg::vprintf FAILED TO format args to buffer!\r\n\tthe buffer ( size: %I64u ) was too small)\r\n", static_cast<uint64_t>( bufferCount ) );
		return;
	}
	if ( fmtResult == STRSAFE_E_INVALID_PARAMETER )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"CUIforETWDlg::vprintf FAILED TO format args to buffer! An invalid parameter was passed to StringCchVPrintf!\r\n" );
		return;
	}
	//how should we handle this correctly?
	ATLTRACE2( atlTraceGeneral, 0, L"CUIforETWDlg::vprintf FAILED TO format args to buffer! An expected error was encountered!\r\n" );
	if ( IsDebuggerPresent( ) )
	{
		_CrtDbgBreak( );
	}
	return;
}

bool copyWpaProfileToExecutableDirectory( _In_ const std::wstring& documents, _In_ const std::wstring exeDir )
{
	std::wstring wpaStartup = documents + std::wstring(L"\\WPA Files\\Startup.wpaProfile");
	if ( PathFileExists( wpaStartup.c_str( ) ) )
	{
		return true;
	}
	// Auto-copy a startup profile if there isn't one.
	const std::wstring sourceFile( exeDir + L"Startup.wpaProfile" );

	//If [CopyFile] succeeds, the return value is nonzero.
	//If [CopyFile] fails, the return value is zero.
	//To get extended error information, call GetLastError.
	const BOOL copyFileResult = CopyFile(sourceFile.c_str(), wpaStartup.c_str(), TRUE);


	//TODO: handle error properly!
	if ( copyFileResult == 0 )
	{
		return false;
	}
	return true;

}

_Success_( return )
bool addStringToCComboBox( _Inout_ CComboBox* const comboBox, _In_z_ PCWSTR const stringToAdd )
{
	//CComboBox::AddString calls SendMessage, to send a CB_ADDSTRING message.
	//CB_ADDSTRING returns CB_ERR or CB_ERRSPACE on failure.
	const int addStringResult = comboBox->AddString( stringToAdd );
	if ( addStringResult == CB_ERR )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Unexpected error adding string `%s`!!\r\n", stringToAdd );
		return false;
	}
	if ( addStringResult == CB_ERRSPACE )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Not enough space available to store string `%s`!!\r\n", stringToAdd );
		return false;
	}
	ATLTRACE2( atlTraceGeneral, 2, L"Successfully added string `%s` to CComboBox\r\n", stringToAdd );
	return true;
}

_Success_( return )
bool addbtInputTracingStrings( _Inout_ CComboBox* const btInputTracing )
{

	const bool addOffStringResult = addStringToCComboBox( btInputTracing, L"Off" );
	if ( !addOffStringResult )
	{
		return false;
	}

	const bool addPrivateStringResult = addStringToCComboBox( btInputTracing, L"Private");
	if ( !addPrivateStringResult )
	{
		return false;
	}

	const bool addFullStringResult = addStringToCComboBox( btInputTracing, L"Full");
	if ( !addFullStringResult )
	{
		return false;
	}
	return true;
}

_Success_( return )
bool addbtTracingModeStrings( _Inout_ CComboBox* const btTracingMode )
{
	const bool addCircularBufferStringResult = addStringToCComboBox( btTracingMode, L"Circular buffer tracing");
	if ( !addCircularBufferStringResult )
	{
		return false;
	}

	const bool addTraceToFileStringResult = addStringToCComboBox( btTracingMode, L"Tracing to file");
	if ( !addTraceToFileStringResult )
	{
		return false;
	}

	const bool addHeapTraceToFileStringResult = addStringToCComboBox( btTracingMode, L"Heap tracing to file");
	if ( !addHeapTraceToFileStringResult )
	{
		return false;
	}

	return true;
}

_Success_( return )
bool setCComboBoxSelection( _Inout_ CComboBox* const comboBoxToSet, _In_ _In_range_( -1, INT_MAX ) const int selectionToSet )
{
	//CComboBox::SetCurSel calls SendMessage, to send a CB_SETCURSEL message.

	//The documentation for CB_SETCURSEL is a bit confusing!
	//CB_SETCURSEL, if successful, returns the index of the item that was selected.
	//CB_SETCURSEL, on failure, returns CB_ERR.
	//The documentation suggests that -1 might be a valid argument for clearing the selection,
	//however, it also says that if passed -1, it will return CB_ERR.
	const int setSelectionResult = comboBoxToSet->SetCurSel( selectionToSet );
	if ( setSelectionResult == CB_ERR )
	{
		
		ATLTRACE2( atlTraceGeneral, 0,
				   L"Failed to set selection (for a comboBox)\r\n"
				   L"\tIndex that we attempted to set the selection to: %i\r\n"
				   L"\tCue banner of CComboBox: %s\r\n",
				   selectionToSet,
				   comboBoxToSet->GetCueBanner( ) //Grr. I hate CString.
				 );
		return false;
	}
	return true;
}

template<class DerivedButton>
_Success_( return )
bool addSingleToolToToolTip( 
							_Inout_ CToolTipCtrl*  const toolTip,
							_Inout_ DerivedButton* const btToAdd,
							_In_z_  PCWSTR         const toolTipMessageToAdd
						   )
{
	static_assert( std::is_base_of<CWnd, DerivedButton>::value, "Cannot add a non-CWnd-derived class as a tooltip!" );
	const BOOL addToolTipResult = toolTip->AddTool( btToAdd, toolTipMessageToAdd );
	if ( addToolTipResult != TRUE )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Failed to add a message to a tooltip!!\r\nThe tooltip message:\r\n\t`%s`\r\n", toolTipMessageToAdd );
		return false;
	}
	return true;
}


_Success_( return )
bool initializeToolTip(
						_Out_ CToolTipCtrl* const toolTip,
						_In_  CButton*      const btStartTracing,
						_In_  CButton*      const btCompress,
						_In_  CButton*      const btCswitchStacks,
						_In_  CButton*      const btSampledStacks,
						_In_  CButton*      const btFastSampling,
						_In_  CButton*      const btGPUTracing,
						_In_  CButton*      const btShowCommands,
						_In_  CStatic*      const btInputTracingLabel,
						_In_  CComboBox*    const btInputTracing,
						_In_  CComboBox*    const btTracingMode,
						_In_  CListBox*     const btTraces,
						_In_  CEdit*        const btTraceNotes
					  )
{
	
	toolTip->SetMaxTipWidth(400);
	toolTip->Activate(TRUE);

	//toolTip->AddTool( btStartTracing, L"Start ETW tracing.");

	const bool addStartETWTracingToolTip =
		addSingleToolToToolTip( toolTip, btStartTracing, StartEtwTracingString );
	if ( !addStartETWTracingToolTip )
	{
		return false;
	}

	//toolTip->AddTool( btCompress, btCompressToolTipString );
	
	const bool addBtCompressTollTip =
		addSingleToolToToolTip( toolTip, btCompress, btCompressToolTipString );
	if ( !addBtCompressTollTip )
	{
		return false;
	}

	//toolTip->AddTool( btCswitchStacks, btCswitchStacksString );
	
	const bool addBtCswitchStacksString =
		addSingleToolToToolTip( toolTip, btCswitchStacks, btCswitchStacksString );
	if ( !addBtCswitchStacksString )
	{
		return false;
	}
	
	//toolTip->AddTool( btSampledStacks, );
	
	const bool addBtSampledStacksString =
		addSingleToolToToolTip( toolTip, btSampledStacks, SampledStacksString );
	if ( !addBtSampledStacksString )
	{
		return false;
	}


	//toolTip->AddTool( btFastSampling, SampleRateString);
	
	const bool addToolString =
		addSingleToolToToolTip( toolTip, btFastSampling, SampleRateString );
	if ( !addToolString )
	{
		return false;
	}

	//toolTip->AddTool( btGPUTracing, GPUTracingString );

	const bool addGPUTracing =
		addSingleToolToToolTip( toolTip, btGPUTracing, GPUTracingString );
	if ( !addGPUTracing )
	{
		return false;
	}


	//toolTip->AddTool( btShowCommands, ShowCommandsString );

	const bool addShowCommands =
		addSingleToolToToolTip( toolTip, btShowCommands, ShowCommandsString );
	if ( !addShowCommands )
	{
		return false;
	}

	
	//toolTip->AddTool( btInputTracingLabel, InputTipString);
	const bool addTracingLabel =
		addSingleToolToToolTip( toolTip, btInputTracingLabel, InputTipString);
	if ( !addTracingLabel )
	{
		return false;
	}
	
	//toolTip->AddTool( btInputTracing, InputTipString);
	const bool addInputTracing =
		addSingleToolToToolTip( toolTip, btInputTracing, InputTipString );
	if ( !addInputTracing )
	{
		return false;
	}

	//toolTip->AddTool( btTracingMode, TracingModeString);
	const bool addTracingMode =
		addSingleToolToToolTip( toolTip, btTracingMode, TracingModeString);
	if ( !addTracingMode )
	{
		return false;
	}


	//toolTip->AddTool( btTraces, TracesString );
	const bool addTraces =
		addSingleToolToToolTip( toolTip, btTraces, TracesString );
	if ( !addTraces )
	{
		return false;
	}

	
	//toolTip->AddTool( btTraceNotes, TraceNotesString);
	const bool addTraceNotes =
		addSingleToolToToolTip( toolTip, btTraceNotes, TraceNotesString);
	if ( !addTraceNotes )
	{
		return false;
	}

	return true;
}

std::wstring GetPathToWindowsPerformanceToolkit( )
{
	// The WPT 8.1 installer is always a 32-bit installer, so on 64-bit
	// Windows it ends up in the (x86) directory.
	if ( Is64BitWindows( ) )
	{
		return L"C:\\Program Files (x86)\\Windows Kits\\8.1\\Windows Performance Toolkit\\";
	}
	return L"C:\\Program Files\\Windows Kits\\8.1\\Windows Performance Toolkit\\";
}


CRect GetWindowRectFromHwnd( _In_ const HWND hwnd )
{
	RECT windowRect_temp;
	ASSERT( ::IsWindow( hwnd ) );
	const BOOL getWindowRectResult = ::GetWindowRect( hwnd, &windowRect_temp );
	if ( getWindowRectResult == 0 )
	{
		const DWORD err = GetLastError( );

		//TODO: format error code!
		ATLTRACE2( atlTraceGeneral, 0, L"GetWindowRect failed!! Error code: %u\r\n", err );
		
		exit(10);
	}

	return CRect( windowRect_temp );
}

void SetSaveTraceBuffersWindowText( _In_ const HWND hWnd )
{
	const BOOL btSaveTbSetTextResult = ::SetWindowTextW( hWnd, L"Sa&ve Trace Buffers");
	if ( btSaveTbSetTextResult == 0 )
	{
		const DWORD err = GetLastError( );

		//TODO: format error code!
		ATLTRACE2( atlTraceGeneral, 0, L"::SetWindowTextW( btSaveTraceBuffers_.m_hWnd, L\"Sa&ve Trace Buffers\" failed!!! Error code: %u\r\n", err );
		exit(10);
	}

}


bool appendAboutBoxToSystemMenu( _In_ const CWnd& window )
{
	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	static_assert( ( IDM_ABOUTBOX & 0xFFF0 ) == IDM_ABOUTBOX, "IDM_ABOUTBOX IS NOT in the system command range!" );
	static_assert( IDM_ABOUTBOX < 0xF000, "IDM_ABOUTBOX IS NOT in the system command range!" );



	//The pointer returned by [CWnd::GetSystemMenu(FALSE)] may be temporary and should not be stored for later use.
	CMenu* const pSysMenu = window.GetSystemMenu(FALSE);
	if ( pSysMenu == NULL )
	{
		return false;
	}

	CString strAboutMenu;

	//[CStringT:LoadString returns] nonzero if resource load was successful; otherwise 0.
	const BOOL bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
	ASSERT(bNameValid);
	if ( bNameValid == 0 )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Failed to load about box menu string!!\r\n" );
		return false;
	}
	if ( strAboutMenu.IsEmpty( ) )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"About box string is empty?!?!\r\n" );
		return false;
	}


	//[CWnd::AppendMenu returns] nonzero if the function is successful; otherwise 0.
	const BOOL appendSeparatorResult = pSysMenu->AppendMenu(MF_SEPARATOR);
	if ( appendSeparatorResult == 0 )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Failed to append separator to menu!\r\n" );
		return false;
	}
	const BOOL appendAboutBoxResult = pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
	if ( appendAboutBoxResult == 0 )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Failed to append about box string to menu!\r\n" );
		return false;
	}

	return true;

}

void checkETWCompatibility( )
{
	if (!IsWindowsVistaOrGreater( ))
	{
		AfxMessageBox(L"ETW tracing requires Windows Vista or above.");
		exit(10);
	}
	return;
}

void CheckDialogButtons( 
						_In_ CWnd* const dlgToCheck,
						_In_ const bool bCompress,
						_In_ const bool bCswitchStacks,
						_In_ const bool bSampledStacks,
						_In_ const bool bFastSampling,
						_In_ const bool bGPUTracing,
						_In_ const bool bShowCommands
					   )
{
	//This MFC function (CheckDlgButton) doesn't properly check return codes for internal calls.
	dlgToCheck->CheckDlgButton(IDC_COMPRESSTRACE, bCompress );
	dlgToCheck->CheckDlgButton(IDC_CONTEXTSWITCHCALLSTACKS, bCswitchStacks );
	dlgToCheck->CheckDlgButton(IDC_CPUSAMPLINGCALLSTACKS, bSampledStacks);
	dlgToCheck->CheckDlgButton(IDC_FASTSAMPLING, bFastSampling);
	dlgToCheck->CheckDlgButton(IDC_GPUTRACING, bGPUTracing);
	dlgToCheck->CheckDlgButton(IDC_SHOWCOMMANDS, bShowCommands);
}

HACCEL loadAcceleratorsForF2andESC( _In_ const HINSTANCE instance )
{
	// Load the F2 (rename) and ESC (silently swallow ESC) accelerators
	return LoadAccelerators( instance, MAKEINTRESOURCE(IDR_ACCELERATORS));
}

HACCEL loadAcceleratorsForExitingRenaming( _In_ const HINSTANCE instance )
{
	// Load the Enter accelerator for exiting renaming.
	return LoadAccelerators( instance, MAKEINTRESOURCE(IDR_RENAMEACCELERATORS));
}

HACCEL loadAcceleratorsForEditingTraceNotes( _In_ const HINSTANCE instance )
{
	// Load the accelerators for when editing trace notes.
	return LoadAccelerators( instance, MAKEINTRESOURCE(IDR_NOTESACCELERATORS));
}

HACCEL loadAcceleratorsForActiveTraceList( _In_ const HINSTANCE instance )
{
	// Load the accelerators for when the trace list is active.
	return LoadAccelerators( instance, MAKEINTRESOURCE(IDR_TRACESACCELERATORS));

}

std::string getChromiumSymbolPath( _In_ const bool bIsChromeDev )
{
	if ( bIsChromeDev )
	{
		return ChromiumSymbolPath;
	}
	return DefaultSymbolPath;
}

_Success_( return )
bool setEnvironmentVariable( _In_z_ PCSTR const variableName, _In_z_ PCSTR const value )
{
	//If [SetEnvironmentVariableA] succeeds, the return value of [SetEnvironmentVariableA] is nonzero.
	//If the [SetEnvironmentVariableA] fails, the return value of [SetEnvironmentVariableA] is zero.
	//To get extended error information, call GetLastError.
	const BOOL setSymbolPathResult = SetEnvironmentVariableA( variableName, value );

	if ( setSymbolPathResult == 0 )
	{
		const DWORD err = GetLastError( );
		ATLTRACE2( atlTraceGeneral, 0, L"Failed to set symbol path!\r\n\tAttempted to set environment variable: `%S`\r\n\tValue that we attempted to set the variable to: `%S`\r\n\tError code: %u\r\n", variableName, value, err );
		return false;
	}
	return true;

}

_Success_( return )
bool setChromiumSymbolPath( _In_ const bool bChromeDeveloper )
{
	const std::string symbolPath = getChromiumSymbolPath( bChromeDeveloper );

	const bool setSymbolPathResult =
		setEnvironmentVariable( 
								NtSymbolEnvironmentVariableName,
								symbolPath.c_str( )
								);

	if ( !setSymbolPathResult )
	{
		return false;
	}

	outputPrintf( L"Setting _NT_SYMBOL_PATH to %S (Microsoft%s). "
					L"Set _NT_SYMBOL_PATH yourself or toggle"
					L"'Chrome developer' if you want different defaults.\n",
					symbolPath.c_str( ),
					( bChromeDeveloper ? L" plus Chrome" : L"" )
				);

	return true;
}

std::wstring getRawDirectoryFromEnvironmentVariable( _In_z_ PCWSTR env )
{
	rsize_t returnValue = 0;
	const errno_t getEnvironmentVariableLengthResult = _wgetenv_s( &returnValue, NULL, 0, env );
	if ( getEnvironmentVariableLengthResult != 0 )
	{
		//Failure!
#ifndef DEBUG
#error fix this!
#endif
	}
	if ( returnValue == 0 )
	{
		//Not found!
		return L"";
	}

	// Get a directory (from an environment variable, if set) and make sure it exists.
	//std::wstring result = default;
	const rsize_t bufferSize = 512u;
	
	
	if ( ( returnValue + 1 ) > bufferSize )
	{
		std::unique_ptr<wchar_t[ ]> dynamicBuffer = std::make_unique<wchar_t[ ]>( returnValue + 1 );
		const errno_t getEnvResult = _wgetenv_s( &returnValue, dynamicBuffer.get( ), ( returnValue + 1 ), env );
		if ( getEnvResult != 0 )
		{
			//Failure!
#ifndef DEBUG
#error fix this!
#endif
		}
		return dynamicBuffer.get( );
	}

	wchar_t environmentVariableBuffer[ bufferSize ] = { 0 };
	const errno_t getEnvResult = _wgetenv_s( &returnValue, environmentVariableBuffer, env );
	if ( getEnvResult != 0 )
	{
		//Failure!
#ifndef DEBUG
#error fix this!
#endif
	}
	return environmentVariableBuffer;
}

std::wstring getTraceDirFromEnvironmentVariable( _In_z_ PCWSTR env, const std::wstring& default )
{
	
	std::wstring rawDirectory = getRawDirectoryFromEnvironmentVariable( env );

	std::wstring result = default;

	if (!rawDirectory.empty( ) )
	{
		result = rawDirectory;
	}

	// Make sure the name ends with a backslash.
	if ( ( !result.empty( ) ) && ( result[ result.size( ) - 1 ] != '\\' ) )
	{
		result += '\\';
	}
	return result;

}

bool isValidPathToFSObject( std::wstring path )
{
	if ( path.size( ) < MAX_PATH )
	{

	}
}


}// namespace {


// This convenient hack function is so that the ChildProcess code can
// print to the main output window. This function can only be called
// from the main thread.
void outputPrintf(_Printf_format_string_ PCWSTR pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	pMainWindow->vprintf(pFormat, args);
	va_end(args);
}

void CUIforETWDlg::vprintf(PCWSTR pFormat, va_list args)
{
	const rsize_t bufferCount = 5000u;
	
	wchar_t buffer[ bufferCount ];

	const HRESULT printFormattedArgsToBuffer = StringCchVPrintfW( buffer, bufferCount, pFormat, args );
	ASSERT( SUCCEEDED( printFormattedArgsToBuffer ) );
	if ( FAILED( printFormattedArgsToBuffer ) )
	{
		//how should we handle this correctly?
		handle_vprintfFailure( printFormattedArgsToBuffer, bufferCount );
		return;
	}


	for (PCWSTR pBuf = buffer; *pBuf; ++pBuf)
	{
		// Need \r\n as a line separator.
		if (pBuf[0] == '\n')
		{
			// Don't add a line separator at the very beginning.
			if ( !output_.empty( ) )
			{
				output_ += L"\r\n";
			}
		}
		else
		{
			output_ += pBuf[ 0 ];
		}
	}

	//This is why I hate MFC, they've overloaded a function with a macro-defined name!
	//And CWnd::SetDlgItemText doesn't even check the return value of SetDlgItemText
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


CUIforETWDlg::CUIforETWDlg(_In_opt_ CWnd* pParent )
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
	ON_MESSAGE(WM_UPDATETRACELIST, &CUIforETWDlg::UpdateTraceListHandler)
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


_Success_( return )
bool CUIforETWDlg::SetSymbolPath()
{
	// Make sure that the symbol paths are set.
	// See:
	//    "Changing Environment Variables"
	//    https://msdn.microsoft.com/en-us/library/windows/desktop/ms682009.aspx

#pragma warning(suppress : 4996)
	if (bManageSymbolPath_ || !getenv(NtSymbolEnvironmentVariableName))
	{
		bManageSymbolPath_ = true;
		const bool setChromiumSymbolPathResult = setChromiumSymbolPath( bChromeDeveloper_ );
		if ( !setChromiumSymbolPathResult )
		{
			return false;
		}
	}
	
	size_t sizeRequired = 0;
	//pReturnValue (first parameter)
	//The buffer size that's required, or 0 if the variable is not found.
	//[getenv_s returns] zero if successful; otherwise, an error code on failure.
	//What the hell does a "not found" look like?
	const errno_t getSymCachePathResult = getenv_s( &sizeRequired, NULL, 0, NtSymCacheEnvironmentVariableName );
	
	//TODO: does this make any goddamned sense?
	if ( getSymCachePathResult != 0 )
	{
		//getenv_s failed!
		return false;
	}

	if ( sizeRequired != 0 )
	{
		ATLTRACE2( atlTraceGeneral, 1, L"_NT_SYMCACHE_PATH was found! No work necessary!\r\n" );
		return true;
	}

	const bool SetSymbolCachePathresult = setEnvironmentVariable( NtSymCacheEnvironmentVariableName, DefaultSymbolCachePath );
	if ( !SetSymbolCachePathresult )
	{
		return false;
	}
	return true;
}


BOOL CUIforETWDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	ATLTRACE2( atlTraceGeneral, 0, L"Initializing dialog box!\r\n" );

	const HINSTANCE instanceHandle = AfxGetInstanceHandle( );

	hAccelTable_ = loadAcceleratorsForF2andESC( instanceHandle );
	hRenameAccelTable_ = loadAcceleratorsForExitingRenaming( instanceHandle );
	hNotesAccelTable_ = loadAcceleratorsForEditingTraceNotes( instanceHandle );
	hTracesAccelTable_ = loadAcceleratorsForActiveTraceList( instanceHandle );

	CRect windowRect = GetWindowRectFromHwnd( m_hWnd );

	initialWidth_ = windowRect.Width();
	lastWidth_ = windowRect.Width( );

	initialHeight_ = windowRect.Height();
	lastHeight_ = windowRect.Height( );

	// Win+Ctrl+C is used to trigger recording of traces. This is compatible with
	// wprui. If this is changed then be sure to change the button text.
	if (!RegisterHotKey(*this, kRecordTraceHotKey, MOD_WIN + MOD_CONTROL, 'C'))
	{
		AfxMessageBox(L"Couldn't register hot key.");

		SetSaveTraceBuffersWindowText( btSaveTraceBuffers_.m_hWnd );
	}


	const bool appendAboutBoxResult = appendAboutBoxToSystemMenu( *this );
	if ( !appendAboutBoxResult )
	{
		AfxMessageBox(L"Couldn't append about box!");
		exit( 10 );
	}

	checkETWCompatibility( );


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
	const std::wstring xperfPath( GetXperfPath( ) );

	if (!PathFileExists(xperfPath.c_str()))
	{
		AfxMessageBox( ( xperfPath + L" does not exist. Please install WPT 8.1. Exiting." ).c_str( ) );
		exit(10);
	}

	_Null_terminated_ wchar_t documents_temp[ MAX_PATH ] = { 0 };

	//We want to CREATE IT if it doesn't exist??!?
	const HRESULT shGetMyDocResult = SHGetFolderPath( 0, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, documents_temp );
	ASSERT( SUCCEEDED( shGetMyDocResult ) );

	if ( FAILED( shGetMyDocResult ) )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Failed to find the My Documents directory!\r\n" );
		exit(10);
	}

	//request Unicode (long-path compatible) versions of APIs
	const std::wstring documents( L"\\\\?\\" + std::wstring( documents_temp ) );

	std::wstring defaultTraceDir = documents + std::wstring(L"\\etwtraces\\");
	traceDir_ = GetDirectory(L"etwtracedir", defaultTraceDir);

	const bool copyToDirResult = copyWpaProfileToExecutableDirectory( documents, std::move( GetExeDir( ) ) );
	if ( !copyToDirResult )
	{
		//TODO: properly handle error!
		exit(10);
	}

	tempTraceDir_ = GetDirectory(L"temp", traceDir_);

	SetSymbolPath();

	btTraceNameEdit_.GetWindowRect(&traceNameEditRect_);

	//This MFC function (ScreenToClient) doesn't properly check return codes for internal calls.
	ScreenToClient(&traceNameEditRect_);

	// Set the icon for this dialog. The framework does this automatically
	// when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon

	if ( IsWindows8OrGreater( ) )
	{
		bCompress_ = false; // ETW trace compression requires Windows 8.0
		SmartEnableWindow(btCompress_, false);
	}

	CheckDialogButtons(
						this,
						bCompress_,
						bCswitchStacks_,
						bSampledStacks_,
						bFastSampling_,
						bGPUTracing_,
						bShowCommands_
					  );



	// If a fast sampling speed is requested then set it now.
	//**Note that this assumes that the speed will otherwise be normal**.
	if (bFastSampling_)
		SetSamplingSpeed();


	
	const bool addInputTracingStringsResult = addbtInputTracingStrings( &btInputTracing_ );
	if ( !addInputTracingStringsResult )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Failed to add strings to btInputTracing_!\r\n" );
		exit(10);
	}
	

	static_assert( std::is_convertible<decltype( InputTracing_ ), int>::value, "Bad argument to set CComboBox selection to (on next line)! We need to be able to convert to an int!" );
	const bool setBtInputTracingSelectionResult = setCComboBoxSelection( &btInputTracing_, InputTracing_ );
	if ( !setBtInputTracingSelectionResult )
	{
		ATLTRACE( atlTraceGeneral, 0, L"Failed to set btInputTracing_ selection to index: %i\r\n", InputTracing_ );
		exit(10);

	}

	const bool addTracingModeStringsResult = addbtTracingModeStrings( &btTracingMode_ );
	if ( !addTracingModeStringsResult )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Failed to add strings to btTracingMode_!\r\n" );
		exit(10);
	}

	
	
	//btTracingMode_.SetCurSel(tracingMode_);
	
	static_assert( std::is_convertible<decltype( tracingMode_ ), int>::value, "Bad argument to set CComboBox selection to (on next line)! We need to be able to convert to an int!" );
	const bool setBtTracingModeSelection = setCComboBoxSelection( &btTracingMode_, tracingMode_ );
	if ( !setBtTracingModeSelection )
	{
		ATLTRACE2( atlTraceGeneral, 0, L"Failed to set btTracingMode_ selection to index: %i\r\n", tracingMode_ );
		exit(10);
	}
	UpdateEnabling();
	SmartEnableWindow(btTraceNotes_, false); // This window always starts out disabled.

	// Don't change traceDir_ because the monitor thread has a pointer to it.
	monitorThread_.StartThread(&traceDir_);

	DisablePagingExecutive();

	UpdateTraceList();

	const BOOL toolTipCreateResult = toolTip_.Create( this );

	if (toolTipCreateResult)
	{
		const bool initializeToolTipResult =
			initializeToolTip(
								&toolTip_,
								&btStartTracing_,
								&btCompress_,
								&btCswitchStacks_,
								&btSampledStacks_,
								&btFastSampling_,
								&btGPUTracing_,
								&btShowCommands_,
								&btInputTracingLabel_,
								&btInputTracing_,
								&btTracingMode_,
								&btTraces_,
								&btTraceNotes_
							 );
		if ( !initializeToolTipResult )
		{
			ATLTRACE2( atlTraceGeneral, 0, L"Failed to initialize tool tips!" );
			exit(10);
		}
	}

	SetHeapTracing(false);
	// Start the input logging thread with the current settings.
	SetKeyloggingState(InputTracing_);

	return TRUE; // return TRUE unless you set the focus to a control
}

std::wstring CUIforETWDlg::GetDirectory( _In_z_ PCWSTR env, const std::wstring& default)
{
	// Get a directory (from an environment variable, if set) and make sure it exists.

	std::wstring result = getTraceDirFromEnvironmentVariable( env, default );

	//This is a very bad way of doing things!
	//if (!PathFileExistsW(result.c_str()))
	//{
	//	(void)_wmkdir(result.c_str());
	//}

	const bool doesFileExist = isValidPathToFSObject( result );

	if (!PathFileExistsW(result.c_str()))
	{
		(void)_wmkdir(result.c_str());
	}

	if (!PathIsDirectoryW(result.c_str()))
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

_Pre_satisfies_( ( tracingMode_ == kTracingToMemory ) || ( tracingMode_ == kTracingToFile ) || ( tracingMode_ == kHeapTracingToFile ) )
void CUIforETWDlg::OnBnClickedStarttracing()
{
	RegisterProviders();
	if ( tracingMode_ == kTracingToMemory )
	{
		outputPrintf( L"\nStarting tracing to in-memory circular buffers...\n" );
	}
	else if ( tracingMode_ == kTracingToFile )
	{
		outputPrintf( L"\nStarting tracing to disk...\n" );
	}
	else if ( tracingMode_ == kHeapTracingToFile )
	{
		outputPrintf( L"\nStarting heap tracing to disk of %s...\n", heapTracingExe_.c_str( ) );
	}
	else
	{
		assert( 0 );
	}

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
	if ( bSaveTrace )
	{
		outputPrintf( L"\nSaving trace to disk...\n" );
	}
	else
	{
		outputPrintf( L"\nStopping tracing...\n" );
	}

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
			if ( tracingMode_ == kHeapTracingToFile )
			{
				child.Run( bShowCommands_, L"xperf.exe -stop xperfHeapSession -stop UIforETWSession -stop " + GetKernelLogger( ) );
			}
			else
			{
				child.Run( bShowCommands_, L"xperf.exe -stop UIforETWSession -stop " + GetKernelLogger( ) );
			}
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
			if ( tracingMode_ == kHeapTracingToFile )
			{
				args += L" \"" + GetHeapFile( ) + L"\"";
			}
			args += L" \"" + traceFilename + L"\"";
			if ( bCompress_ )
			{
				args += L" -compress";
			}
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
	{
		outputPrintf( L"Tracing stopped.\n" );
	}
}


void CUIforETWDlg::OnBnClickedSavetracebuffers()
{
	StopTracingAndMaybeRecord(true);
}

void CUIforETWDlg::OnBnClickedStoptracing()
{
	OutputDebugStringA( "\"Stop Tracing\" clicked!\r\n" );
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


//This is a mess!
void CUIforETWDlg::PreprocessTrace(const std::wstring& traceFilename)
{
	if ( !bChromeDeveloper_ )
	{
		return;
	}

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
			PCWSTR const typeLabel = L" --type=";
			PCWSTR typeFound = wcsstr(line.c_str(), typeLabel);
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
			PCWSTR const pidstr = wcschr(line.c_str(), '(');
			if (pidstr)
			{
				const int scanToStringResult = swscanf_s(pidstr + 1, L"%lu", &pid);
				if ( scanToStringResult == 0 )
				{
					ATLTRACE2( atlTraceGeneral, 0, L"No fields assigned from pidstr+1 (%s)!!\r\n", ( pidstr+1 ) );
					//0 is an invalid process ID!
					//http://blogs.msdn.com/b/oldnewthing/archive/2004/02/23/78395.aspx
					pidsByType[type].push_back( 0 );
					continue;
				}
				if ( scanToStringResult == EOF )
				{
					ATLTRACE2( atlTraceGeneral, 0, L"EOF hit before assigning fields from pidstr+1 (%s)!!\r\n", ( pidstr+1 ) );
					//0 is an invalid process ID!
					//http://blogs.msdn.com/b/oldnewthing/archive/2004/02/23/78395.aspx
					pidsByType[type].push_back( 0 );
					continue;
				}
				static_assert( EOF == -1, "swscanf_s might return a value that's not handled!" );
			}
			if (pid)
			{
				pidsByType[type].push_back(pid);
			}
		}
	}
	//Maybe we should request the unicode version of the APIs (for long path support)?
	const std::wstring fileToOpen = ( traceFilename.substr( 0, traceFilename.size( ) - 4 ) + L".txt" );

#pragma warning(suppress : 4996)
	//FILE* pFile = _wfopen(fileToOpen.c_str(), L"a");
	FILE* pFile = NULL;

	//[_wfopen_s returns] zero if successful; an error code on failure.
	const errno_t fileOpenResult = _wfopen_s( &pFile, fileToOpen.c_str( ), L"a" );
	if ( fileOpenResult != 0 )
	{
		//const std::string err_str( std::to_string( fileToOpen ) );//Grr. Need a proper way to convert!
		throw std::runtime_error( "Failed to open a file for preprocessing!" );
	}

	if ( pFile == NULL )
	{
		throw std::runtime_error( "(apparently) Failed to open a file for preprocessing! pFile is NULL!" );
	}


	const int PIDsByProcessTypeResult = fwprintf_s(pFile, L"Chrome PIDs by process type:\n");
	if ( PIDsByProcessTypeResult < 0 )
	{
		const int closeResult = fclose( pFile );
		if ( closeResult != 0 )
		{
			ATLTRACE( atlTraceGeneral, 0, L"DOUBLE FAULT: serious error occurred! Failed to write to file, then failed to close it!\r\n" );
			std::terminate( );
		}
		throw std::runtime_error( "Failed to write 'PIDs by process type' to file!" );
	}
	for (auto& types : pidsByType)
	{
		const int firstTypesResult = fwprintf_s(pFile, L"%-10s:", types.first.c_str());
		if ( firstTypesResult < 0 )
		{
			const int closeResult = fclose( pFile );
			if ( closeResult != 0 )
			{
				ATLTRACE( atlTraceGeneral, 0, L"DOUBLE FAULT: serious error occurred! Failed to write to file, then failed to close it!\r\n" );
				std::terminate( );
			}
			throw std::runtime_error( "Failed to write types.first to file!" );
		}

		for (auto& pid : types.second)
		{
			const int secondTypesResult = fwprintf_s(pFile, L" %lu", pid);
			if ( secondTypesResult < 0 )
			{
				const int closeResult = fclose( pFile );
				if ( closeResult != 0 )
				{
					ATLTRACE( atlTraceGeneral, 0, L"DOUBLE FAULT: serious error occurred! Failed to write to file, then failed to close it!\r\n" );
					std::terminate( );
				}
				throw std::runtime_error( "Failed to write types.second.pid to file!" );
			}
		}

		const int endOfFileNewLineResult = fwprintf_s(pFile, L"\n");
		if ( endOfFileNewLineResult < 0 )
		{
			const int closeResult = fclose( pFile );
			if ( closeResult != 0 )
			{
				ATLTRACE( atlTraceGeneral, 0, L"DOUBLE FAULT: serious error occurred! Failed to write to file, then failed to close it!\r\n" );
				std::terminate( );
			}
			throw std::runtime_error( "Failed to new line to end of file!" );
		}
	}
	const int fcloseResultFinal = fclose(pFile);
	if ( fcloseResultFinal != 0 )
	{
		throw std::runtime_error( "Failed to close file, after successfully writing to it!" );
	}

#endif

}


void CUIforETWDlg::StartRenameTrace(bool fullRename)
{
	SaveNotesIfNeeded();
	const int curSel = btTraces_.GetCurSel();

	ASSERT( traces_.size( ) < INT_MAX );
	if ( curSel < 0 )
	{
		return;
	}

	if ( curSel >= static_cast< int >( traces_.size( ) ) )
	{
		return;
	}

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
			if ( ( c != '-' ) && ( c != '_' ) && ( c != '.' ) && ( !iswdigit( c ) ) )
			{
				validRenameDate_ = false;
			}
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


	//[CListBox::GetItemRect returns] LB_ERR if an error occurs.
	const int getRectResult = btTraces_.GetItemRect(curSel, &itemRect);
	if ( getRectResult == LB_ERR )
	{
		//Yeah, we should probably unwind here.
		throw std::runtime_error( "CUIforETWDlg::StartRenameTrace - Unexpected error in btTraces_.GetItemRect!" );
	}

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
		renamed.reserve( oldNames.size( ) );

		std::wstring failedSource;
		for (const auto& oldName : oldNames)
		{
			std::wstring extension = GetFileExt(oldName);;
			std::wstring newName = newTraceName + extension;
			const BOOL result = MoveFile((GetTraceDir() + oldName).c_str(), (GetTraceDir() + newName).c_str());
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
