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

void SetRegistryDWORD(HKEY root, const std::wstring& subkey, const std::wstring& valueName, DWORD value);
void CreateRegistryKey(HKEY root, const std::wstring& subkey, const std::wstring& newKey);

std::wstring GetEditControlText(HWND hwnd);
std::wstring AnsiToUnicode(const std::string& text);

// Return a string from a format string and some printf-style arguments.
// Maximum output size is 4 K - larger outputs will be truncated.
std::wstring stringPrintf(_Printf_format_string_ const wchar_t* pFormat, ...);
// Call OutputDebugString with a format string and some printf-style arguments.
void debugPrintf(_Printf_format_string_ const wchar_t* pFormat, ...);

// This function checks to see whether a control has focus before
// disabling it. If it does have focus then it moves the focus, to
// avoid breaking keyboard mnemonics.
_Pre_satisfies_(Win != NULL)
void SmartEnableWindow(HWND Win, BOOL Enable);

// Return the string after the final '\' or after the final '.' in
// the file part of a path. If the last character is '\' then GetFilePart
// will return an empty string. If there is no '.' after the last '\'
// then GetFileExt will return an empty string.
std::wstring GetFilePart(const std::wstring& path);
std::wstring GetFileExt(const std::wstring& path);
// Return the path part only, or an empty string if there is no '\'.
// The '\' character is returned.
std::wstring GetDirPart(const std::wstring& path);
// Pass this a path and it returns the pre extension part of
// the file part of the path (which could conceivably be an
// empty string).
std::wstring CrackFilePart(const std::wstring& path);
// Pass this a path and it returns everything except the extension.
std::wstring StripExtensionFromPath(const std::wstring& path);

// Delete one or more files using the shell so that errors will bring up
// a dialog and deleted files will go to the recycle bin.
int DeleteOneFile(HWND hwnd, const std::wstring& path);
int DeleteFiles(HWND hwnd, const std::vector<std::wstring>& paths);
int64_t GetFileSize(const std::wstring& path);

void SetClipboardText(const std::wstring& text);
std::wstring GetClipboardText();

enum WindowsVersion
{
	kWindowsVersionXP,
	kWindowsVersionVista,
	kWindowsVersion7,
	kWindowsVersion8,
	kWindowsVersion8_1,
	kWindowsVersion10,
};

bool Is64BitWindows();
bool Is64BitBuild();
WindowsVersion GetWindowsVersion();

std::wstring FindPython(); // Returns a full path to python.exe or nothing.

class ElapsedTimer
{
public:
	double ElapsedSeconds() const
	{
		auto duration = std::chrono::steady_clock::now() - start_;
		auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
		return microseconds.count() / 1e6;
	}
private:
	std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

std::wstring GetEXEBuildTime();

void SetCurrentThreadName(PCSTR threadName);

void CopyStartupProfiles(const std::wstring& exeDir, bool force);

void CloseValidHandle( _Pre_valid_ _Post_ptr_invalid_ HANDLE handle );

