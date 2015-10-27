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

#include "base/command_line.h"

#include "base/string_utils.h"

namespace base {

namespace {

const wchar_t kSwitchNamePrefix[] = L"--";
const size_t kSwitchNamePrefixLength =
    (sizeof(kSwitchNamePrefix) / sizeof(wchar_t)) - 1;

void ParseCommandLine(
    int argc,
    wchar_t* argv[],
    std::unordered_map<std::wstring, std::wstring>* switches) {
  std::wstring current_switch_name;
  for (int i = 0; i < argc; ++i) {
    std::wstring token(argv[i]);
    // Check if this is a switch name.
    if (base::WStringBeginsWith(token, kSwitchNamePrefix)) {
      // Add the previous switch name to the switch map.
      if (!current_switch_name.empty())
        switches->insert({current_switch_name, std::wstring()});

      // Keep track of the name of the current switch.
      current_switch_name = token.substr(kSwitchNamePrefixLength);
    } else if (!current_switch_name.empty()) {
      // This a switch value. Add it to the switch map with its name.
      switches->insert({current_switch_name, token});
      current_switch_name.clear();
    }
  }

  // If necessary, add the last switch name to the map.
  if (!current_switch_name.empty())
    switches->insert({current_switch_name, std::wstring()});
}

}  // namespace

CommandLine::CommandLine(int argc, wchar_t* argv[]) {
  ParseCommandLine(argc, argv, &switches_);
}

bool CommandLine::HasSwitch(const std::wstring& switch_name) const {
  return switches_.find(switch_name) != switches_.end();
}

std::wstring CommandLine::GetSwitchValue(
    const std::wstring& switch_name) const {
  auto look = switches_.find(switch_name);
  if (look == switches_.end())
    return std::wstring();

  return look->second;
}

}  // namespace base
