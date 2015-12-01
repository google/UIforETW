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
#include <direct.h>
#include <ShlObj.h>


namespace {
PCWSTR const kWPAStartupFileName = L"Startup.wpaProfile";
PCWSTR const kChromeRegionsFileName = L"chrome_regions_of_interest.xml";

void UnlockGlobalMemory(_In_ const HGLOBAL hmem)
{
	const BOOL unlockRes = ::GlobalUnlock(hmem);
	if (!unlockRes)
	{
		UIETWASSERT(::GetLastError() == NO_ERROR);
	}
}

std::wstring GetDocumentsFolderPath()
{
	ATL::CComHeapPtr<_Null_terminated_ wchar_t> docsPathTemp;
	const HRESULT docsPathResult =
		::SHGetKnownFolderPath(
		FOLDERID_Documents, KF_FLAG_NO_ALIAS, NULL, &docsPathTemp.m_pData);
	if (FAILED(docsPathResult))
	{
		debugPrintf(L"SHGetKnownFolderPath (for Documents) failed to retrieve the path.\n");
		std::terminate();
	}
	return docsPathTemp.m_pData;
}

void copyFileToDocumentsWPA(PCWSTR const fileName, const std::wstring& exeDir, const bool force)
{
	// First copy the WPA 8.1 startup.wpaProfile file
	std::wstring docsPath(GetDocumentsFolderPath());

	if (docsPath.empty())
	{
		outputPrintf(L"Failed to copy %s to documents. See debugger output for details.\n", fileName);
		return;
	}

	const std::wstring source = exeDir + fileName;
	const std::wstring destDir = docsPath + std::wstring(L"\\WPA Files");
	const std::wstring dest = destDir + L"\\" + fileName;
	const BOOL destinationExists = ::PathFileExistsW(dest.c_str());
	if (force || !destinationExists)
	{
		ATLVERIFY(::CreateDirectoryW(destDir.c_str(), NULL) || ::GetLastError() == ERROR_ALREADY_EXISTS);
		const BOOL copyResult = ::CopyFileW(source.c_str(), dest.c_str(), FALSE);
		if (copyResult)
		{
			if (force)
				outputPrintf(L"Copied %s to the WPA Files directory.\n", fileName);
			return;
		}
		if (force)
		{
			outputPrintf(L"Failed to copy %s to the WPA Files directory.\n", fileName);
			outputLastError();
			return;
		}
	}
}

void copyWPAProfileToLocalAppData(const std::wstring& exeDir, const bool force)
{
	PCWSTR const localAppDataEnvVar = L"localappdata";
	// Then copy the WPA 10 startup.wpaProfile file
	const std::wstring localAppData = GetEnvironmentVariableString(localAppDataEnvVar);
	if (localAppData.empty())
	{
		outputPrintf(L"the `%s` environment variable didn't contain a valid path. Failed to copy WPA 10 profile.\n", localAppDataEnvVar);
		return;
	}
	std::wstring source = exeDir + L"startup10.wpaProfile";
	std::wstring destDir = std::wstring(localAppData) + L"\\Windows Performance Analyzer\\";
	std::wstring dest = destDir + kWPAStartupFileName;
	if (force || !::PathFileExistsW(dest.c_str()))
	{

		ATLVERIFY(::CreateDirectoryW(destDir.c_str(), NULL) || ::GetLastError() == ERROR_ALREADY_EXISTS);

		if (::CopyFileW(source.c_str(), dest.c_str(), FALSE))
		{
			if (force)
				outputPrintf(L"%s", L"Copied Startup.10wpaProfile to %localappdata%\\Windows Performance Analyzer\n");
			return;
		}
		if (force)
		{
			outputPrintf(L"%s", L"Failed to copy Startup.10wpaProfile to %localappdata%\\Windows Performance Analyzer\n");
			outputLastError();
		}
	}

}
} // namespace {

