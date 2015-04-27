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
#include "ChildProcess.h"
#include "Utility.h"
#include "alias.h"
#include <vector>

static const wchar_t* kPipeName = L"\\\\.\\PIPE\\UIforETWPipe";

ChildProcess::ChildProcess(std::wstring exePath)
	: exePath_(std::move(exePath))
{
	// Create the pipe here so that it is guaranteed to be created before
	// we try starting the process.
	hPipe_ = CreateNamedPipeW(kPipeName,
		(PIPE_ACCESS_DUPLEX bitor PIPE_TYPE_BYTE bitor PIPE_READMODE_BYTE),
		PIPE_WAIT,
		1,
		1024 * 16,
		1024 * 16,
		NMPWAIT_USE_DEFAULT_WAIT,
		NULL);
	hChildThread_ = CreateThread(0, 0, ListenerThreadStatic, this, 0, 0);

	hOutputAvailable_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

ChildProcess::~ChildProcess()
{
	if (hProcess_)
	{
		const DWORD exitCode = GetExitCode();
		if (exitCode)
		{
			outputPrintf( L"Process exit code was %08x (%lu)\n", exitCode, exitCode );
		}
		handle_close::closeHandle(hProcess_);
	}
	if (hOutputAvailable_)
	{
		handle_close::closeHandle(hOutputAvailable_);
	}
}

bool ChildProcess::IsStillRunning()
{
	HANDLE handles[] =
	{
		hProcess_,
		hOutputAvailable_,
	};
	DWORD waitIndex = WaitForMultipleObjects(ARRAYSIZE(handles), &handles[0], FALSE, INFINITE);
	// Return true if hProcess_ was not signaled.
	return waitIndex != 0;
}

std::wstring ChildProcess::RemoveOutputText()
{
	CSingleLock locker( &outputLock_ );
	std::wstring result = processOutput_;
	processOutput_ = L"";
	return result;
}

DWORD WINAPI ChildProcess::ListenerThreadStatic(LPVOID pVoidThis)
{
	ChildProcess* pThis = static_cast<ChildProcess*>(pVoidThis);
	return pThis->ListenerThread();
}

DWORD ChildProcess::ListenerThread()
{
	// wait for someone to connect to the pipe
	if (ConnectNamedPipe(hPipe_, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)
	{
		// Acquire the lock while writing to processOutput_
		char buffer[1024];
		DWORD dwRead;
		while (ReadFile(hPipe_, buffer, sizeof(buffer) - 1, &dwRead, NULL) != FALSE)
		{
			if (dwRead > 0)
			{
				CSingleLock locker( &outputLock_ );
				buffer[dwRead] = 0;
				OutputDebugStringA(buffer);
				processOutput_ += AnsiToUnicode(buffer);
			}
			SetEvent(hOutputAvailable_);
		}
	}
	else
	{
		OutputDebugStringW(L"Connect failed.\n");
	}

	DisconnectNamedPipe(hPipe_);

	return 0;
}

_Pre_satisfies_( hProcess_ == 0 )
_Success_( return )
bool ChildProcess::Run(bool showCommand, std::wstring args)
{
	ATLASSERT(!hProcess_);

	if (showCommand)
	{
		outputPrintf( L"%s\n", args.c_str( ) );
	}

	SECURITY_ATTRIBUTES security = { sizeof(security), 0, TRUE };

	hStdOutput_ = CreateFileW(kPipeName, GENERIC_WRITE, 0, &security,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, INVALID_HANDLE_VALUE);
	if (hStdOutput_ == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	const HANDLE currentProcess = GetCurrentProcess( );
	if (!DuplicateHandle( currentProcess, hStdOutput_, currentProcess,
		&hStdError_, 0, TRUE, DUPLICATE_SAME_ACCESS ))
	{
		return false;
	}
	STARTUPINFO startupInfo = {};
	startupInfo.hStdOutput = hStdOutput_;
	startupInfo.hStdError = hStdError_;
	startupInfo.hStdInput = INVALID_HANDLE_VALUE;
	startupInfo.dwFlags = STARTF_USESTDHANDLES;

	PROCESS_INFORMATION processInfo = {};
	DWORD flags = CREATE_NO_WINDOW;

	// Wacky CreateProcess rules say args has to be writable!
	std::vector<wchar_t> argsCopy(args.size() + 1);

	//wcscpy_s(&argsCopy[0], argsCopy.size(), args.c_str());
	const HRESULT strCpyResult = StringCchCopyNW( &argsCopy[ 0 ], argsCopy.size( ), args.c_str( ), args.length( ) );
	if (FAILED( strCpyResult ))
	{
		ATLASSERT( strCpyResult == STRSAFE_E_INSUFFICIENT_BUFFER );
		outputPrintf( L"Failed to copy arguments into writable buffer!\n" );
		debug::Alias( &argsCopy );
		debug::Alias( &args );
		debug::Alias( &strCpyResult );
		std::terminate( );
	}

	const BOOL success = CreateProcessW(exePath_.c_str(	), &argsCopy[0], NULL, NULL,
		TRUE, flags, NULL, NULL, &startupInfo, &processInfo);
	if (success)
	{
		handle_close::closeHandle(processInfo.hThread);
		hProcess_ = processInfo.hProcess;
		return true;
	}
	else
	{
		outputPrintf(L"Error %u starting %s, %s\n", GetLastError(), exePath_.c_str(), args.c_str());
	}

	return false;
}

DWORD ChildProcess::GetExitCode()
{
	if (!hProcess_)
		return 0;
	// Don't allow getting the exit code unless the process has exited.
	WaitForCompletion(true);
	DWORD result;
	(void)GetExitCodeProcess(hProcess_, &result);
	return result;
}

std::wstring ChildProcess::GetOutput()
{
	if (!hProcess_)
		return L"";
	WaitForCompletion(false);
	return RemoveOutputText();
}

void ChildProcess::WaitForCompletion(bool printOutput)
{
	if (hProcess_)
	{
		// This looks like a busy loop, but it isn't. IsStillRunning()
		// waits until the process exits or sends more output, so this
		// is actually an idle loop.
		while (IsStillRunning())
		{
			if (printOutput)
			{
				std::wstring output = RemoveOutputText();
				outputPrintf(L"%s", output.c_str());
			}
		}
		// This isn't technically needed, but removing it would make
		// me nervous.
		WaitForSingleObject(hProcess_, INFINITE);
	}

	// Once the process is finished we have to close the stderr/stdout
	// handles so that the listener thread will exit. We also have to
	// close these if the process never started.
	if (hStdError_ != INVALID_HANDLE_VALUE)
	{
		handle_close::closeHandle(hStdError_);
		hStdError_ = INVALID_HANDLE_VALUE;
	}
	if (hStdOutput_ != INVALID_HANDLE_VALUE)
	{
		handle_close::closeHandle(hStdOutput_);
		hStdOutput_ = INVALID_HANDLE_VALUE;
	}

	// Wait for the listener thread to exit.
	if (hChildThread_)
	{
		WaitForSingleObject(hChildThread_, INFINITE);
		handle_close::closeHandle(hChildThread_);
		hChildThread_ = 0;
	}

	// Clean up.
	if (hPipe_ != INVALID_HANDLE_VALUE)
	{
		handle_close::closeHandle(hPipe_);
		hPipe_ = INVALID_HANDLE_VALUE;
	}

	if (printOutput)
	{
		// Now that the child thread has exited we can finally read
		// the last of the child-process output.
		std::wstring output = RemoveOutputText();
		if ( !output.empty( ) )
		{
			outputPrintf( L"%s", output.c_str( ) );
		}
	}
}
