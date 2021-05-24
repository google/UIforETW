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

#include "base/string_utils.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>

namespace base {

namespace {

template <typename T>
bool StringBeginsWithInternal(const T& str, const T& starting) {
  if (str.compare(0, starting.length(), starting) == 0)
    return true;
  return false;
}

template <typename T>
bool StringEndsWithInternal(const T& str, const T& ending) {
  if (ending.length() > str.length())
    return false;

  if (str.compare(str.length() - ending.length(), ending.length(), ending) ==
      0) {
    return true;
  }

  return false;
}

template <typename T>
bool SubstrAtPosInternal(const T& str, const T& substr, size_t pos) {
  if (pos + substr.size() > str.size())
    return false;
  for (size_t i = 0; i < substr.size(); ++i) {
    if (str[i + pos] != substr[i])
      return false;
  }
  return true;
}

template <typename T>
std::vector<T> SplitStringInternal(const T& str, const T& separator) {
  std::vector<T> res;
  size_t start_index = 0;
  size_t end_index = 0;

  while (start_index < str.size()) {
    while (end_index < str.size() &&
           !SubstrAtPosInternal(str, separator, end_index))
      ++end_index;
    res.push_back(str.substr(start_index, end_index - start_index));
    start_index = end_index + separator.size();
    end_index = start_index;
  }

  return res;
}

template <typename T>
T TrimInternal(const T& str) {
  // Find position of first and last non-space character.
  auto first = std::find_if(str.begin(), str.end(),
                            std::not1(std::ptr_fun(std::isspace)));
  auto last = std::find_if(str.rbegin(), str.rend(),
                           std::not1(std::ptr_fun(std::isspace)));

  auto first_index = std::distance(str.begin(), first);
  auto length = std::distance(last, str.rend()) - first_index;

  return str.substr(first_index, length);
}

template <typename T>
void ReplaceAllInternal(const T& search,
                        const T& replace,
                        T* str) {
  size_t search_pos = 0;
  size_t token_pos;

  while ((token_pos = str->find(search, search_pos)) != T::npos) {
    str->replace(token_pos, search.length(), replace);
    search_pos = token_pos + replace.size();
  }
}

}  // namespace

std::wstring StringToWString(const std::string& string) {
  return std::wstring(string.begin(), string.end());
}

#pragma warning(disable : 4244)
// warning C4244: 'argument': conversion from 'const wchar_t' to 'const _Elem', possible loss of data
std::string WStringToString(const std::wstring& string) {
  return std::string(string.begin(), string.end());
}

bool SubstrAtPos(const std::string& str,
                 const std::string& substr,
                 size_t pos) {
  return SubstrAtPosInternal(str, substr, pos);
}

bool WSubstrAtPos(const std::wstring& str,
                  const std::wstring& substr,
                  size_t pos) {
  return SubstrAtPosInternal(str, substr, pos);
}

bool StringBeginsWith(const std::string& str, const std::string& starting) {
  return StringBeginsWithInternal(str, starting);
}

bool WStringBeginsWith(const std::wstring& str, const std::wstring& starting) {
  return StringBeginsWithInternal(str, starting);
}

bool StringEndsWith(const std::string& str, const std::string& ending) {
  return StringEndsWithInternal(str, ending);
}

bool WStringEndsWith(const std::wstring& str, const std::wstring& ending) {
  return StringEndsWithInternal(str, ending);
}

std::string StringEscapeSpecialCharacter(const std::string& str) {
  std::stringstream ss;
  for (std::string::const_iterator i = str.begin(); i != str.end(); ++i) {
    unsigned char c = *i;
    if (' ' <= c && c <= '~' && c != '\\' && c != '"') {
      ss << c;
      continue;
    }

    ss << '\\';

    switch (c) {
      case '"':
        ss << '"';
        break;
      case '\\':
        ss << '\\';
        break;
      case '\t':
        ss << 't';
        break;
      case '\r':
        ss << 'r';
        break;
      case '\n':
        ss << 'n';
        break;
      default: {
        static char const* const hexdig = "0123456789ABCDEF";
        ss << 'x';
        ss << hexdig[c >> 4];
        ss << hexdig[c & 0xF];
        break;
      }
    }
  }

  return ss.str();
}

std::vector<std::string> SplitString(const std::string& str,
                                     const std::string& separator) {
  return SplitStringInternal(str, separator);
}

std::vector<std::wstring> SplitWString(const std::wstring& str,
                                       const std::wstring& separator) {
  return SplitStringInternal(str, separator);
}

std::string Trim(const std::string& str) {
  return TrimInternal(str);
}

std::wstring TrimW(const std::wstring& str) {
  return TrimInternal(str);
}

void ReplaceAll(const std::string& search,
                const std::string& replace,
                std::string* str) {
  ReplaceAllInternal(search, replace, str);
}

void ReplaceAllW(const std::wstring& search,
                 const std::wstring& replace,
                 std::wstring* str) {
  ReplaceAllInternal(search, replace, str);
}

}  // namespace base