void outputLastError(const DWORD lastErr)
{
	const DWORD errMsgSize = 1024u;
	wchar_t errBuff[errMsgSize] = {};
	const DWORD ret = ::FormatMessageW(
		(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS),
		NULL, lastErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		errBuff, errMsgSize, NULL);

	if (ret == 0)
		return; // FormatMessageW failed.
	outputPrintf(errBuff);
}

void debugLastError(const DWORD lastErr)
{
	const DWORD errMsgSize = 1024u;
	wchar_t errBuff[errMsgSize] = {};
	const DWORD ret = ::FormatMessageW(
		(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS),
		NULL, lastErr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		errBuff, errMsgSize, NULL);

	if (ret == 0)
		return; // FormatMessageW failed.
	debugPrintf(L"UIforETW encountered an error: %s\r\n", errBuff);
}

std::vector<std::wstring> split(const std::wstring& s, const char c)
{
	std::wstring::size_type i = 0;
	std::wstring::size_type j = s.find(c);

	std::vector<std::wstring> result;
	result.reserve(s.length());
	while (j != std::wstring::npos)
	{
		result.emplace_back(s.substr(i, j - i));
		i = ++j;
		j = s.find(c, j);
	}

	if (!s.empty())
		result.emplace_back(s.substr(i, s.length()));

	return result;
}

std::vector<std::wstring> GetFileList(const std::wstring& pattern, const bool fullPaths)
{
	const std::wstring directory = (fullPaths ? GetDirPart(pattern) : L"");

	// may not pass an empty string to FindFirstFileEx
	UIETWASSERT(pattern.length() > 0);

	// string passed to FindFirstFileEx must not end in a backslash
	UIETWASSERT(pattern.back() != L'\\');

	WIN32_FIND_DATA findData;
	HANDLE hFindFile = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic,
				&findData, FindExSearchNameMatch, NULL, 0);
	// Call GetLastError() here because on VC++ 2015 debug builds the memory
	// allocations in the result constructor zero the last error value.
	DWORD lastError = ::GetLastError();

	std::vector<std::wstring> result;
	if (hFindFile == INVALID_HANDLE_VALUE)
	{
		// If there are NO matching files, then FindFirstFileExW returns
		// INVALID_HANDLE_VALUE and the last error is ERROR_FILE_NOT_FOUND.
		UIETWASSERT(lastError == ERROR_FILE_NOT_FOUND);
		return result;
	}
	do
	{
		result.emplace_back(directory + findData.cFileName);
	} while (::FindNextFileW(hFindFile, &findData));

	UIETWASSERT(::GetLastError() == ERROR_NO_MORE_FILES);
	ATLVERIFY(::FindClose(hFindFile));
	return result;
}

// Load a file and convert to a string. If the file contains
// an embedded NUL then the resulting string will be truncated.
std::wstring LoadFileAsText(const std::wstring& fileName)
{
	if (!::PathFileExistsW(fileName.c_str()))
		return L"";

	std::ifstream f;
	f.open(fileName, std::ios_base::binary);
	if (!f)
	{
		outputPrintf(L"Failed to load file `%s` as text - f.open failed!\n", fileName.c_str());
		return L"";
	}
	// Find the file length.
	f.seekg(0, std::ios_base::end);
	const std::streamoff len_int = f.tellg();
	if (len_int == -1)
	{
		outputPrintf(L"Failed to load file `%s` as text - f.tellg failed!\n", fileName.c_str());
		return L"";
	}
	const size_t length = static_cast<size_t>(len_int);
	f.seekg(0, std::ios_base::beg);

	// Allocate a buffer and read the file.
	std::vector<char> data(length + 2);
	f.read(&data[0], length);
	if (!f)
	{
		outputPrintf(L"Failed to load file `%s` as text - f.read failed!\n", fileName.c_str());
		return L"";
	}

	// Add a multi-byte null terminator.
	data[length] = 0;
	data[length+1] = 0;

	const wchar_t bom = 0xFEFF;
	UIETWASSERT(data.size() > sizeof(bom));
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

std::wstring ConvertToCRLF(const std::wstring& input)
{
	std::wstring result;
	result.reserve(input.size());

	for (wchar_t c : input)
	{
		// Replace '\n' with '\r\n' and ignore any '\r' characters that were
		// previously present.
		// Checking for '\n' and '\r' is safe even when dealing with surrogate
		// pairs because the high and low surrogate have non-overlapping ranges.
		if (c == '\n')
			result += '\r';
		if (c != '\r')
			result += c;
	}
	return result;
}

void SetRegistryDWORD(const HKEY root, const std::wstring& subkey, const std::wstring& valueName, const DWORD value)
{
	HKEY key;
	if (::RegOpenKeyExW(root, subkey.c_str(), 0, KEY_ALL_ACCESS, &key) != ERROR_SUCCESS)
		return;
	ATLVERIFY(ERROR_SUCCESS == ::RegSetValueExW(key, valueName.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)));
	ATLVERIFY(::RegCloseKey(key) == ERROR_SUCCESS);
}

