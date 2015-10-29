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

#include "flame_graph/clean_stack.h"

#include "base/string_utils.h"

namespace etw_insights {

namespace {

const size_t kMaxStackSize = 60;

std::string CleanSymbol(const std::string& symbol) {
  // Don't clean special symbols.
  if (symbol.empty() || symbol.front() == '[')
    return symbol;

  std::string cleaned_symbol(symbol);

  // Replace semicolons colons.
  base::ReplaceAll(";", ",", &cleaned_symbol);

  // Remove dll and exe extensions.
  base::ReplaceAll(".dll!", "!", &cleaned_symbol);
  base::ReplaceAll(".exe!", "!", &cleaned_symbol);

  return cleaned_symbol;
}

}  // namespace

Stack CleanStack(const Stack& stack) {
  Stack cleaned_stack;

  bool is_in_page_fault = false;

  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    // Truncate tall stacks.
    if (cleaned_stack.size() > kMaxStackSize) {
      cleaned_stack.push_back("[Tuncated]");
      break;
    }

    // Simplify page fault call stack.
    if (*it == "ntoskrnl.exe!KiPageFault") {
      is_in_page_fault = true;
    }
    if (is_in_page_fault) {
      if (*it == "[Off-CPU]") {
        is_in_page_fault = false;
        cleaned_stack.push_back("[Page Fault]");
        cleaned_stack.push_back("[Off-CPU]");
      }
      continue;
    }

    // Ignore some symbols at the bottom of the stack.
    if (*it == "ntdll.dll!_RtlUserThreadStart" ||
        *it == "ntdll.dll!__RtlUserThreadStart" ||
        *it == "kernel32.dll!BaseThreadInitThunk" ||
        *it == "chrome.exe!__tmainCRTStartup") {
      continue;
    }

    // Ignore some symbols at the top of the stack.
    if (*it == "ntoskrnl.exe!SwapContext_PatchLdMxCsr" ||
        *it == "ntoskrnl.exe!KiSwapContext" ||
        *it == "ntoskrnl.exe!KiSwapThread" ||
        *it == "ntoskrnl.exe!KiCommitThreadWait" ||
        *it == "ntoskrnl.exe!KeWaitForSingoeObject") {
      continue;
    }

    // Add the symbol to the cleaned stack.
    cleaned_stack.push_back(CleanSymbol(*it));
  }

  return cleaned_stack;
}

}  // namespace etw_insights
