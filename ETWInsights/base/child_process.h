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

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <mutex>
#include <string>

#include "base/base.h"

namespace base {

// This class encapsulates running a child process and writing its output to a
// file. Typical usage is:
//   ChildProcess child();
//   child.SetOutputPath(L"output.txt");
//   child.Run(L"exename.exe -arg value");
//   child.WaitForCompletion();
// Run returns immediately. The destructor, GetExitCode(), and
// WaitForCompletion() will all wait for the process to exit.

class ChildProcess {
 public:
  ChildProcess();
  // This waits for the child process to terminate.
  ~ChildProcess();

  // Sets the path to which output will be written.
  void SetOutputPath(const std::wstring& output_path) {
    output_path_ = output_path;
  }

  // Returns true if the process started. This function returns
  // immediately without waiting for process completion.
  _Pre_satisfies_(!(this->hProcess_)) bool Run(const std::wstring& command);

  // This can be called even if the process doesn't start, but it will return
  // zero. If the process is still running it will wait until the process
  // returns and then get the exit code.
  DWORD GetExitCode();

  // Waits for the process to complete its execution. This is called by the
  // destructor so calling it is strictly optional.
  void WaitForCompletion();

 private:
  // Process, thread, and event handles have an uninitialized state of zero.
  // Files have an uninitialized state of INVALID_HANDLE_VALUE. Yay Windows!
  HANDLE hProcess_ = 0;

  // Output handles for the child process.
  HANDLE hStdOutput_ = INVALID_HANDLE_VALUE;
  HANDLE hStdError_ = INVALID_HANDLE_VALUE;
  HANDLE hStdInput_ = INVALID_HANDLE_VALUE;

  // Output path.
  std::wstring output_path_;

  DISALLOW_COPY_AND_ASSIGN(ChildProcess);
};

}  // namespace base
