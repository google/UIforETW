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
#include <vector>

namespace base {

// Convert between ASCII and Unicode strings.
std::wstring StringToWString(const std::string& string);
std::string WStringToString(const std::wstring& string);

// Returns true if |str| contains |substr| at position |pos|.
bool SubstrAtPos(const std::string& str, const std::string& substr, size_t pos);
bool WSubstrAtPos(const std::wstring& str,
                  const std::wstring& substr,
                  size_t pos);

// Returns true if |str| begins with |starting|, or false otherwise.
bool StringBeginsWith(const std::string& str, const std::string& starting);
bool WStringBeginsWith(const std::wstring& str, const std::wstring& starting);

// Returns true if |str| ends with |ending|, or false otherwise.
bool StringEndsWith(const std::string& str, const std::string& ending);
bool WStringEndsWith(const std::wstring& str, const std::wstring& ending);

// Escape the C special characters with a backslash.
// @param str the string to be escaped.
// @returns a string escaped copy of |str|.
std::string StringEscapeSpecialCharacter(const std::string& str);

// Splits |str| at each occurrence of |separator|.
std::vector<std::string> SplitString(const std::string& str,
                                     const std::string& separator);
std::vector<std::wstring> SplitWString(const std::wstring& str,
                                       const std::wstring& separator);

// Removes spaces at the beginning and end of |str|.
std::string Trim(const std::string& str);
std::wstring TrimW(const std::wstring& str);

// Replaces all occurrences of |search| in |str| by |replace|.
void ReplaceAll(const std::string& search,
                const std::string& replace,
                std::string* str);
void ReplaceAllW(const std::wstring& search,
                 const std::wstring& replace,
                 std::wstring* str);

}  // namespace base
