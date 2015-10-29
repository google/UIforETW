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

#include "flame_graph/flame_graph.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>

#include "base/child_process.h"
#include "base/file.h"
#include "base/string_utils.h"
#include "flame_graph/clean_stack.h"

namespace etw_insights {

namespace {

// Ignore call stacks that contain these sequences of frames.
const char* kSequencesToIgnore[][2] = {
    {"base::SequencedWorkerPool::Inner::ThreadLoop",
     "base::WinVistaCondVar::TimedWait"},
    {"base::SequencedWorkerPool::Inner::ThreadLoop",
     "base::WinVistaCondVar::Wait"},
    {"base::SequencedWorkerPool::Worker::Worker", "base::WaitableEvent::Wait"},
    {"base::MessageLoop::RunHandler", "base::WaitableEvent::Wait"},
    {"base::MessagePumpDefault::Run", "base::WaitableEvent::TimedWait"},
    {"base::MessagePumpForIO::DoRunLoop",
     "base::MessagePumpForIO::WaitForIOCompletion"},
    {"base::MessagePumpForUI::DoRunLoop", "MsgWaitForMultipleObjectsEx"},
    {"base::trace_event::TraceEventETWExport::ETWKeywordUpdateThread::"
     "ThreadMain",
     "base::PlatformThread::Sleep"},
    {"cc::TaskGraphRunner::Run", "base::WinVistaCondVar::Wait"},
    {"MojoWaitMany", "mojo::system::Core::WaitMany"},
    {"TppWorkerThread", "ZwWaitForWorkViaWorkerFactory"},
    {"sandbox::BrokerServicesBase::TargetEventsThread",
     "GetQueuedCompletionStatus"},
};
const size_t kSequencesToIgnoreSize = ARRAYSIZE(kSequencesToIgnore);

// Ignore call stacks that contain these frames.
const char* kFramesToIgnore[] = {
    "EtwpQueueStackWalkApc", "EtwpTraceStackWalk", "EtwpLogKernelEvent",
};
const size_t kFramesToIgnoreSize = ARRAYSIZE(kFramesToIgnore);

bool ShouldIgnoreStack(const Stack& stack) {
  if (stack.empty())
    return true;

  bool is_off_cpu = stack.front() == "[Off-CPU]";

  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    // Ignore sequences of frames.
    auto next_it = it + 1;
    if (is_off_cpu && next_it != stack.rend()) {
      for (size_t sequence_index = 0; sequence_index < kSequencesToIgnoreSize;
           ++sequence_index) {
        if (base::StringEndsWith(*it, kSequencesToIgnore[sequence_index][0]) &&
            base::StringEndsWith(*next_it,
                                 kSequencesToIgnore[sequence_index][1])) {
          return true;
        }
      }
    }

    // Ignore single frames.
    for (size_t frame_index = 0; frame_index < kFramesToIgnoreSize;
         ++frame_index) {
      if (base::StringEndsWith(*it, kFramesToIgnore[frame_index]))
        return true;
    }
  }

  return false;
}

}  // namespace

FlameGraph::FlameGraph() {}

void FlameGraph::AddThreadHistory(const ThreadHistory& thread_history,
                                  base::Timestamp start_ts,
                                  base::Timestamp end_ts) {
  auto it = thread_history.Stacks().IteratorFromTimestamp(start_ts);
  auto end_it = thread_history.Stacks().IteratorEnd();

  for (; it != end_it && it->start_ts < end_ts; ++it) {
    base::Timestamp stack_start_ts = max(start_ts, it->start_ts);

    base::Timestamp stack_end_ts = min(end_ts, thread_history.end_ts());
    auto next_it = it + 1;
    if (next_it != end_it && next_it->start_ts < stack_end_ts)
      stack_end_ts = next_it->start_ts;

    if (stack_end_ts < stack_start_ts) {
      continue;
    }

    base::Timestamp stack_duration = stack_end_ts - stack_start_ts;
    stack_time_[it->value] += stack_duration;
  }
}

void FlameGraph::WriteTxtReport(const std::wstring& path) {
  std::ofstream out(path, std::ios::binary);

  for (const auto& stack_and_time : stack_time_) {
    if (ShouldIgnoreStack(stack_and_time.first))
      continue;

    Stack stack = CleanStack(stack_and_time.first);
    bool first = true;
    for (const auto& symbol : stack) {
      if (!first)
        out << ";";
      first = false;
      out << symbol;
    }

    out << " " << stack_and_time.second << "\n";
  }
}

}  // namespace etw_insights
