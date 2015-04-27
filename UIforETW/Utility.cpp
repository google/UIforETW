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
#include "Utility.h"
#include <fstream>
#include "alias.h"

namespace {


void handleMultiByteToWideCharFailure( const DWORD error )
{
	if (error == ERROR_INSUFFICIENT_BUFFER)
	{
		outputPrintf( L"MultiByteToWideChar failed!!: ERROR_INSUFFICIENT_BUFFER\n" );
		return;
	}
	if (error == ERROR_NO_UNICODE_TRANSLATION)
	{
		outputPrintf( L"MultiByteToWideChar failed!!: ERROR_NO_UNICODE_TRANSLATION\n" );
		return;
	}
	if (error == ERROR_INVALID_FLAGS)
	{
		outputPrintf( L"MultiByteToWideChar failed!!: ERROR_INVALID_FLAGS\n" );
		MessageBoxW( NULL, L"MultiByteToWideChar failed!!: ERROR_INVALID_FLAGS - this indicates a logic error!", L"Fatal error!", MB_OK );
		std::terminate( );
	}
	if (error == ERROR_INVALID_PARAMETER)
	{
		outputPrintf( L"MultiByteToWideChar failed!!: ERROR_INVALID_PARAMETER\n" );
		MessageBoxW( NULL, L"MultiByteToWideChar failed!!: ERROR_INVALID_PARAMETER - this indicates a logic error!", L"Fatal error!", MB_OK );
		std::terminate( );
	}
	outputPrintf( L"MultiByteToWideChar failed!!: (unexpected error!)\n" );
	MessageBoxW( NULL, L"MultiByteToWideChar failed!!: (unexpected error!) - this indicates a logic error!", L"Fatal error!", MB_OK );
	std::terminate( );
}

}

namespace handle_close {


//Die if we fail to close registry key
void regCloseKey( _In_ _Pre_valid_ _Post_ptr_invalid_ HKEY hKey )
{
	//If [RegCloseKey] succeeds, the return value is ERROR_SUCCESS.
	//If the function fails, the return value is a nonzero error code 
	//defined in Winerror.h. 
	//You can use the FormatMessage function with the 
	//FORMAT_MESSAGE_FROM_SYSTEM flag to get a generic description of the error.
	const LONG result = ::RegCloseKey( hKey );
	if ( result == ERROR_SUCCESS )
	{
		return;
	}
	debug::Alias( &result );
	debug::Alias( &hKey );

	const DWORD lastErr = GetLastError( );
	ErrorHandling::outputErrorDebug( lastErr );
	std::terminate( );
}

void closeHandle( _In_ _Pre_valid_ _Post_ptr_invalid_ HANDLE handle )
{
	//If [CloseHandle] succeeds, the return value is nonzero.
	//If [CloseHandle] fails, the return value is zero.
	//To get extended error information, call GetLastError.
	const BOOL result = ::CloseHandle( handle );
	ATLASSERT( result != 0 );
	
	if ( result != 0 )
	{
		return;
	}

	const DWORD lastErr = GetLastError( );
	ErrorHandling::outputErrorDebug( lastErr );
	std::terminate( );
}

}

namespace ErrorHandling {
void DisplayWindowsMessageBoxWithErrorMessage( const DWORD error )
{
	const rsize_t errorMessageBufferSize = 512u;
	wchar_t errorMessageBuffer[ errorMessageBufferSize ] = { 0 };
	
	GetLastErrorAsFormattedMessage( errorMessageBuffer, error );

	const int messageBoxResult =
		::MessageBoxW(
						NULL,
						errorMessageBuffer,
						L"UIforETW",
						(MB_OK bitor MB_ICONERROR)
					 );
	
	if ( messageBoxResult == 0 )
	{
		OutputDebugStringA( 
							"UIforETW successfully formatted an error message, "
							"but failed to display it in a message box. "
							"There's nothing we can do, except crash & burn\r\n"
						  );
		debug::Alias( &messageBoxResult );
		debug::Alias( &error );
		debug::Alias( &errorMessageBuffer );
		std::terminate( );
	}
}


template<rsize_t strSize>
void GetLastErrorAsFormattedMessage( 
								ETWUI_WRITES_TO_STACK( strSize )
									wchar_t (&psz_formatted_error)[strSize],
									const DWORD error
							  )
{
	const DWORD ret = FormatMessageW( 
										(FORMAT_MESSAGE_FROM_SYSTEM bitor FORMAT_MESSAGE_IGNORE_INSERTS), 
										NULL, 
										error, 
										MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), 
										psz_formatted_error, 
										static_cast<DWORD>( strSize ), 
										NULL 
									);
	if ( ret != 0 )
	{
		return;
	}
	const DWORD error_err = GetLastError( );
	ATLTRACE2( 
				L"FormatMessageW failed with error code: `%lu`!!\r\n", 
				error_err
			 );
	