void CreateRegistryKey(const HKEY root, const std::wstring& subkey, const std::wstring& newKey)
{
	HKEY key;
	if (::RegOpenKeyExW(root, subkey.c_str(), 0, KEY_ALL_ACCESS, &key) != ERROR_SUCCESS)
		return;
	HKEY resultKey;
	// TODO: RegCreateKey is deprecated.
	const LONG createResult = ::RegCreateKeyW(key, newKey.c_str(), &resultKey);
	UIETWASSERT(createResult == ERROR_SUCCESS);
	if (createResult == ERROR_SUCCESS)
	{
		ATLVERIFY(::RegCloseKey(resultKey) == ERROR_SUCCESS);
	}

	ATLVERIFY(::RegCloseKey(key) == ERROR_SUCCESS);
}

std::wstring GetEditControlText(const HWND hEdit)
{
	const int length = ::GetWindowTextLengthW(hEdit);
	std::vector<wchar_t> buffer(length + 1);

	// GetWindowText https://msdn.microsoft.com/en-us/library/windows/desktop/ms633520.aspx
	// If [GetWindowTextW] succeeds, the return value is the length,
	// in characters, of the copied string, not including the
	// terminating null character.

	// If the window has no title bar or text,
	// [or] if the title bar is empty,
	// or if the window or control handle is invalid,
	// the return value is zero.

	const int charsWritten = ::GetWindowTextW(hEdit, &buffer[0], static_cast<int>(buffer.size()));
	if (charsWritten == 0)
	{
		if (length > 0)
			debugLastError();
		return L"";
	}
	UIETWASSERT(charsWritten == length);
	UIETWASSERT((charsWritten) < (int)buffer.size());
	UIETWASSERT(buffer[charsWritten] == 0);
	// Double-verify that the buffer is null-terminated.
	buffer[buffer.size() - 1] = 0;
	return &buffer[0];
}

// MultiByteToWideChar: https://msdn.microsoft.com/en-us/library/windows/desktop/dd319072.aspx
//
// Remarks:
// As mentioned in the caution above,
// the output buffer can easily be overrun
// if this function is not first called with cchWideChar set to 0
// in order to obtain the required size.
int RequiredNumberOfWideChars(const std::string& text)
{
	static_assert(sizeof(std::string::value_type) == 1 == sizeof(text[0]),
		"bad assumptions!");

	const int multiCharCount = ::MultiByteToWideChar(CP_ACP, 0, text.c_str(),
		static_cast<int>(text.size() + 1), NULL, 0);

	if (multiCharCount == 0)
	{
		// No reasonable way for MultiByteToWideChar to fail.
		debugLastError();
		std::terminate();
	}
	UIETWASSERT(multiCharCount > 0);
	return multiCharCount;
}

