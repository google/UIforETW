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

#include "etw_reader/stack.h"

#include <string>

namespace etw_insights {

// Cleans a call stack.
// - Tall call stacks are truncated.
// - Frames related to page faults are replaced by [Page Fault].
// - Uninteresting frames are removed.
Stack CleanStack(const Stack& stack);

}  // namespace etw_insights
