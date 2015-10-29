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

namespace base {

// @param path a file path.
// @returns everything that comes before the last backslash in |path|. Empty if
//     |path| doesn't contain a backslash.
std::wstring DirName(const std::wstring& path);

// @param path a file path.
// @returns the last path component of the provided file path.
std::wstring BaseName(const std::wstring& path);

// @param path a file path.
// @returns true if the file path exists, false otherwise.
bool FilePathExists(const std::wstring& path);

}  // namespace base