std::wstring AnsiToUnicode(const std::string& text)
{
	// If the string is empty, then we can return early, and avoid
	// confusing return values (from MultiByteToWideChar)
	if (text.empty())
		return L"";

	// Determine number of wide characters to be allocated for the
	// Unicode string.
	const int multiCharCount = RequiredNumberOfWideChars(text);

	std::vector<wchar_t> buffer(multiCharCount);

	// Convert to Unicode.
	const int multiToWideResult = ::MultiByteToWideChar(
		CP_ACP, 0, text.c_str(), static_cast<int>(text.size() + 1),
		&buffer[0], multiCharCount);

	if (multiToWideResult == 0)
	{
		// No reasonable way for MultiByteToWideChar to fail.
		debugLastError();
		std::terminate();
	}

	UIETWASSERT(multiToWideResult > 0);
	UIETWASSERT(buffer[multiToWideResult - 1] == 0);

	// Double-verify that the buffer is null-terminated.
	buffer[buffer.size() - 1] = 0;
	std::wstring result = &buffer[0];
	return result;
}

// Return a string from a format string and some printf-style arguments.
std::wstring stringPrintf(_Printf_format_string_ PCWSTR const pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	// 4 K should be enough for anyone and it keeps this function simple.
	wchar_t buffer[4096];
	_vsnwprintf_s(buffer, _TRUNCATE, pFormat, args);
	va_end(args);
	return buffer;
}

// Call OutputDebugString with a format string and some printf-style arguments.
void debugPrintf(_Printf_format_string_ PCWSTR const pFormat, ...)
{
	va_list args;
	va_start(args, pFormat);
	// 1 K maximum is imposed by OutputDebugString.
	wchar_t buffer[1024];
	_vsnwprintf_s(buffer, _TRUNCATE, pFormat, args);
	va_end(args);
	OutputDebugStringW(buffer);
}

// Get the next/previous dialog item (next/prev in window order and tab order) allowing
// for disabled controls, invisible controls, and wrapping at the end of the tab order.

static bool ControlOK(const HWND win)
{
	if (!win)
		return false;
	if (!::IsWindowEnabled(win))
		return false;
	if (!(::GetWindowLong(win, GWL_STYLE) & WS_TABSTOP))
		return false;
	// You have to check for visibility of the parent window because during dialog
	// creation the parent window is invisible, which renders the child windows
	// all invisible - not good.
	const HWND parent = ::GetParent(win);
	UIETWASSERT(parent);
	if (!::IsWindowVisible(win) && ::IsWindowVisible(parent))
		return false;
	return true;
}

static HWND GetNextDlgItem(const HWND win, const bool Wrap)
{
	HWND next = ::GetWindow(win, GW_HWNDNEXT);
	while ((next != win) && (!::ControlOK(next)))
	{
		if (next)
			next = ::GetWindow(next, GW_HWNDNEXT);
		else
		{
			if (Wrap)
				next = ::GetWindow(win, GW_HWNDFIRST);
			else
				return 0;
		}
	}
	UIETWASSERT(!Wrap || next);
	return next;
}

_Pre_satisfies_(Win != NULL)
void SmartEnableWindow(const HWND Win, const BOOL Enable)
{
	UIETWASSERT(Win);
	if (!Enable)
	{
		bool FocusProblem = false;
		for (HWND focuscopy = ::GetFocus(); focuscopy; focuscopy = ::GetParent(focuscopy))
		{
			if (focuscopy == Win)
				FocusProblem = true;
		}

		if (FocusProblem)
		{
			HWND nextctrl = ::GetNextDlgItem(Win, true);
			if (nextctrl)
				::SetFocus(nextctrl);
		}
	}
	::EnableWindow(Win, Enable);
}