	debug::Alias( &error_err );
	debug::Alias( &ret );
	debug::Alias( &error );
	debug::Alias( &psz_formatted_error );
	std::terminate( );
}

void outputErrorDebug( const DWORD lastErr )
{
	const rsize_t bufferSize = 512u;
	wchar_t errBuffer[ bufferSize ] = { 0 };
	ErrorHandling::GetLastErrorAsFormattedMessage( errBuffer, lastErr );
	OutputDebugStringA( "UIforETW:\tError message: " );
	OutputDebugStringW( errBuffer );
	OutputDebugStringA( "\r\n" );
}

void outputPrintfErrorDebug( const DWORD lastErr )
{
	const rsize_t bufferSize = 512u;
	wchar_t errBuffer[ bufferSize ] = { 0 };
	ErrorHandling::GetLastErrorAsFormattedMessage( errBuffer, lastErr );
	outputPrintf( L"Error message: %s\n", errBuffer );
}

} //namespace ErrorHandling 


std::vector<std::wstring> split(const std::wstring& s, char c)
{
	std::wstring::size_type i = 0;
	std::wstring::size_type j = s.find(c);

	std::vector<std::wstring> result;
	while (j != std::wstring::npos)
	{
		result.push_back(s.substr(i, j - i));
		i = ++j;
		j = s.find(c, j);
	}

	if (!s.empty())
		result.push_back(s.substr(i, s.length()));

	return result;
}

std::vector<std::wstring> GetFileList(const std::wstring& pattern, bool fullPaths)
{
	std::wstring directory;
	if (fullPaths)
		directory = GetDirPart(pattern);
	WIN32_FIND_DATA findData;
	HANDLE hFindFile = FindFirstFileExW(pattern.c_str(), FindExInfoStandard,
				&findData, FindExSearchNameMatch, NULL, 0);

	std::vector<std::wstring> result;
	if (hFindFile != INVALID_HANDLE_VALUE)
	{
		do
		{
			result.push_back(directory + findData.cFileName);
		} while (FindNextFileW(hFindFile, &findData));

		FindClose(hFindFile);
	}

	return result;
}

// Load a file and convert to a string. If the file contains
// an embedded NUL then the resulting string will be truncated.
std::wstring LoadFileAsText(const std::wstring& fileName)
{
	std::ifstream f;
	f.open(fileName, std::ios_base::binary);
	if (!f)
	{
		return L"";
	}

	// Find the file length.
	f.seekg(0, std::ios_base::end);
	const auto raw_length = f.tellg( );
	ATLASSERT( raw_length >= 0 );
	ATLASSERT( raw_length < SIZE_T_MAX );
	size_t length = static_cast<size_t>( f.tellg() );
	
	f.seekg(0, std::ios_base::beg);

	// Allocate a buffer and read the file.
	std::vector<char> data(length + 2);
	f.read(&data[0], length);
	if (!f)
	{
		return L"";
	}

	// Add a multi-byte null terminator.
	data[length] = 0;
	data[length+1] = 0;

	const wchar_t bom = 0xFEFF;
	if (memcmp(&bom, &data[0], sizeof(bom)) == 0)
	{
		// Assume UTF-16, strip bom, and return.
		return reinterpret_cast<const wchar_t*>(&data[sizeof(bom)]);
	}

	// If not-UTF-16 then convert from ANSI to wstring and return
	return AnsiToUnicode(&data[0]);
}


void WriteTextAsFile(const std::wstring& fileName, const std::wstring& text)
{
	std::ofstream outFile;
	outFile.open(fileName, std::ios_base::binary);
	if (!outFile)
		return;

	const wchar_t bom = 0xFEFF; // Always write a byte order mark
	outFile.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
	outFile.write(reinterpret_cast<const char*>(text.c_str()), text.size() * sizeof(text[0]));
}

