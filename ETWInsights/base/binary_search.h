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

namespace base {

// Finds the last element of a vector that compares smaller or equal to |val|.
// @param vec the vector in which to search.
// @param val the value used for comparison.
// @param comparator the comparison function or function object.
// @returns an iterator to the last element of |vec| that is smaller or equal
//    to |val|, of .end() if none exists.
template <typename container_type,
          typename comparison_value_type,
          typename comparator_type>
auto FindSmallerOrEqual(container_type& vec,
                        comparison_value_type& val,
                        comparator_type comparator) -> decltype(vec.begin()) {
  auto it = std::upper_bound(vec.begin(), vec.end(), val, comparator);
  if (it == vec.begin())
    return vec.end();

  --it;

  return it;
}

}  // namespace base
