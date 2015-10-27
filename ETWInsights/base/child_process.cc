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

#include "base/child_process.h"
#include "base/error_string.h"
#include "base/logging.h"
#include "base/string_utils.h"

#include <vector>

namespace base {

ChildProcess::ChildProcess() {}

ChildProcess::~ChildProcess() {
  if (hProcess_) {
    DWORD exitCode = GetExitCode();
    if (exitCode)
      LOG(ERROR) << "Process exit code was " << exitCode << std::endl;
    CloseHandle(hProcess_);
  }
}

_Pre_satisfies_(!(this->hProcess_)) bool ChildProcess::Run(
    const std::wstring& command) {
  DCHECK(!hProcess_);

  if (output_path_.empty())
    return false;

  SECURITY_ATTRIBUTES security = {sizeof(security), 0, TRUE};

  hStdOutput_ =
      CreateFile(output_path_.c_str(), GENERIC_WRITE, 0, &security,
                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, INVALID_HANDLE_VALUE);

  if (hStdOutput_ == INVALID_HANDLE_VALUE)
    return false;
  if (!DuplicateHandle(GetCurrentProcess(), hStdOutput_, GetCurrentProcess(),
                       &hStdError_, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
    return false;
  }
  // Keep Python happy by giving it a valid hStdInput handle.
  if (!DuplicateHandle(GetCurrentProcess(), hStdOutput_, GetCurrentProcess(),
                       &hStdInput_, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
    return false;
  }

  STARTUPINFO startupInfo = {};
  startupInfo.hStdOutput = hStdOutput_;
  startupInfo.hStdError = hStdError_;
  startupInfo.hStdInput = hStdInput_;
  startupInfo.dwFlags = STARTF_USESTDHANDLES;

  PROCESS_INFORMATION processInfo = {};
  DWORD flags = CREATE_NO_WINDOW;
  // Wacky CreateProcess rules say command has to be writable!
  std::vector<wchar_t> commandCopy(command.size() + 1);
  wcscpy_s(&commandCopy[0], commandCopy.size(), command.c_str());
  BOOL success = CreateProcess(NULL, &commandCopy[0], NULL, NULL, TRUE, flags,
                               NULL, NULL, &startupInfo, &processInfo);
  if (success) {
    CloseHandle(processInfo.hThread);
    hProcess_ = processInfo.hProcess;
    return true;
  } else {
    LOG(ERROR) << "Error starting " << base::WStringToString(command) << ": "
               << base::GetLastWindowsErrorString() << std::endl;
  }

  return false;
}

DWORD ChildProcess::GetExitCode() {
  if (!hProcess_)
    return 0;
  // Don't allow getting the exit code unless the process has exited.
  WaitForCompletion();
  DWORD result;
  (void)GetExitCodeProcess(hProcess_, &result);
  return result;
}

void ChildProcess::WaitForCompletion() {
  if (hProcess_) {
    // Wait for the process to finish.
    WaitForSingleObject(hProcess_, INFINITE);
  }

  // Close the stderr/stdout/stdin handles.
  if (hStdError_ != INVALID_HANDLE_VALUE) {
    CloseHandle(hStdError_);
    hStdError_ = INVALID_HANDLE_VALUE;
  }
  if (hStdOutput_ != INVALID_HANDLE_VALUE) {
    CloseHandle(hStdOutput_);
    hStdOutput_ = INVALID_HANDLE_VALUE;
  }
  if (hStdInput_ != INVALID_HANDLE_VALUE) {
    CloseHandle(hStdInput_);
    hStdInput_ = INVALID_HANDLE_VALUE;
  }
}

}  // namespace base