void SetRegistryDWORD(HKEY root, const std::wstring& subkey, const std::wstring& valueName, const DWORD value)
{

	//See Registry Element Size Limits:
	//    https://msdn.microsoft.com/en-us/library/windows/desktop/ms724872.aspx
	if (subkey.length( ) >= 255)
	{
		//TODO: how to best handle?
		throw std::logic_error( "Key too long!" );
	}
	if (valueName.length( ) >= 16383)
	{
		//TODO: how to best handle?
		throw std::logic_error("Value too long!");
	}


	HKEY key;
	//If [RegOpenKeyEx] succeeds, the return value is ERROR_SUCCESS.
	//If [RegOpenKeyEx] fails,
	//    the return value is a nonzero error code defined in Winerror.h.
	//  You can use the FormatMessage function with the 
	//    FORMAT_MESSAGE_FROM_SYSTEM flag to get a generic description of the error.
	const LONG openResult = RegOpenKeyExW(root, subkey.c_str(), 0, KEY_ALL_ACCESS, &key);
	if (openResult != ERROR_SUCCESS)
	{
		outputPrintf( L"Failed to open registry key `%s`\n", subkey.c_str( ) );
		ErrorHandling::outputPrintfErrorDebug( );
		return;
	}
	//RegSetValueEx has the exact same return behavior as RegOpenKeyEx
	const LONG setValueResult =
		RegSetValueExW(
						key, 
						valueName.c_str(),
						0,
						REG_DWORD,
						reinterpret_cast<const BYTE*>(&value),
						sizeof(value)
						);

	if (setValueResult != ERROR_SUCCESS)
	{
		//TODO: how to best handle?
		throw std::runtime_error( "Failed to write value to registry!!!" );
	}
	handle_close::regCloseKey( key );

		
	//SAL catches this :)
	//handle_close::regCloseKey( key );


}

void CreateRegistryKey(HKEY root, const std::wstring& subkey, const std::wstring& newKey)
{
	HKEY key;
	const LONG openResult =
		RegOpenKeyExW(root, subkey.c_str(), 0, KEY_ALL_ACCESS, &key);
	
	if ( openResult != ERROR_SUCCESS )
	{
		outputPrintf( L"Failed to open registry key `%s`\n", subkey.c_str( ) );
		ErrorHandling::outputPrintfErrorDebug( );
		return;
	}

	HKEY resultKey;
	const LONG createResult =
		RegCreateKeyW(key, newKey.c_str(), &resultKey);
	if (createResult == ERROR_SUCCESS)
	{
		RegCloseKey(resultKey);
	}
	RegCloseKey(key);

}

std::wstring GetEditControlText(HWND hEdit)
{
	std::wstring result;
	const int length = GetWindowTextLength(hEdit);
	std::vector<wchar_t> buffer(length + 1);
	GetWindowTextW(hEdit, &buffer[0], buffer.size());
	// Double-verify that the buffer is null-terminated.
	buffer[buffer.size() - 1] = 0;
	return &buffer[0];
}

std::wstring AnsiToUnicode(const std::string& text)
{
	// Determine number of wide characters to be allocated for the
	// Unicode string.
	const rsize_t cCharacters = text.size() + 1;

	std::vector<wchar_t> buffer(cCharacters);

	// Convert to Unicode.
	std::wstring result;
	if (MultiByteToWideChar(CP_ACP, 0, text.c_str(), cCharacters, &buffer[0], cCharacters))
	{
		// Double-verify that the buffer is null-terminated.
		buffer[buffer.size() - 1] = 0;
		result = &buffer[0];
		return result;
	}

	const DWORD lastErr = GetLastError( );
	handleMultiByteToWideCharFailure( lastErr );
	return result;
}

// Get the next/previous dialog item (next/prev in window order and tab order) allowing
// for disabled controls, invisible controls, and wrapping at the end of the tab order.

static bool ControlOK(HWND win)
{
	if (!win)
		return false;
	if (!IsWindowEnabled(win))
		return false;
	if (!(GetWindowLong(win, GWL_STYLE) & WS_TABSTOP))
		return false;
	// You have to check for visibility of the parent window because during dialog
	// creation the parent window is invisible, which renders the child windows
	// all invisible - not good.
	if (!IsWindowVisible(win) && IsWindowVisible(GetParent(win)))
		return false;
	return true;
}

static HWND GetNextDlgItem(HWND win, const bool Wrap)
{
	HWND next = GetWindow(win, GW_HWNDNEXT);
	while (next != win && !ControlOK(next))
	{
		if (next)
		{
			next = GetWindow( next, GW_HWNDNEXT );
		}
		else
		{
			if (Wrap)
			{
				next = GetWindow( win, GW_HWNDFIRST );
			}
			else
			{
				return 0;
			}
		}
	}
	ATLASSERT(!Wrap || next);
	return next;
}

