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

#include <unordered_map>

#include "base/base.h"
#include "base/types.h"
#include "etw_reader/thread_history.h"

namespace etw_insights {

// Contains the history of a system for the duration of a trace.
class SystemHistory {
 public:
  typedef std::unordered_map<base::Tid, ThreadHistory> ThreadHistoryMap;

  SystemHistory();

  ThreadHistory& GetThread(base::Tid tid);

  void set_first_event_ts(base::Timestamp ts) { first_event_ts_ = ts; }
  base::Timestamp first_event_ts() const { return first_event_ts_; }

  void set_last_event_ts(base::Timestamp ts) { last_event_ts_ = ts; }
  base::Timestamp last_event_ts() const { return last_event_ts_; }

  void set_first_non_empty_paint_ts(base::Timestamp ts) {
    first_non_empty_paint_ts_ = ts;
  }
  base::Timestamp first_non_empty_paint_ts() const {
    return first_non_empty_paint_ts_;
  }

  void SetProcessName(base::Pid process_id, const std::string& process_name);
  const std::string& GetProcessName(base::Pid process_id) const;

  ThreadHistoryMap::const_iterator threads_begin() const {
    return threads_.begin();
  }
  ThreadHistoryMap::const_iterator threads_end() const {
    return threads_.end();
  }

 private:
  // Empty string.
  std::string empty_string_;

  // Timestamp of the first event.
  base::Timestamp first_event_ts_;

  // Timestamp of the last event.
  base::Timestamp last_event_ts_;

  // Timestamp of the first non empty paint, in Chrome.
  base::Timestamp first_non_empty_paint_ts_;

  // History of each thread.
  ThreadHistoryMap threads_;

  // Process names (Process ID -> Process Name).
  std::unordered_map<base::Pid, std::string> process_names_;

  DISALLOW_COPY_AND_ASSIGN(SystemHistory);
};

}  // namespace etw_insights
