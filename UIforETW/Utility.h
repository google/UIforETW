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

#pragma once

#include <vector>
#include <string>
#include <chrono>

std::vector<std::wstring> split(const std::wstring& s, char c);
// If fullPaths == true then the names returned will be full Paths to the files. Otherwise
// they will just be the file portions.
std::vector<std::wstring> GetFileList(const std::wstring& pattern, bool fullPaths = false);

// Load an ANSI or UTF-16 file into a wstring
std::wstring LoadFileAsText(const std::wstring& fileName);
// Write a wstring as UTF-16.
void WriteTextAsFile(const std::wstring& fileName, const std::wstring& text);

// Convert a string that may have '\n' line endings to '\r\n' line endings.
std::wstring ConvertToCRLF(const std::wstring& input);

// Returns true if something is read.
bool GetRegistryDWORD(const HKEY root, const std::wstring& subkey, const std::wstring& valueName, DWORD* pValue) noexcept;
void SetRegistryDWORD(HKEY root, const std::wstring& subkey, const std::wstring& valueName, DWORD value) noexcept;
void CreateRegistryKey(HKEY root, const std::wstring& subkey, const std::wstring& newKey) noexcept;
std::wstring ReadRegistryString(HKEY root, const std::wstring& subkey, const std::wstring& valueName, bool force32Bit);

std::wstring GetEditControlText(HWND hwnd);
std::wstring AnsiToUnicode(const std::string& text);

// Return a string from a format string and some printf-style arguments.
// Maximum output size is 4 K - larger outputs will be truncated.
std::wstring stringPrintf(_Printf_format_string_ PCWSTR pFormat, ...);
// Call OutputDebugString with a format string and some printf-style arguments.
void debugPrintf(_Printf_format_string_ PCWSTR pFormat, ...) noexcept;
void outputLastError(DWORD lastErr = ::GetLastError());
void debugLastError(DWORD lastErr = ::GetLastError()) noexcept;

// This function checks to see whether a control has focus before
// disabling it. If it does have focus then it moves the focus, to
// avoid breaking keyboard mnemonics.
_Pre_satisfies_(Win != NULL)
void SmartEnableWindow(HWND Win, BOOL Enable) noexcept;

// Return the string after the final '\' or after the final '.' in
// the file part of a path. If the last character is '\' then GetFilePart
// will return an empty string. If there is no '.' after the last '\'
// then GetFileExt will return an empty string.
std::wstring GetFilePart(const std::wstring& path);
std::wstring GetFileExt(const std::wstring& path);
// Return the path part only, or an empty string if there is no '\'.
// The '\' character is returned.
std::wstring GetDirPart(const std::wstring& path);
// Ensures that a non-empty string ends with '\'.
// Empty strings are left untouched.
void EnsureEndsWithDirSeparator(std::wstring& path);
// Pass this a path and it returns the pre extension part of
// the file part of the path (which could conceivably be an
// empty string).
std::wstring CrackFilePart(const std::wstring& path);
// Pass this a path and it returns everything except the extension.
std::wstring StripExtensionFromPath(const std::wstring& path);
// Remove extraneous ..\ entries, etc. This is needed for msiexec
// which expects canonicalized paths.
std::wstring CanonicalizePath(const std::wstring& path);

// Delete one or more files using the shell so that errors will bring up
// a dialog and deleted files will go to the recycle bin.
int DeleteOneFile(HWND hwnd, const std::wstring& path);
int DeleteFiles(HWND hwnd, const std::vector<std::wstring>& paths);
int64_t GetFileSize(const std::wstring& path) noexcept;

void SetClipboardText(const std::wstring& text);
std::wstring GetClipboardText();

std::wstring GetEnvironmentVariableString(_In_z_ PCWSTR variable);

// Get version information out of a PE file. If it fails for any reason then the
// result will be zero. The version number is the four 16-bit parts in the obvious
// order. That is, it is dwFileVersionMS and dwFileVersionLS from VS_FIXEDFILEINFO.
uint64_t GetFileVersion(const std::wstring& path);

bool Is64BitWindows() noexcept;
bool Is64BitBuild() noexcept;
bool IsWindowsTenOrGreater();
bool IsWindowsXPOrLesser();
bool IsWindowsSevenOrLesser();
bool IsWindowsVistaOrLesser();

// Finds an executable in the path.
std::wstring FindInPath(const std::wstring& exeName);