void SmartEnableWindow(_In_ const HWND Win, _In_ const BOOL Enable)
{
	ATLASSERT(Win);
	if (!Enable)
	{
		HWND hasfocus = GetFocus();
		bool FocusProblem = false;
		HWND focuscopy;
		for ( focuscopy = hasfocus; focuscopy; focuscopy = ( GetParent ) ( focuscopy ) )
		{
			if (focuscopy == Win)
			{
				FocusProblem = true;
			}
		}
		if (FocusProblem)
		{
			HWND nextctrl = GetNextDlgItem(Win, true);
			if (nextctrl)
			{
				SetFocus( nextctrl );
			}
		}
	}
	::EnableWindow(Win, Enable);
}

std::wstring GetFilePart(const std::wstring& path)
{
	PCWSTR const pLastSlash = wcsrchr(path.c_str(), '\\');
	if (pLastSlash)
		return pLastSlash + 1;
	// If there's no slash then the file part is the entire string.
	return path;
}

std::wstring GetFileExt(const std::wstring& path)
{
	std::wstring filePart = GetFilePart(path);
	PCWSTR const pLastPeriod = wcsrchr(filePart.c_str(), '.');
	if (pLastPeriod)
		return pLastPeriod;
	return L"";
}

std::wstring GetDirPart(const std::wstring& path)
{
	PCWSTR const pLastSlash = wcsrchr(path.c_str(), '\\');
	if (pLastSlash)
		return path.substr(0, pLastSlash + 1 - path.c_str());
	// If there's no slash then there is no directory.
	return L"";
}

std::wstring CrackFilePart(const std::wstring& path)
{
	std::wstring filePart = GetFilePart(path);
	const std::wstring extension = GetFileExt(filePart);
	if (!extension.empty())
	{
		filePart = filePart.substr(0, filePart.size() - extension.size());
	}

	return filePart;
}

int DeleteOneFile(HWND hwnd, const std::wstring& path)
{
	std::vector<std::wstring> paths;
	paths.push_back(path);
	return DeleteFiles(hwnd, paths);
}

int DeleteFiles(HWND hwnd, const std::vector<std::wstring>& paths)
{
	std::vector<wchar_t> fileNames;
	fileNames.reserve( paths.size( ) );
	for (auto& path : paths)
	{
		// Push the file name and its NULL terminator onto the vector.
		fileNames.insert(fileNames.end(), path.c_str(), path.c_str() + path.size());
		fileNames.push_back(0);
	}

	// Double null-terminate.
	fileNames.push_back(0);

	SHFILEOPSTRUCT fileOp =
	{
		hwnd,
		FO_DELETE,
		&fileNames[0],
		NULL,
		(FOF_ALLOWUNDO bitor FOF_FILESONLY bitor FOF_NOCONFIRMATION),
	};
	// Delete using the recycle bin.
	const int result = SHFileOperation(&fileOp);

	return result;
}

void SetClipboardText(const std::wstring& text)
{
	//If [OpenClipboard] fails, the return value is zero. 
	//To get extended error information, call GetLastError.
	const BOOL cb = OpenClipboard( GetDesktopWindow( ) );
	if (cb == 0)
	{
		const DWORD lastErr = GetLastError( );
		outputPrintf( L"Failed to open clipboard!!\n\tError code: %u\n", lastErr );
		ErrorHandling::outputPrintfErrorDebug( lastErr );
		return;
	}

	//If [EmptyClipboard] fails, the return value is zero. 
	//To get extended error information, call GetLastError.
	const BOOL emptyRes = EmptyClipboard( );
	if (emptyRes == 0)
	{
		//TODO: check this instead of VERIFYing
		VERIFY( CloseClipboard( ) );
		const DWORD lastErr = GetLastError( );
		outputPrintf( L"Failed to empty clipboard!!\n\tError code: %u\n", lastErr );
		ErrorHandling::outputPrintfErrorDebug( lastErr );
		return;
	}

	const size_t length = (text.size() + 1) * sizeof(wchar_t);
	
	//If [GlobalAlloc] fails, the return value is NULL. 
	//To get extended error information, call GetLastError.
	const HANDLE hmem = GlobalAlloc( GMEM_MOVEABLE, length );
	if (hmem == NULL)
	{
		//TODO: check this instead of VERIFYing
		VERIFY( CloseClipboard( ) );
		const DWORD lastErr = GetLastError( );
		outputPrintf( L"GlobalAlloc failed!!\n\tError code: %u\n", lastErr );
		ErrorHandling::outputPrintfErrorDebug( lastErr );
		return;
	}
	
	//If [GlobalLock] fails, the return value is NULL. 
	//To get extended error information, call GetLastError.
	void* const ptr = GlobalLock(hmem);
	if (ptr == NULL)
	{
		//If [GlobalFree] succeeds, the return value is NULL.
		GlobalFree( hmem );

		//TODO: check this instead of VERIFYing
		VERIFY( CloseClipboard( ) );
		const DWORD lastErr = GetLastError( );
		outputPrintf( L"GlobalLock failed!!\n\tError code: %u\n", lastErr );
		ErrorHandling::outputPrintfErrorDebug( lastErr );
		return;
	}

	memcpy(ptr, text.c_str(), length);

	//GlobalUnlock has crazy return values!
	GlobalUnlock(hmem);

	//If [SetClipboardData] fails, the return value is NULL.
	//To get extended error information, call GetLastError.
	VERIFY( SetClipboardData( CF_UNICODETEXT, hmem ) );

	VERIFY( CloseClipboard( ) );
}

