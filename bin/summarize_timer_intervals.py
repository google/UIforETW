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
This script extracts timer frequency/interval data from an ETW trace and
summarizes it by process in order to understand what processes are raising the
system-global timer interrupt frequency, how frequently, and for how long. The
trace can be one recorded by UIforETW or, if you are just interested in timer
information, one recorded by trace_timer_intervals.bat.

For more information on why this matters see this blog post:
https://randomascii.wordpress.com/2013/07/08/windows-timer-resolution-megawatts-wasted/

The initial trace conversion is done using wpaexporter. See
https://randomascii.wordpress.com/2013/11/04/exporting-arbitrary-data-from-xperf-etl-files/
for details on wpaexporter.

The output from wpaexporter goes to Generic_Events_Timer_Intervals_by_Process.csv.
The first line is column labels and the rest is the fields from the profile.

The one known flaw is that if a process raises the timer interrupt frequency
before tracing starts and then lowers it towards the end of the trace then this
will not be tracked because we have no way of knowing what frequency the
proccess requested.
'''

from __future__ import print_function

import os
import re
import subprocess
import sys
import time

# Our usage of subprocess seems to require Python 2.7+
if sys.version_info.major == 2 and sys.version_info.minor < 7:
  print('Your python version is too old - 2.7 or higher required.')
  print('Python version is %s' % sys.version)
  sys.exit(0)

if len(sys.argv) < 2:
  print('Usage: %s tracename.etl')
  sys.exit(0)

trace_name = sys.argv[1]

scriptPath = os.path.abspath(sys.argv[0])
scriptDir = os.path.split(scriptPath)[0]

profilePath = os.path.join(scriptDir, 'timer_intervals.wpaProfile')
if not os.path.exists(profilePath):
  print('Couldn\'t find \"%s\". This should be part of the UIforETW repo and releases' % profilePath)
  sys.exit(0)

if os.environ.has_key('ProgramFiles(x86)'):
  progFilesx86 = os.environ['ProgramFiles(x86)']
else:
  progFilesx86 = os.environ['ProgramFiles']
wpaExporterPath = os.path.join(progFilesx86, r'Windows Kits\10\Windows Performance Toolkit\wpaexporter.EXE')
if not os.path.exists(wpaExporterPath):
  print('Couldn\'t find "%s". Make sure WPT 10 is installed.' % wpaExporterPath)
  sys.exit(0)

wpaCommand = r'"%s" "%s" -profile "%s"' % (wpaExporterPath, trace_name, profilePath)

print('> %s' % wpaCommand)
print(subprocess.check_output(wpaCommand, stderr=subprocess.STDOUT))

# This dictionary of lists accumulates the data. The key is a process (pid) name
# and the payload is a list containing interval/timestamp pairs. The timer
# interrupt intervals are converted from hexadecimal 100ns units to floats
# representing milliseconds.
changes_by_process = {}

# This tracks how many fake events were added, by process (pid) name. Fake
# events are added to close out timer intervals and, for processes that leave
# the timer interrupt raised for the entire trace, for the start and finish.
# Note that a process that lowers the timer interrupt just before the end of
# the trace will not properly be recorded.
fake_events_added = {}

# Process all of the lines in the output of wpaexporter, skipping the first line
# which is just the column names.
csv_name = 'Generic_Events_Timer_Intervals_by_Process.csv'
# Skip over the header line
for line in open(csv_name).readlines()[1:]:
  parts = line.strip().split(',')
  if len(parts) < 7:
    print(line.strip())
    continue
  task_name, process_name, process, interval, timestamp, PID, app_name = parts
  # Convert from hex (with leading 0x) to decimal and from 100 ns to ms units.
  interval = int(interval[2:], 16) * 1e-4
  timestamp = float(timestamp)
  if task_name == 'SystemTimeResolutionRequestRundown':
    process = os.path.split(app_name)[1] + ' (%d)' % int(PID[2:], 16)
    if process in changes_by_process:
      fake_events_added[process] = 1
    else:
      fake_events_added[process] = 2
      # Add an entry at time==0 to so that this timer that was raised the entire
      # time gets accounted for, then fall through to close it out.
      changes_by_process[process] = [(interval, 0)]
    # Fall through to add to list to ensure that the final time segment is
    # closed out.
  list = changes_by_process.get(process, [])
  list.append((interval, timestamp))
  changes_by_process[process] = list
  final_timestamp = timestamp

# Get process-type information for Chrome processes.
import IdentifyChromeProcesses
types_by_pid = IdentifyChromeProcesses.GetPIDToTypeMap(trace_name)

print('Trace duration is %1.3f seconds.' % final_timestamp)
for process in changes_by_process.keys():
  entries = changes_by_process[process]
  # Scan through all adjacent pairs to find how long the interval was at various
  # levels.
  intervals = {}
  last_interval, last_timestamp = entries[0]
  for interval, timestamp in entries[1:]:
    data = intervals.get(last_interval, 0.0)
    intervals[last_interval] = data + (timestamp - last_timestamp)
    last_timestamp = timestamp
    last_interval = interval
  fake_events_count = 0
  ps = '(%1.1f/s)' % (len(entries) / final_timestamp)
  if process in fake_events_added:
    fake_events_count = fake_events_added[process]
    if fake_events_count == len(entries):
      ps = '- frequency still raised at trace end'
    else:
      ps = '(%1.1f/s) - frequency still raised at trace end' % ((len(entries) - fake_events_count) / final_timestamp)
  type_name = ''
  pid_match = re.match(r'.*\((\d+)\)', process)
  if pid_match:
    pid = int(pid_match.groups()[0])
    if pid in types_by_pid:
      type_name = ' (Chrome %s)' % types_by_pid[pid]
  print('%s%s: %d frequency changes %s' % (process, type_name, len(entries) - fake_events_count, ps))
  if len(entries) == 1 and fake_events_count == 0:
    if entries[0][0] == 0:
      print('  timeEndPeriod called at %1.3f s' % entries[0][1])
    else:
      # This branch should never be hit.
      print('  %1.1f ms set at %1.3f s' % entries[0])
  for interval in intervals.keys():
    if interval > 0:
      print('  %1.1f ms for %5.1f%% of the time' % (interval, 100 * intervals[interval] / final_timestamp))
