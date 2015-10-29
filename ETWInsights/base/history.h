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

#include <algorithm>
#include <vector>

#include "base/binary_search.h"
#include "base/logging.h"
#include "base/types.h"

namespace base {

// An History associates values to time ranges.
template <typename T>
class History {
 public:
  // An element of the history.
  struct Element {
    Element(const base::Timestamp& start_ts, const T& value)
        : start_ts(start_ts), value(value) {}

    // Time at which the element starts.
    base::Timestamp start_ts;

    // Value of the element.
    T value;
  };

  typedef std::vector<Element> HistoryContainer;
  typedef typename HistoryContainer::iterator HistoryIterator;
  typedef typename HistoryContainer::const_iterator HistoryConstIterator;

  History() {}

  // Inserts a new value at the end of the history.
  // @param ts start timestamp for the inserted value.
  // @param value the value to insert.
  // @returns true if the value is inserted successfully. Returns false if |ts|
  //    is earlier than the timestamp of the last inserted value.
  bool Insert(const base::Timestamp& start_ts, const T& value);

  // Gets the value for the specified timestamp.
  // @param ts timestamp for which to obtain the value.
  // @param value value for the specified timestamp.
  // @returns true if there is a value at the specified timestamp, false
  //    otherwise.
  bool GetValue(const base::Timestamp& ts, const T** value) const;

  // Gets the value of the last inserted element.
  // @param value the last value, output.
  // @returns true if there is at least one element in the history, false
  //    otherwise.
  bool GetLastElementValue(T* value) const;

  // Gets the timestamp of the last inserted element.
  // @param ts the last timestamp, output.
  // @returns true if there is at least one element in the history, false
  //    otherwise.
  bool GetLastElementTimestamp(base::Timestamp* ts) const;

  // Returns an iterator to the first element of the history that ends after the
  // specified timestamp.
  // @param ts timestamp at which the iteration starts.
  // @returns an iterator to the first value of the history that ends after the
  //    specified timestamp.
  HistoryIterator IteratorFromTimestamp(const base::Timestamp& ts);
  HistoryConstIterator IteratorFromTimestamp(const base::Timestamp& ts) const;

  // @returns an iterator to the end of the history.
  HistoryIterator IteratorEnd();
  HistoryConstIterator IteratorEnd() const;

  // @returns the number of elements in the history.
  size_t size() const { return history_.size(); }

 private:
  // Elements of the history, sorted by start timestamp.
  HistoryContainer history_;
};

template <typename T>
bool History<T>::Insert(const base::Timestamp& start_ts, const T& value) {
  if (!history_.empty()) {
    if (history_.back().start_ts >= start_ts)
      return false;
    if (history_.back().value == value)
      return true;
  }

  history_.push_back(Element(start_ts, value));
  return true;
}

template <typename T>
bool History<T>::GetValue(const base::Timestamp& ts, const T** value) const {
  DCHECK(value != nullptr);
  auto it = base::FindSmallerOrEqual(
      history_, ts, [](const base::Timestamp& ts, const Element& value) {
        return ts < value.start_ts;
      });

  if (it == history_.end())
    return false;

  *value = &it->value;
  return true;
}

template <typename T>
bool History<T>::GetLastElementValue(T* value) const {
  DCHECK(value != nullptr);
  if (history_.empty())
    return false;

  *value = history_.back().value;
  return true;
}

template <typename T>
bool History<T>::GetLastElementTimestamp(base::Timestamp* ts) const {
  DCHECK(ts != nullptr);
  if (history_.empty())
    return false;

  *ts = history_.back().start_ts;
  return true;
}

template <typename T>
typename History<T>::HistoryIterator History<T>::IteratorFromTimestamp(
    const base::Timestamp& ts) {
  typename History<T>::HistoryIterator it = base::FindSmallerOrEqual(
      history_, ts, [](const base::Timestamp& ts, const Element& value) {
        return ts < value.start_ts;
      });

  if (it == history_.end())
    return history_.begin();

  return it;
}

template <typename T>
typename History<T>::HistoryConstIterator History<T>::IteratorFromTimestamp(
    const base::Timestamp& ts) const {
  auto it = base::FindSmallerOrEqual(
      history_, ts, [](const base::Timestamp& ts, const Element& value) {
        return ts < value.start_ts;
      });

  if (it == history_.end())
    return history_.begin();

  return it;
}

template <typename T>
typename History<T>::HistoryIterator History<T>::IteratorEnd() {
  return history_.end();
}

template <typename T>
typename History<T>::HistoryConstIterator History<T>::IteratorEnd() const {
  return history_.end();
}

}  // namespace base