int64_t GetFileSize(const std::wstring& path)
{
	LARGE_INTEGER result;
	HANDLE hFile = CreateFileW(
								path.c_str(),
								GENERIC_READ,
								(FILE_SHARE_READ bitor FILE_SHARE_WRITE),
								NULL,
								OPEN_EXISTING,
								FILE_ATTRIBUTE_NORMAL,
								NULL
							 );

	if (hFile == INVALID_HANDLE_VALUE)
		return 0;

	if (GetFileSizeEx(hFile, &result))
	{
		handle_close::closeHandle(hFile);
		return result.QuadPart;
	}
	handle_close::closeHandle(hFile);
	return 0;
}

bool Is64BitWindows()
{
	// http://blogs.msdn.com/b/oldnewthing/archive/2005/02/01/364563.aspx
	BOOL f64 = FALSE;
	bool bIsWin64 = IsWow64Process(GetCurrentProcess(), &f64) && f64;
	return bIsWin64;
}

WindowsVersion GetWindowsVersion()
{
	OSVERSIONINFO verInfo = { sizeof(OSVERSIONINFO) };
	// warning C4996: 'GetVersionExA': was declared deprecated
	// warning C28159: Consider using 'IsWindows*' instead of 'GetVersionExW'.Reason : Deprecated.Use VerifyVersionInfo* or IsWindows* macros from VersionHelpers.
#pragma warning(suppress : 4996)
#pragma warning(suppress : 28159)
	GetVersionEx(&verInfo);

	// Windows 10 preview has major version 10, if you have a compatibility
	// manifest for that OS, which this program should have.
	if (verInfo.dwMajorVersion > 6)
		return kWindowsVersion10;	// Or higher, I guess.

	// Windows 8.1 will only be returned if there is an appropriate
	// compatibility manifest.
	if (verInfo.dwMajorVersion == 6 && verInfo.dwMinorVersion >= 3)
		return kWindowsVersion8_1;

	if (verInfo.dwMajorVersion == 6 && verInfo.dwMinorVersion >= 2)
		return kWindowsVersion8;

	if (verInfo.dwMajorVersion == 6 && verInfo.dwMinorVersion >= 1)
		return kWindowsVersion7;

	if (verInfo.dwMajorVersion == 6)
		return kWindowsVersionVista;

	return kWindowsVersionXP;
}

//bool IsWindowsServer()
//{
//	OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0, { 0 }, 0, 0, 0, VER_NT_WORKSTATION };
//	DWORDLONG        const dwlConditionMask = VerSetConditionMask(0, VER_PRODUCT_TYPE, VER_EQUAL);
//
//	bool result = !VerifyVersionInfoW(&osvi, VER_PRODUCT_TYPE, dwlConditionMask);
//	return result;
//}

double ElapsedTimer::ElapsedSeconds() const
{
	const auto duration = std::chrono::steady_clock::now() - start_;
	const auto microseconds =
		std::chrono::duration_cast<std::chrono::microseconds>(duration);
	
	return microseconds.count() / 1e6;
}


std::wstring FindPython()
{
#pragma warning(suppress:4996)
	PCWSTR path = _wgetenv(L"path");
	if (!path)
	{
		// No python found.
		return L"";
	}

	std::vector<std::wstring> pathParts = split(path, ';');
	for (auto part : pathParts)
	{
		std::wstring pythonPath = part + L"\\python.exe";
		if (PathFileExistsW(pythonPath.c_str()))
		{
			return pythonPath;
		}
	}

	// No python found.
	return L"";
}
