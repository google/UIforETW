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

#include "base/error_string.h"

#include <sstream>
#include <strsafe.h>

#include "base/string_utils.h"

namespace base {

std::string GetWindowsErrorString(DWORD error) {
  LPWSTR error_desc_buffer = NULL;
  LPWSTR error_str_buffer = NULL;

  ::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&error_desc_buffer, 0, NULL);

  error_str_buffer = (LPWSTR)LocalAlloc(
      LMEM_ZEROINIT,
      (lstrlen((LPCTSTR)error_desc_buffer) + 40) * sizeof(TCHAR));
  ::StringCchPrintf((LPTSTR)error_str_buffer,
                    LocalSize(error_str_buffer) / sizeof(TCHAR),
                    TEXT("%d - %s"), error, error_desc_buffer);

  std::wstring error_str(error_str_buffer);

  ::LocalFree(error_desc_buffer);
  ::LocalFree(error_str_buffer);

  return WStringToString(error_str);
}

std::string GetLastWindowsErrorString() {
  return GetWindowsErrorString(::GetLastError());
}

}  // namespace base