std::wstring GetFilePart(const std::wstring& path)
{
	UIETWASSERT(path.size() > 0);
	const size_t lastSlash = path.find_last_of(L'\\');
	if (lastSlash != std::wstring::npos)
		return path.substr(lastSlash);
	// If there's no slash then the file part is the entire string.
	return path;
}

std::wstring GetFileExt(const std::wstring& path)
{
	const std::wstring filePart = GetFilePart(path);
	const size_t lastPeriod = filePart.find_last_of(L'.');
	if (lastPeriod != std::wstring::npos)
		return filePart.substr(lastPeriod);
	// If there's no period then there's no extension.
	return L"";
}

std::wstring GetDirPart(const std::wstring& path)
{
	UIETWASSERT(path.size() > 0);
	const size_t lastSlash = path.find_last_of(L'\\');
	if (lastSlash != std::wstring::npos)
	{
		return path.substr(0, lastSlash+1);
	}
	// If there's no slash then there is no directory.
	return L"";
}

std::wstring CrackFilePart(const std::wstring& path)
{
	const std::wstring filePart = GetFilePart(path);
	const std::wstring extension = GetFileExt(filePart);
	UIETWASSERT(filePart.size() >= extension.size());
	if (!extension.empty())
		return filePart.substr(0, filePart.size() - extension.size());
	return filePart;
}

std::wstring StripExtensionFromPath(const std::wstring& path)
{
	UIETWASSERT(path.size() > 0);
	const std::wstring ext = GetFileExt(path);
	UIETWASSERT(path.size() >= ext.size());
	return path.substr(0, path.size() - ext.size());
}

std::wstring CanonicalizePath(const std::wstring& path)
{
	// The PathCanonicalize function says that the maximum supported
	// input length is MAX_PATH, so check for that.
	if (path.size() >= MAX_PATH)
		return L"";
	WCHAR output[MAX_PATH];
	PathCanonicalize(output, path.c_str());
	return output;
}

int DeleteOneFile(const HWND hwnd, const std::wstring& path)
{
	// {path} uses std::vector list initialization
	return DeleteFiles(hwnd, {path});
}

int DeleteFiles(const HWND hwnd, const std::vector<std::wstring>& paths)
{
	UIETWASSERT(paths.size() > 0);

	std::vector<wchar_t> fileNames;
	for (const auto& path : paths)
	{
		// Push the file name and its NULL terminator onto the vector.
		fileNames.insert(fileNames.end(), path.c_str(), path.c_str() + path.size());
		fileNames.emplace_back(0);
	}

	// Double null-terminate.
	fileNames.emplace_back(0);

	SHFILEOPSTRUCT fileOp =
	{
		hwnd,
		FO_DELETE,
		&fileNames[0],
		NULL,
		FOF_ALLOWUNDO | FOF_FILESONLY | FOF_NOCONFIRMATION,
	};
	// Delete using the recycle bin.
	// TODO: IFileOperation?
	return ::SHFileOperationW(&fileOp);
}

void SetClipboardText(const std::wstring& text)
{
	if (!::OpenClipboard(::GetDesktopWindow()))
		return;

	ATLVERIFY(::EmptyClipboard());

	const size_t length = (text.size() + 1) * sizeof(text[0]);
	HANDLE hmem = ::GlobalAlloc(GMEM_MOVEABLE, length);
	UIETWASSERT(hmem); // We are not hardened against OOM.

	void* const ptr = ::GlobalLock(hmem);
	UIETWASSERT(ptr != NULL);

	wcscpy_s(static_cast<wchar_t*>(ptr), (text.size() + 1), text.c_str());

	UnlockGlobalMemory(hmem);
	if (::SetClipboardData(CF_UNICODETEXT, hmem) == NULL)
	{
		ATLVERIFY(!::GlobalFree(hmem));
		ATLVERIFY(::CloseClipboard());
		return;
	}
	ATLVERIFY(::CloseClipboard());
}

