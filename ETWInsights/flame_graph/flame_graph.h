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

#include <map>

#include "base/base.h"
#include "base/types.h"
#include "etw_reader/thread_history.h"

namespace etw_insights {

class FlameGraph {
 public:
  FlameGraph();

  void AddThreadHistory(const ThreadHistory& thread_history,
                        base::Timestamp start_ts,
                        base::Timestamp end_ts);

  void WriteTxtReport(const std::wstring& path);

 private:
  // Map: Stack -> Total time spent in the call stack.
  typedef std::map<Stack, base::Timestamp> StackTimeMap;
  StackTimeMap stack_time_;

  DISALLOW_COPY_AND_ASSIGN(FlameGraph);
};

}  // namespace etw_insights
