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

#include "etw_reader/generate_history_from_trace.h"

#include <map>
#include <unordered_map>
#include <vector>

#include "base/numeric_conversions.h"
#include "base/string_utils.h"
#include "base/types.h"
#include "etw_reader/etw_reader.h"

namespace etw_insights {

namespace {

// [Off-CPU] stack frame.
const char kOffCpuStackFrame[] = "[Off-CPU]";

// Generic event fields.
const char kTimestampField[] = "TimeStamp";
const char kThreadIDField[] = "ThreadID";
const char kProcessNameField[] = "Process Name ( PID)";

// Stack event.
const char kStackType[] = "Stack";
const char kStackSymbolField[] = "Image!Function";

// Sampled profile event.
const char kSampledProfileType[] = "SampledProfile";

// CSwitch event.
const char kCSwitchType[] = "CSwitch";
const char kCSwitchNewTidField[] = "New TID";
const char kCSwitchOldTidField[] = "Old TID";
const char kCSwitchTimeSinceLastField[] = "TmSinceLast";

// Process start event.
const char kProcessStartType[] = "P-Start";
const char kProcessDCStartType[] = "P-DCStart";

// Thread start event.
const char kThreadStartType[] = "T-Start";
const char kThreadDCStartType[] = "T-DCStart";

// Thread end event.
const char kThreadEndType[] = "T-End";
const char kThreadDCEndType[] = "T-DCEnd";

// FileIo events.
const char kFileIoCreateType[] = "FileIoCreate";
const char kFileIoCleanupType[] = "FileIoCleanup";
const char kFileIoCloseType[] = "FileIoClose";
const char kFileIoFlushType[] = "FileIoFlush";
const char kFileIoReadType[] = "FileIoRead";
const char kFileIoWriteType[] = "FileIoWrite";
const char kFileIoSetInfoType[] = "FileIoSetInfo";
const char kFileIoQueryInfoType[] = "FileIoQueryInfo";
const char kFileIoFSCTLType[] = "FileIoFSCTL";
const char kFileIoDeleteType[] = "FileIoDelete";
const char kFileIoRenameType[] = "FileIoRename";
const char kFileIoDirEnumType[] = "FileIoDirEnum";
const char kFileIoDirNotifyType[] = "FileIoDirNotify";
const char kFileIoOpEnd[] = "FileIoOpEnd";
const char kFileIoFileNameField[] = "FileName";
const char kFileIoTypeField[] = "Type";
const char kFileIoLoggingThreadIdField[] = "LoggingThreadID";

// Chrome events.
const char kChromeType[] = "Chrome//win:Info";
const char kChromeNameField[] = "Name";
const char kChromePhaseField[] = "Phase";
const char kChromeNonEmptyPaint[] =
    "\"Startup.FirstWebContents.NonEmptyPaint\"";
const char kChromePhaseAsyncBegin[] = "\"Async End\"";

// Unknown stack frame.
const char kUnknownStackFrame[] = "[Unknown]";

// State of a thread.
struct ThreadState {
  ThreadState() {}

  // Active file operation.
  std::string file_operation;

  // Last events encountered on the thread (timestamp -> type).
  std::map<base::Timestamp, std::string> last_events;

