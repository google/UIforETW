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

#include "base/history.h"
#include "base/types.h"
#include "etw_reader/stack.h"

namespace etw_insights {

// Contains the history of a thread for the duration of a trace.
class ThreadHistory {
 public:
  ThreadHistory()
      : tid_(base::kInvalidTid),
        start_ts_(base::kInvalidTimestamp),
        end_ts_(base::kInvalidTimestamp),
        parent_process_id_(base::kInvalidPid) {}
  ThreadHistory(base::Tid tid)
     : tid_(tid),
       start_ts_(base::kInvalidTimestamp),
       end_ts_(base::kInvalidTimestamp),
       parent_process_id_(base::kInvalidPid) {}

  base::Tid tid() const { return tid_; }

  void set_start_ts(base::Timestamp ts) { start_ts_ = ts; }
  base::Timestamp start_ts() const { return start_ts_; }

  void set_end_ts(base::Timestamp ts) { end_ts_ = ts; }
  base::Timestamp end_ts() const { return end_ts_; }

  void set_parent_process_id(base::Pid parent_process_id) {
    parent_process_id_ = parent_process_id;
  }
  base::Timestamp parent_process_id() const { return parent_process_id_; }

  typedef base::History<Stack> StackHistory;
  StackHistory& Stacks() { return stacks_; }
  const StackHistory& Stacks() const { return stacks_; }

 private:
  // Thread id.
  base::Tid tid_;

  // Start timestamp.
  base::Timestamp start_ts_;

  // End timestamp.
  base::Timestamp end_ts_;

  // Parent process.
  base::Pid parent_process_id_;

  // Stack history.
  StackHistory stacks_;
};

}  // namespace etw_insights
