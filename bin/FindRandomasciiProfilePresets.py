# Copyright 2019 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

'''
This script scans a .wpaProfile file looking for presets that start with
Randomascii. These are the custom presets shipped with UIforETW. When updating
the startup profile it is easy for these to get deleted, so a tool for auditing 
changes is quite helpful.
'''

from __future__ import print_function

import re
import sys

if len(sys.argv) < 2:
  print('Usage: %s profilename.wpaProfile' % sys.argv[0])
  print('Prints a list of Randomascii presets in a .wpaProfile file.')
  sys.exit(0)

count = 0
for line in open(sys.argv[1]).readlines():
  match = re.match(r'.*<Preset Name="(Randomascii[^"]*)".*', line, flags=re.IGNORECASE)
  if match:
    print('  %s' % match.groups()[0])
    count += 1

print('Found %d presets.' % count)
