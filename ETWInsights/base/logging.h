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

#include <cstdlib>
#include <sstream>

#include "base/base.h"

namespace base {

enum LogSeverity { LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_FATAL };

class LogMessage {
 public:
  LogMessage(LogSeverity severity, const char* file, int line)
      : severity_(severity), file_(file), line_(line) {}

  ~LogMessage();

  std::ostream& stream() { return stream_; }

 private:
  std::ostringstream stream_;
  LogSeverity severity_;
  const char* file_;
  const int line_;

  DISALLOW_COPY_AND_ASSIGN(LogMessage);
};

#define LOG(severity) \
  base::LogMessage(base::LOG_##severity, __FILE__, __LINE__).stream()

#ifndef NDEBUG
#define DCHECK(cond) \
  if (!(cond))       \
  LOG(FATAL) << "'" << #cond << "' failed.\n"
#else
#define DCHECK(cond) (void)(cond);
#endif

#define DCHECK_EQ(a, b) DCHECK((a) == (b))
#define DCHECK_NE(a, b) DCHECK((a) != (b))
#define DCHECK_LT(a, b) DCHECK((a) < (b))
#define DCHECK_LE(a, b) DCHECK((a) <= (b))
#define DCHECK_GT(a, b) DCHECK((a) > (b))
#define DCHECK_GE(a, b) DCHECK((a) >= (b))

}  // namespace base
