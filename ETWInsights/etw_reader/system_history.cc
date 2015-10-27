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

#include "etw_reader/system_history.h"

namespace etw_insights {

SystemHistory::SystemHistory()
    : first_event_ts_(0),
      last_event_ts_(base::kInvalidTimestamp),
      first_non_empty_paint_ts_(base::kInvalidTimestamp) {}

ThreadHistory& SystemHistory::GetThread(base::Tid tid) {
  auto look = threads_.find(tid);
  if (look != threads_.end())
    return look->second;
  threads_[tid] = ThreadHistory(tid);
  return threads_[tid];
}

void SystemHistory::SetProcessName(base::Pid process_id,
                                   const std::string& process_name) {
  process_names_[process_id] = process_name;
}

const std::string& SystemHistory::GetProcessName(base::Pid process_id) const {
  auto look = process_names_.find(process_id);
  if (look == process_names_.end())
    return empty_string_;
  return look->second;
}

}  // namespace etw_insights
