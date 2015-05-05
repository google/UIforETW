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

#include <string>

// This class encapsulates running a child thread and reading
// its output. Typical usage is:
//   ChildProcess child(fullPathToExecutable);
//   std::wstring args = L" -go";
//   child.Run(bShowCommands_, L"exename.exe" + args);
// The destructor will wait for the process to exit,
// calling the global outputPrintf function with output from
// the child process as it arrives. The calling code can
// optionally retrieve the exit code, which also waits for
// the process to exit.
//   DWORD exitCode = child.GetExitCode();

class ChildProcess final
{
public:
	ChildProcess(std::wstring exePath);
	~ChildProcess();

	// Returns true if the process started.
	_Pre_satisfies_( hProcess_ == 0 )
	_Success_( return )
	bool Run(bool showCommand, std::wstring args);

	// This can be called even if the process doesn't start, but
	// it will return zero. If the process is still running it
	// will wait until the process returns and then get the exit code.
	DWORD GetExitCode();

	// Normally all output is printed as it is received. If this function
	// is called after Run() then all output will be returned, and not
	// printed.
	std::wstring GetOutput();

private:
	// Path to the executable to be run, and its process handle.
	std::wstring exePath_;
	// Process, thread, and event handles have an uninitialized state
	// of zero. Pipes and files have an uninitialized state of
	// INVALID_HANDLE_VALUE. Yay Windows!
	HANDLE hProcess_ = 0;

	// This will be signaled when fresh child-process output is available.
	HANDLE hOutputAvailable_ = 0;

	// The processOutput_ string is written to by the listener thread.
	// Don't modify processOutput_ without acquiring the lock.
	CCriticalSection outputLock_;
	//This annotation is commented out because /analyze doesn't
	//properly understand it.
	/*_Guarded_by_( outputLock_ )*/
	std::wstring processOutput_;

	// Output handles for the child process -- connected to the pipe.
	HANDLE hStdOutput_ = INVALID_HANDLE_VALUE;
	HANDLE hStdError_ = INVALID_HANDLE_VALUE;

	// Pipe to read from, and the handle to the pipe reading thread.
	HANDLE hPipe_ = INVALID_HANDLE_VALUE;
	HANDLE hChildThread_ = 0;
	// Thread functions for reading from the pipe.
	static DWORD WINAPI ListenerThreadStatic(LPVOID);
	DWORD ListenerThread();

	// IsStillRunning returns when the child process exits or
	// when new output is available. It returns true if the
	// child is still running.
	bool IsStillRunning();
	// Remove and return the accumulated output text. Typically
	// this is called in an IsStillRunning() loop.
	std::wstring RemoveOutputText();

	// This can be called even if the process doesn't start, but
	// it will just return immediately. Otherwise it will wait
	// for the process to end. This is called by the destructor
	// so calling this is strictly optional.
	void WaitForCompletion(bool printOutput);
};