std::wstring GetClipboardText()
{
	std::wstring result;
	if (!::OpenClipboard(::GetDesktopWindow()))
		return result;

	HANDLE hClip = ::GetClipboardData(CF_UNICODETEXT);
	if (!hClip)
		return result;

	void* const ptr = ::GlobalLock(hClip);
	UIETWASSERT(ptr != NULL);
	const size_t bytes = ::GlobalSize(hClip);
	PCWSTR const text = static_cast<PCWSTR>(ptr);
	result.insert(result.begin(), text, text + (bytes / sizeof(text[0])));
	UnlockGlobalMemory(hClip);
	ATLVERIFY(::CloseClipboard());
	return result;
}

int64_t GetFileSize(const std::wstring& path)
{
	LARGE_INTEGER result = {};
	HANDLE hFile = ::CreateFileW(path.c_str(), GENERIC_READ,
		(FILE_SHARE_READ | FILE_SHARE_WRITE), NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		debugPrintf(L"Failed to get file size!\n");
		debugLastError();
		return 0;
	}

	const BOOL gfsResult = ::GetFileSizeEx(hFile, &result);
	CloseValidHandle(hFile);
	if (gfsResult)
		return result.QuadPart;
	return 0;
}

bool Is64BitWindows()
{
#if defined(_WIN64)
	return true;
#else
	// http://blogs.msdn.com/b/oldnewthing/archive/2005/02/01/364563.aspx
	BOOL f64 = FALSE;
	return ::IsWow64Process(::GetCurrentProcess(), &f64) && f64;
#endif
}

bool Is64BitBuild()
{
#if defined(_WIN64)
	return true;
#else
	return false;
#endif
}

bool IsWindowsTenOrGreater()
{
	return IsWindowsVersionOrGreater(10, 0, 0);
}

bool IsWindowsXPOrLesser()
{
	return !IsWindowsVistaOrGreater();
}

bool IsWindowsSevenOrLesser()
{
	return !IsWindows8OrGreater();
}

bool IsWindowsVistaOrLesser()
{
	return !IsWindows7OrGreater();
}

std::wstring GetEnvironmentVariableString(_In_z_ PCWSTR const variable)
{
	std::vector<wchar_t> buffer(500);
	rsize_t sizeRequired = 0;
	// The _wgetenv_s are supposed to be more secure than the regular versions,
	// although I really don't see the advantage. They are certainly less convenient
	// than they could be, which certainly helps to discourage their use. They should
	// return a std::wstring.
	errno_t result = _wgetenv_s(&sizeRequired, &buffer[0], buffer.size(), variable);
	if (result == ERANGE)
	{
		buffer.resize(sizeRequired);
		result = _wgetenv_s(&sizeRequired, &buffer[0], buffer.size(), variable);
	}
	if (result == 0)
		return &buffer[0];
	return L"";
}

std::wstring FindPython()
{
	const std::wstring pytwoseven = GetEnvironmentVariableString(L"python27");

	// Some people, like me, (Alexander Riccio) have an environment variable
	// that specifically points to Python 2.7.
	// As a workaround for issue #13, we'll use that version of Python.
	// See the issue: https://github.com/google/UIforETW/issues/13
	if (!pytwoseven.empty())
		return pytwoseven;
	const std::wstring path = GetEnvironmentVariableString(L"path");
	if (path.empty())
	{
		// No python found.
		return L"";
	}

	const std::vector<std::wstring> pathParts = split(path, ';');
	// First look for python.exe. If that isn't found then look for
	// python.bat, part of Chromium's depot_tools
	for (const auto& exeName : { L"\\python.exe", L"\\python.bat" })
	{
		for (const auto& part : pathParts)
		{
			const std::wstring pythonPath = part + exeName;
			if (::PathFileExistsW(pythonPath.c_str()))
				return pythonPath;
		}
	}
	// No python found.
	return L"";
}

