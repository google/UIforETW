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
#include <unordered_map>

#include "base/base.h"

namespace base {

class CommandLine {
 public:
  // Constructs a command line from an argument list.
  // @param argc Number of arguments.
  // @param argv Arguments.
  CommandLine(int argc, wchar_t* argv[]);

  // @param switch_name Name of a switch.
  // @returns true if the command line contains the given switch.
  bool HasSwitch(const std::wstring& switch_name) const;

  // @param switch_name Name of a switch.
  // @returns the value associated with the switch, or an empty string if no
  //    value was specified for the switch in the command line.
  std::wstring GetSwitchValue(const std::wstring& switch_name) const;

  // @returns the number of command-line switches.
  size_t GetNumSwitches() const {
    return switches_.size();
  }

 private:
  // Map Switch Name -> Switch Value.
  std::unordered_map<std::wstring, std::wstring> switches_;

  DISALLOW_COPY_AND_ASSIGN(CommandLine);
};

}  // namespace base