std::wstring FindPython(); // Returns a full path to python.exe, python.bat, or nothing.

// Helpful timer class using trendy C++ 11 features.
class ElapsedTimer final
{
public:
	double ElapsedSeconds() const noexcept
	{
		const auto duration = std::chrono::steady_clock::now() - start_;
		const auto microseconds =
			std::chrono::duration_cast<std::chrono::microseconds>(duration);
		return microseconds.count() / 1e6;
	}
private:
	std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// High-precision timer class using QueryPerformanceCounter.
// This may make ElapsedTimer unnecessary.
class QPCElapsedTimer final
{
public:
	QPCElapsedTimer() noexcept
	{
		QueryPerformanceCounter(&start_);
	}
	double ElapsedSeconds() const noexcept
	{
		LARGE_INTEGER stop;
		QueryPerformanceCounter(&stop);
		LARGE_INTEGER frequency;
		QueryPerformanceFrequency(&frequency);
		return (stop.QuadPart - start_.QuadPart) / static_cast<double>(frequency.QuadPart);
	}
private:
	LARGE_INTEGER start_;
};

// Lock and locker classes, to avoid using the MFC classes which
// bloat non-MFC apps.
class CriticalSection
{
public:
	CriticalSection() noexcept
	{
		InitializeCriticalSection(&cs_);
	}
	~CriticalSection()
	{
		DeleteCriticalSection(&cs_);
	}

	void Lock() noexcept
	{
		EnterCriticalSection(&cs_);
	}
	void Unlock() noexcept
	{
		LeaveCriticalSection(&cs_);
	}

private:
	CRITICAL_SECTION cs_;

	CriticalSection(const CriticalSection&) = delete;
	CriticalSection(const CriticalSection&&) = delete;
	CriticalSection& operator=(const CriticalSection&) = delete;
	CriticalSection& operator=(const CriticalSection&&) = delete;
};

class Locker
{
public:
	Locker(CriticalSection* lock) noexcept
		: lock_(lock)
	{
		lock_->Lock();
	}
	~Locker()
	{
		lock_->Unlock();
	}

private:
	CriticalSection* lock_;

	Locker(const Locker&) = delete;
	Locker(const Locker&&) = delete;
	Locker& operator=(const Locker&) = delete;
	Locker& operator=(const Locker&&) = delete;
};

std::wstring GetEXEBuildTime();

void SetCurrentThreadName(PCSTR threadName) noexcept;

void CopyStartupProfiles(const std::wstring& exeDir, bool force);

void CloseValidHandle(_In_ _Pre_valid_ _Post_ptr_invalid_ HANDLE handle) noexcept;

#ifdef IS_MFC_APP
// Put MFC specific code here
void MoveControl(const CWnd* pParent, CWnd& control, int xDelta, int yDelta);
#endif

// Heap tracing can be enabled for one or two PIDs (can be enabled when they are
// already running, one or more process names (most enable through registry
// prior to process launch), or for a single process which xperf launches.
// These are all useful in different scenarios.
// In the context of Chrome profiling:
//   Profiling a single process (of any type) that is already running can be done
// by specifying the PID of that process or pair of processes.
//   Profiling all Chrome processes can be done by specifying chrome.exe. If this
// specification is done after launching the browser process then this will only
// heap-profile subsequently launched processes, so mostly newly created renderer
// processes, potentially from startup.
//   Profiling the *browser* process from startup can be done by specifying the
// the full patch to the executable, and xperf will then launch this process and
// start heap tracing. Note that this will run Chrome as admin, so be careful.
// These options cannot be used together.

// The heap tracing options can be specified as a semi-colon separated list of
// process IDs (maximum of two) and process names *or* a single fully-qualified
// path name.
struct HeapTracedProcesses
{
	// Space-separated set of process IDs (as required by -Pids), or an empty string.
	std::wstring processIDs;
	// Vector of process names, including .exe but not any path information.
	std::vector<std::wstring> processNames;
	// A single fully qualified executable which xperf will launch when tracing
	// starts.
	std::wstring pathName;
};

// Parse the semi-colon separated heap trace settings
HeapTracedProcesses ParseHeapTracingSettings(std::wstring heapTracingExes);

// This should really be called from a background thread to avoid UI hangs.
void OpenFolderAndSelectItem(std::wstring filename, std::wstring dir);
