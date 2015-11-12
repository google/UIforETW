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

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef min
#undef max

#include <iostream>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/numeric_conversions.h"
#include "base/string_utils.h"
#include "etw_reader/generate_history_from_trace.h"
#include "etw_reader/system_history.h"
#include "flame_graph/flame_graph.h"

using namespace etw_insights;

namespace {

// Suffix for a flame graph file name.
const wchar_t kFlameGraphFileNameSuffix[] = L".flamegraph.txt";

void ShowUsage() {
  std::cout
      << "Usage: flame_graph.exe --trace <trace_file_path> [options]"
      << std::endl
      << std::endl
      << "Options:" << std::endl
      << "  --process_name: Only include stacks from processes with the "
         "specified name."
      << std::endl
      << "  --tid: Only include stacks from the specified thread." << std::endl
      << "  --start_ts: Only include stacks that occurred after the specified "
         "timestamp (in microseconds)."
      << std::endl
      << "  --end_ts: Only include stacks that occurred before the specified "
         "timestamp (in microseconds)."
      << std::endl
      << "  --out: Output file path. Default: <trace_file_path>.flamegraph.txt"
      << std::endl;
}

}  // namespace

int wmain(int argc, wchar_t* argv[], wchar_t* /*envp */ []) {
  // Read command line arguments.
  base::CommandLine command_line(argc, argv);

  if (command_line.GetNumSwitches() == 0) {
    ShowUsage();
    return 1;
  }

  std::wstring trace_path = command_line.GetSwitchValue(L"trace");
  if (trace_path.empty()) {
    std::cout << "Please specify a trace path (--trace)." << std::endl
              << std::endl;
    ShowUsage();
    return 1;
  }

  std::wstring thread_id_filter_str = command_line.GetSwitchValue(L"tid");
  uint64_t thread_id_filter = base::kInvalidTid;
  if (!thread_id_filter_str.empty() &&
      !base::StrToULong(thread_id_filter_str, &thread_id_filter)) {
    std::cout << "Thread id must be numeric (--tid)." << std::endl << std::endl;
    ShowUsage();
    return 1;
  }

  std::string process_name_filter(
      base::WStringToString(command_line.GetSwitchValue(L"process_name")));

  uint64_t start_ts = 0;
  base::StrToULong(command_line.GetSwitchValue(L"start_ts"), &start_ts);

  uint64_t end_ts = base::kInvalidTimestamp;
  base::StrToULong(command_line.GetSwitchValue(L"end_ts"), &end_ts);

  std::wstring output_path(command_line.GetSwitchValue(L"out"));

  // Generate a system history from the trace.
  SystemHistory system_history;
  if (!GenerateHistoryFromTrace(trace_path, &system_history)) {
    LOG(ERROR) << "Error while generating history from trace.";
    return 1;
  }

  // Determine the end time of the analysis.
  base::Timestamp analysis_end_ts =
      std::min(std::min(end_ts, system_history.last_event_ts()),
               system_history.first_non_empty_paint_ts());

  // Tell the user what we are doing.
  LOG(INFO) << "Generating flame graph." << std::endl;

  // Create a flame graph.
  FlameGraph flame_graph;

  // Traverse all threads and add those that match the filter to the history.
  for (auto threads_it = system_history.threads_begin();
       threads_it != system_history.threads_end(); ++threads_it) {
    // Thread id filter.
    if (thread_id_filter != base::kInvalidTid &&
        thread_id_filter != threads_it->first)
      continue;

    // Process name filter.
    if (!process_name_filter.empty()) {
      std::string process_name =
          system_history.GetProcessName(threads_it->second.parent_process_id());
      if (process_name_filter != process_name)
        continue;
    }

    // The current thread matches the filter. Add it to the flame graph.
    flame_graph.AddThreadHistory(
        threads_it->second,
        std::max(start_ts, system_history.first_event_ts()),
        analysis_end_ts);
  }

  // Write the flame graph in a text file.
  if (output_path.empty())
    output_path = trace_path + kFlameGraphFileNameSuffix;
  flame_graph.WriteTxtReport(output_path);

  // Tell the user that the flame graph was generated.
  LOG(INFO) << "Wrote flame graph data in file "
            << base::WStringToString(output_path);

  return 0;
}
