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

#include "etw_reader/etw_reader.h"

#include "base/child_process.h"
#include "base/file.h"
#include "base/logging.h"
#include "base/numeric_conversions.h"
#include "base/string_utils.h"

namespace etw_insights {

namespace {
// CSV column separator.
const char kSeparator[] = ",";

// Extension for a CSV file.
const wchar_t kCSVFileExtension[] = L".csv";

// Invalid line index.
const size_t kInvalidLineIndex = static_cast<size_t>(-1);

std::vector<std::string> ExtractTokens(const std::string& str) {
  std::vector<std::string> tokens(base::SplitString(str, kSeparator));
  for (auto& token : tokens)
    token = base::Trim(token);
  return tokens;
}

std::wstring ConvertEtlToCsv(const std::wstring& etl_path) {
  // Generate the name of the CSV file.
  std::wstring csv_path = etl_path + kCSVFileExtension;

  // Check if the CSV file already exists.
  if (base::FilePathExists(csv_path))
    return csv_path;

  // Tell the user what we are doing.
  LOG(INFO) << "Converting trace file to CSV format." << std::endl;

  // Generate the CSV file.
  base::ChildProcess xperf_process;
  xperf_process.SetOutputPath(csv_path);
  std::wstringstream command;
  command << L"xperf -i \"" << etl_path << "\" -symbols";
  xperf_process.Run(command.str());

  return csv_path;
}
}  // namespace

const char* ETWReader::kEmptyEventType = "Empty";

ETWReader::Line::Line() {}

bool ETWReader::Line::GetFieldAsString(const std::string& name,
                                       std::string* value) const {
  auto look = values_.find(name);
  if (look == values_.end())
    return false;
  *value = look->second;
  return true;
}

bool ETWReader::Line::GetFieldAsULong(const std::string& name,
                                      uint64_t* value) const {
  std::string value_str;
  if (!GetFieldAsString(name, &value_str))
    return false;
  return base::StrToULong(value_str, value);
}

bool ETWReader::Line::GetFieldAsULongHex(const std::string& name,
                                         uint64_t* value) const {
  std::string value_str;
  if (!GetFieldAsString(name, &value_str))
    return false;
  return base::StrToULongHex(value_str, value);
}

ETWReader::Iterator::Iterator() : current_line_index_(kInvalidLineIndex) {}

bool ETWReader::Iterator::operator==(const ETWReader::Iterator& other) const {
  return current_line_index_ == other.current_line_index_;
}

bool ETWReader::Iterator::operator!=(const ETWReader::Iterator& other) const {
  return current_line_index_ != other.current_line_index_;
}

ETWReader::Iterator& ETWReader::Iterator::operator++() {
  // Reset the current line values.
  current_line_.values_.clear();

  // Read the current line.
  std::string line;
  if (!std::getline(file_, line)) {
    current_line_index_ = kInvalidLineIndex;
    return *this;
  }
  ++current_line_index_;
  auto tokens = ExtractTokens(line);

  // Check if the current line is empty.
  if (tokens.empty()) {
    current_line_.type_ = kEmptyEventType;
    return *this;
  }

  // Set the current line type.
  current_line_.type_ = tokens.front();

  // Get the column names for this line type.
  auto look_column_names = header_.find(current_line_.type());
  if (look_column_names == header_.end()) {
    current_line_.values_.clear();
    return *this;
  }
  const auto& column_names = look_column_names->second;

  // Check that we got the expected number of tokens.
  if ((tokens.size() - 1) < column_names.size()) {
    LOG(ERROR) << "Unexpected number of tokens for line of type "
               << current_line_.type() << ".";
    return *this;
  }

  // Create a map of Column Name -> Column Value for the current line.
  for (size_t column_index = 0; column_index < column_names.size();
       ++column_index) {
    size_t token_index = column_index + 1;
    std::string value = tokens[token_index];

    // If this is the last column, use all the remaining tokens as the value.
    // TODO(fdoray): Find a cleaner solution.
    if (column_index == column_names.size() - 1) {
      ++token_index;
      while (token_index < tokens.size()) {
        value += ",";
        value += tokens[token_index];
        ++token_index;
      }
    }

    current_line_.values_[column_names[column_index]] = value;
  }

  return *this;
}

ETWReader::Iterator::Iterator(const std::wstring& file_path)
    : current_line_index_(static_cast<size_t>(0)) {
  // Open the CSV file.
  file_.open(file_path);

  // Parse the header.
  ParseHeader();

  // Skip the line with the trace metadata.
  std::string dummy;
  std::getline(file_, dummy);
  ++current_line_index_;

  // Read the first event line.
  ++(*this);
}

void ETWReader::Iterator::ParseHeader() {
  bool read_first_line = false;
  std::string line;
  while (std::getline(file_, line)) {
    ++current_line_index_;

    // Ignore the first line.
    if (!read_first_line) {
      read_first_line = true;
      continue;
    }

    // Stop when the EndHeader line is encountered.
    if (line == "EndHeader")
      break;

    // Extract tokens names from the line.
    auto tokens = ExtractTokens(line);

    // The first token is the line type.
    auto type = tokens.front();

    // The other tokens are column names.
    auto column_names =
        std::vector<std::string>(tokens.begin() + 1, tokens.end());

    // Save the Line type -> Column names pair.
    header_[type] = column_names;
  }
}

ETWReader::ETWReader() {}

bool ETWReader::Open(const std::wstring& trace_path) {
  // Check that the ETL file exists.
  if (!base::FilePathExists(trace_path)) {
    LOG(ERROR) << "Trace file " << base::WStringToString(trace_path)
               << " doesn't exist.";
    return false;
  }

  // Convert the ETL file to CSV.
  csv_file_path_ = ConvertEtlToCsv(trace_path);

  return true;
}

ETWReader::Iterator ETWReader::begin() const {
  DCHECK(!csv_file_path_.empty());
  return Iterator(csv_file_path_);
}

ETWReader::Iterator ETWReader::end() const {
  return Iterator();
}

}  // namespace etw_insights