  // Timestamp of the last switch out that occurred before each switch in.
  // (Switch in ts -> Switch out ts).
  std::map<base::Timestamp, base::Timestamp> last_switch_out_before_switch_in_;
};

typedef std::unordered_map<base::Tid, ThreadState> ThreadStates;

Stack ConcatenateStacks(std::initializer_list<Stack> stacks) {
  Stack stack_res;
  for (const auto& stack : stacks)
    stack_res.insert(stack_res.end(), stack.begin(), stack.end());
  return stack_res;
}

void SplitProcessNameField(const std::string& value,
                           std::string* process_name,
                           base::Pid* pid) {
  auto tokens = base::SplitString(value, " ");
  *process_name = tokens[0];

  size_t start_pos = 0;
  if (tokens.back()[0] == '(')
    start_pos = 1;

  if (!base::StrToULong(
          tokens.back().substr(start_pos, tokens.back().size() - 1 - start_pos),
          pid)) {
    LOG(ERROR) << "Unable to extract process id from process name field ("
               << value << ").";
  }
}

void HandleStackEvent(base::Timestamp ts,
                      ETWReader::Iterator& it,
                      ThreadStates* thread_states,
                      SystemHistory* system_history) {
  // Get the event tid.
  base::Tid tid = 0;
  if (!it->GetFieldAsULong(kThreadIDField, &tid))
    LOG(ERROR) << "Unable to read column ThreadID of Stack event.";

  // Get the event stack.
  Stack stack;
  while (it->type() == kStackType) {
    std::string symbol;
    if (!it->GetFieldAsString(kStackSymbolField, &symbol))
      continue;
    stack.push_back(symbol);
    ++it;
  }
  DCHECK_EQ(ETWReader::kEmptyEventType, it->type());

  // Get the associated event type.
  const ThreadState& thread_state = (*thread_states)[tid];
  auto look_associated_event_type = thread_state.last_events.find(ts);
  if (look_associated_event_type == thread_state.last_events.end())
    return;
  auto associated_event_type = look_associated_event_type->second;

  // Get the stack history for the thread.
  auto& stack_history = system_history->GetThread(tid).Stacks();
  base::Timestamp last_stack_ts = 0;
  stack_history.GetLastElementTimestamp(&last_stack_ts);

  if (associated_event_type == kSampledProfileType) {
    // Handle a call stack associated with a SampledProfile event.
    if (last_stack_ts == ts) {
      auto stack_history_it = stack_history.IteratorFromTimestamp(ts);
      stack_history_it->value =
          ConcatenateStacks({stack_history_it->value, stack});
    } else {
      stack_history.Insert(ts, stack);
    }
  } else if (associated_event_type == kCSwitchType) {
    // Handle a call stack associated with a CSwitch event.

    // Get the switch in and switch out times.
    base::Timestamp switch_in_time = ts;
    auto look_switch_out_time =
        thread_state.last_switch_out_before_switch_in_.find(switch_in_time);
    if (look_switch_out_time ==
        thread_state.last_switch_out_before_switch_in_.end()) {
      LOG(ERROR) << "No switch out time for CSwitch stack.";
      return;
    }
    base::Timestamp switch_out_time = look_switch_out_time->second;

    // Update the call stack history.
    if (switch_in_time != switch_out_time) {
      if (last_stack_ts == ts) {
        // Concatenate this CSwitch stack to another CSwitch stack that we got
        // previously.
        auto stack_history_it =
            stack_history.IteratorFromTimestamp(switch_out_time);
        stack_history_it->value =
            ConcatenateStacks({stack_history_it->value, stack});
      } else {
        // Save the previous stack.
        Stack previous_stack;
        stack_history.GetLastElementValue(&previous_stack);

        // Add the blocked stack.
        Stack off_cpu_synthetic_stack;
        if (!thread_state.file_operation.empty())
          off_cpu_synthetic_stack.push_back(thread_state.file_operation);
        off_cpu_synthetic_stack.push_back(kOffCpuStackFrame);

        stack_history.Insert(
            switch_out_time,
            ConcatenateStacks({off_cpu_synthetic_stack, stack}));

        // Add the stack that follow the blocked stack.
        if (previous_stack.empty()) {
          previous_stack.push_back(kUnknownStackFrame);
        }

        stack_history.Insert(switch_in_time, previous_stack);
      }
    }
  }
}

void HandleCSwitchEvent(base::Timestamp ts,
                        const ETWReader::Line& event,
                        ThreadStates* thread_states) {
  base::Tid new_tid = 0;
  base::Tid old_tid = 0;
  base::Timestamp time_since_last = 0;
  if (!event.GetFieldAsULong(kCSwitchNewTidField, &new_tid) ||
      !event.GetFieldAsULong(kCSwitchOldTidField, &old_tid) ||
      !event.GetFieldAsULong(kCSwitchTimeSinceLastField, &time_since_last)) {
    LOG(ERROR) << "Missing some fields in CSwitch event at ts=" << ts << ".";
    return;
  }

  ThreadState& new_thread_state = (*thread_states)[new_tid];
  new_thread_state.last_switch_out_before_switch_in_[ts] = ts - time_since_last;
}

void HandleProcessStartEvent(base::Timestamp ts,
                             const ETWReader::Line& event,
                             SystemHistory* system_history) {
  std::string process_name_field;
  if (!event.GetFieldAsString(kProcessNameField, &process_name_field)) {
    LOG(ERROR) << "Missing some fields in Process Start event at ts=" << ts
               << ".";
    return;
  }

  std::string process_name;
  base::Pid process_id = base::kInvalidPid;
  SplitProcessNameField(process_name_field, &process_name, &process_id);

  system_history->SetProcessName(process_id, process_name);
}

void HandleThreadStartEvent(base::Timestamp ts,
                            const ETWReader::Line& event,
                            SystemHistory* system_history) {
  base::Tid thread_id = 0;
  std::string process_name_field;
  if (!event.GetFieldAsULong(kThreadIDField, &thread_id) ||
      !event.GetFieldAsString(kProcessNameField, &process_name_field)) {
    LOG(ERROR) << "Missing some fields in Thread Start event at ts=" << ts
               << ".";
    return;
  }

  std::string process_name;
  base::Pid process_id = base::kInvalidPid;
  SplitProcessNameField(process_name_field, &process_name, &process_id);

  auto& thread_history = system_history->GetThread(thread_id);
  thread_history.set_start_ts(ts);
  thread_history.set_parent_process_id(process_id);
}

void HandleThreadEndEvent(base::Timestamp ts,
                          const ETWReader::Line& event,
                          SystemHistory* system_history) {
  base::Tid thread_id = 0;
  if (!event.GetFieldAsULong(kThreadIDField, &thread_id)) {
    LOG(ERROR) << "Missing some fields in Thread End event at ts=" << ts << ".";
    return;
  }

  auto& thread_history = system_history->GetThread(thread_id);
  thread_history.set_end_ts(ts);
}

void HandleFileIoEvent(base::Timestamp ts,
                       const ETWReader::Line& event,
                       ThreadStates* thread_states) {
  base::Tid thread_id = 0;
  std::string file_name;
  if (!event.GetFieldAsULong(kFileIoLoggingThreadIdField, &thread_id) ||
      !event.GetFieldAsString(kFileIoFileNameField, &file_name)) {
    LOG(ERROR) << "Missing some fields in FileIo event at ts=" << ts << ".";
    return;
  }

  std::string event_str(std::string("[") + event.type() + ": " + file_name +
                        "]");
  auto& thread_state = (*thread_states)[thread_id];
  thread_state.file_operation = event_str;
}

void HandleFileIoOpEndEvent(base::Timestamp ts,
                            const ETWReader::Line& event,
                            ThreadStates* thread_states) {
  base::Tid thread_id = 0;
  std::string file_name;
  if (!event.GetFieldAsULong(kFileIoLoggingThreadIdField, &thread_id) ||
      !event.GetFieldAsString(kFileIoFileNameField, &file_name)) {
    LOG(ERROR) << "Missing some fields in FileIoOpEnd event at ts=" << ts
               << ".";
    return;
  }

  auto& thread_state = (*thread_states)[thread_id];
  thread_state.file_operation.clear();
}

void HandleChromeEvent(base::Timestamp ts,
                       const ETWReader::Line& event,
                       SystemHistory* system_history,
                       bool* should_stop) {
  std::string name;
  std::string phase;
  if (!event.GetFieldAsString(kChromeNameField, &name) ||
      !event.GetFieldAsString(kChromePhaseField, &phase)) {
    LOG(ERROR) << "Missing some fields in Chrome event at ts=" << ts << ".";
    return;
  }

  if (name == kChromeNonEmptyPaint && phase == kChromePhaseAsyncBegin) {
    system_history->set_first_non_empty_paint_ts(ts);
    *should_stop = true;
  }
}

}  // namespace

bool GenerateHistoryFromTrace(const std::wstring& trace_path,
                              SystemHistory* system_history) {
  // Keeps track of the current state of each thread.
  std::unordered_map<base::Tid, ThreadState> thread_states;

  // Open the CSV trace.
  ETWReader etw_reader;
  if (!etw_reader.Open(trace_path))
    return false;

  // Tell the user what we are doing.
  LOG(INFO) << "Reading trace events." << std::endl;

  // Traverse all the events of the CSV trace.
  for (auto it = etw_reader.begin(); it != etw_reader.end(); ++it) {
    // Should we stop parsing the trace?
    bool should_stop = false;

    // Get the event timestamp.
    base::Timestamp ts = 0;
    it->GetFieldAsULong(kTimestampField, &ts);

    // Handle each event type.
    if (it->type() == kStackType)
      HandleStackEvent(ts, it, &thread_states, system_history);
    else if (it->type() == kCSwitchType)
      HandleCSwitchEvent(ts, *it, &thread_states);
    else if (it->type() == kProcessStartType ||
             it->type() == kProcessDCStartType)
      HandleProcessStartEvent(ts, *it, system_history);
    else if (it->type() == kThreadStartType || it->type() == kThreadDCStartType)
      HandleThreadStartEvent(ts, *it, system_history);
    else if (it->type() == kThreadEndType || it->type() == kThreadDCEndType)
      HandleThreadEndEvent(ts, *it, system_history);
    else if (it->type() == kFileIoCreateType ||
             it->type() == kFileIoCleanupType ||
             it->type() == kFileIoCloseType ||
             it->type() == kFileIoFlushType ||
             it->type() == kFileIoReadType ||
             it->type() == kFileIoWriteType ||
             it->type() == kFileIoSetInfoType ||
             it->type() == kFileIoQueryInfoType ||
             it->type() == kFileIoFSCTLType ||
             it->type() == kFileIoDeleteType ||
             it->type() == kFileIoRenameType ||
             it->type() == kFileIoDirEnumType ||
             it->type() == kFileIoDirNotifyType)
      HandleFileIoEvent(ts, *it, &thread_states);
    else if (it->type() == kFileIoOpEnd)
      HandleFileIoOpEndEvent(ts, *it, &thread_states);
    else if (it->type() == kChromeType)
      HandleChromeEvent(ts, *it, system_history, &should_stop);

    // Remember the last event types encountered on each thread.
    base::Tid tid = 0;
    if (it->GetFieldAsULong(kThreadIDField, &tid) ||
        it->GetFieldAsULong(kCSwitchNewTidField, &tid)) {
      ThreadState& thread_state = thread_states[tid];
      thread_state.last_events[ts] = it->type();
    }

    // Keep track of the timestamp of the first and last events of the trace.
    if (ts != 0) {
      if (system_history->first_event_ts() == 0 ||
          system_history->first_event_ts() > ts) {
        system_history->set_first_event_ts(ts);
      }
      if (system_history->last_event_ts() == base::kInvalidTimestamp ||
          system_history->last_event_ts() < ts) {
        system_history->set_last_event_ts(ts);
      }
    }

    // Stop parsing the trace.
    if (should_stop)
      break;
  }

  return true;
}

}  // namespace etw_insights
