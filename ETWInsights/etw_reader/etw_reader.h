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

#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/base.h"

namespace etw_insights {

// Reads an ETW trace.
class ETWReader {
 public:
  class Iterator;

  // A line of an ETW trace dumped into a CSV file.
  class Line {
   public:
    Line();

    const std::string& type() const { return type_; }

    bool GetFieldAsString(const std::string& name, std::string* value) const;
    bool GetFieldAsULong(const std::string& name, uint64_t* value) const;
    bool GetFieldAsULongHex(const std::string& name, uint64_t* value) const;

   private:
    friend class etw_insights::ETWReader::Iterator;

    std::string type_;
    std::unordered_map<std::string, std::string> values_;
  };

  // Iterates through the events of an ETW trace.
  class Iterator {
   public:
    Iterator();

    const Line& operator*() const { return current_line_; }
    const Line* operator->() const { return &current_line_; }
    bool operator==(const Iterator& other) const;
    bool operator!=(const Iterator& other) const;
    Iterator& operator++();

   private:
    friend class etw_insights::ETWReader;

    Iterator(const std::wstring& file_path);

    void ParseHeader();

    // CSV file stream.
    std::ifstream file_;

    // CSV header: Line type -> Column names.
    std::unordered_map<std::string, std::vector<std::string>> header_;

    // Current line index.
    size_t current_line_index_;

    // Current line.
    Line current_line_;
  };

  ETWReader();

  // Opens an ETW trace.
  // @param trace_path Path to a .etl file.
  // @returns true if the trace was opened successfully, false otherwise.
  bool Open(const std::wstring& trace_path);

  // Returns an iterator to the first event of an ETW trace.
  Iterator begin() const;

  // Returns an iterator to the end of an ETW trace.
  Iterator end() const;

  // Empty event type.
  static const char* kEmptyEventType;

 private:
  // Path to the CSV dump of an ETW trace.
  std::wstring csv_file_path_;

  DISALLOW_COPY_AND_ASSIGN(ETWReader);
};

}  // namespace etw_insights