std::wstring GetBuildTimeFromAddress(_In_ const void* const codeAddress)
{
	// Get the base of the address reservation. This lets this
	// function be passed any function or global variable address
	// in a DLL or EXE.
	MEMORY_BASIC_INFORMATION memoryInfo;
	if (::VirtualQuery(codeAddress, &memoryInfo, sizeof(memoryInfo)) != sizeof(memoryInfo))
	{
		UIETWASSERT(0);
		return L"";
	}
	const void* const ModuleHandle = memoryInfo.AllocationBase;

	// Walk the PE data structures to find the link time stamp.
	const IMAGE_DOS_HEADER* const DosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(ModuleHandle);
	if (IMAGE_DOS_SIGNATURE != DosHeader->e_magic)
	{
		UIETWASSERT(0);
		return L"";
	}
	const IMAGE_NT_HEADERS* const NTHeader =
		reinterpret_cast<const IMAGE_NT_HEADERS*>(
			reinterpret_cast<const char*>(DosHeader) + DosHeader->e_lfanew);
	if (IMAGE_NT_SIGNATURE != NTHeader->Signature)
	{
		UIETWASSERT(0);
		return L"";
	}

	// TimeDateStamp is 32 bits and time_t is 64 bits. That will have to be dealt
	// with when TimeDateStamp wraps in February 2106.
	const time_t timeDateStamp = NTHeader->FileHeader.TimeDateStamp;
	tm linkTime;
	gmtime_s(&linkTime, &timeDateStamp);
	// Print out the module information. The %.24s is necessary to trim
	// the new line character off of the date string returned by asctime().
	// _wasctime_s requires a 26-character buffer.
	wchar_t ascTimeBuf[26];
	_wasctime_s(ascTimeBuf, &linkTime);
	wchar_t	buffer[100];
	swprintf_s(buffer, L"%.24s GMT (%08lx)", ascTimeBuf, NTHeader->FileHeader.TimeDateStamp);
	// Return buffer+4 because we don't need the day of the week.
	return buffer + 4;
}

std::wstring GetEXEBuildTime()
{
	const HMODULE ModuleHandle = ::GetModuleHandle(nullptr);
	return GetBuildTimeFromAddress(ModuleHandle);
}

// From https://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
	DWORD dwType; // Must be 0x1000.
	LPCSTR szName; // Pointer to name (in user addr space).
	DWORD dwThreadID; // Thread ID (-1=caller thread).
	DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

#pragma warning(push)
#pragma warning(disable: 6320 6322)
// These warnings rarely, if ever, point out a real problem. And,
// they fire on lots of totally legitimate code, including Microsoft's
// own SetThreadName sample code.
// warning C6320 : Exception - filter expression is the constant EXCEPTION_EXECUTE_HANDLER.This might mask exceptions that were not intended to be handled.
// warning C6322 : Empty _except block.

void SetCurrentThreadName(PCSTR const threadName)
{
	const DWORD dwThreadID = ::GetCurrentThreadId();
	const THREADNAME_INFO info = { 0x1000, threadName, dwThreadID, 0 };
	__try
	{
		const DWORD numArguments = sizeof(info) / sizeof(ULONG_PTR);

		::RaiseException(MS_VC_EXCEPTION, 0, numArguments, reinterpret_cast<const ULONG_PTR*>(&info));
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}
}
#pragma warning(pop)

void CopyStartupProfiles(const std::wstring& exeDir, const bool force)
{
	if (force)
		outputPrintf(L"\n");

	// WPA 8.1 stores startup.wpaProfile file in Documents/WPA Files
	copyFileToDocumentsWPA(kWPAStartupFileName, exeDir, force);

	copyFileToDocumentsWPA(kChromeRegionsFileName, exeDir, force);

	copyWPAProfileToLocalAppData(exeDir, force);
}

void CloseValidHandle(_In_ _Pre_valid_ _Post_ptr_invalid_ const HANDLE handle)
{
	ATLVERIFY(::CloseHandle(handle) != 0);
}
