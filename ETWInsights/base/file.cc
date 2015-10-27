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

#include "base/file.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace base {

std::wstring DirName(const std::wstring& path) {
  auto pos = path.rfind(L'\\');
  if (pos == std::wstring::npos)
    return std::wstring();
  return path.substr(0, pos);
}

std::wstring BaseName(const std::wstring& path) {
  size_t last_backslash = path.find_last_of(L"\\");
  if (last_backslash == std::wstring::npos)
    return path;
  return path.substr(last_backslash + 1);
}

bool FilePathExists(const std::wstring& path) {
  HANDLE handle = ::CreateFileW(path.c_str(),           // file to open
                                GENERIC_READ,           // open for reading
                                FILE_SHARE_READ,        // share for reading
                                NULL,                   // default security
                                OPEN_EXISTING,          // existing file only
                                FILE_ATTRIBUTE_NORMAL,  // normal file
                                NULL);                  // no attr. template
  if (handle == INVALID_HANDLE_VALUE)
    return false;
  ::CloseHandle(handle);
  return true;
}

}  // namespace base
