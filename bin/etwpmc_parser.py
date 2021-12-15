# Copyright 2016 Google Inc. All Rights Reserved.
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

"""
The data created by recording CPU performance counters (Pmc) for each context switch looks something like this:
                CSwitch,  TimeStamp, New Process Name ( PID),    New TID, NPri, NQnt, TmSinceLast, WaitTime, Old Process Name ( PID),    Old TID, OPri, OQnt,        OldState,      Wait Reason, Swapable, InSwitchTime, CPU, IdealProc,  OldRemQnt, NewPriDecr, PrevCState
                    Pmc,  TimeStamp,   ThreadID, BranchInstructions, BranchMispredictions
...
                CSwitch,      64630,             Idle (   0),          0,    0,   -1,           0,        0,     tracelog.exe (5200),       5912,    9,   -1,         Standby,      WrPreempted,  NonSwap,      6,   6,   7,   14768128,    0,    0
                CSwitch,      64631,     tracelog.exe (5200),       5912,    9,   -1,           1,        1, RuntimeBroker.exe (3896),       7720,    8,   -1,           Ready,    WrDispatchInt,  NonSwap,      6,   7,   7,   32640000,    0,    0
                CSwitch,      64648,      MsMpEng.exe (3016),      13212,    8,   -1,       19604,        2,             Idle (   0),          0,    0,   -1,         Running,        Executive,  NonSwap,   1465,   0,   0,          0,    0,    1
                    Pmc,      64662,       7720, 41066, 6977
                CSwitch,      64662, RuntimeBroker.exe (3896),       7720,    8,   -1,          31,        0,      MsMpEng.exe (3016),      13212,    8,   -1,         Waiting,          WrQueue, Swapable,     14,   0,   4,   68564992,    0,    0
                    Pmc,      64690,          0, 6723, 1485
                CSwitch,      64690,             Idle (   0),          0,    0,   -1,           0,        0,     tracelog.exe (5200),       5912,    9,   -1,         Waiting,        Executive,  NonSwap,     59,   7,   2,   14640128,    0,    0
                    Pmc,      64693,       7904, 34481, 3028
                CSwitch,      64693,      conhost.exe (8148),       7904,   11,   -1,        4243,        1,             Idle (   0),          0,    0,   -1,         Running,        Executive,  NonSwap,   1407,   2,   2,          0,    2,    1
                    Pmc,      64704,          0, 36020, 3267
                CSwitch,      64704,             Idle (   0),          0,    0,   -1,           0,        0,      conhost.exe (8148),       7904,   11,   -1,         Waiting,      UserRequest, Swapable,     12,   2,   6,  202464256,    0,    0
                    Pmc,      64710,       5912, 7077, 1518
                CSwitch,      64710,     tracelog.exe (5200),       5912,    9,   -1,          19,        0,             Idle (   0),          0,    0,   -1,         Running,        Executive,  NonSwap,     19,   7,   7,          0,    0,    1

A few things can be observed about the data.

The Pmc data takes a while to get going - there can be thousands of CSwitch events
before the first Pmc event. Awesome.

The Pmc events are cumulative and per-processor. This explains why they increase over the
duration of the trace, but not monotonically. They only increase monotonically if you look
at them on a particular CPU.

The Pmc events are associated with the following event. This can be seen in the CSwitch at TimeStamp
64704. This CSwitch is on CPU 2 and the following Pmc has a BranchInstructions count of 7077, which
is incompatible with the previous CSwitch which is also on CPU 2.

The CSwitch events are when a thread *starts* executing. So, you don't know what counts to associate
with a timeslice until the *next* context switch on that CPU. So...

When a Pmc event is seen, look for a CSwitch event on the next line. If this is not the first Pmc/CSwitch
pair for this CPU (see column 16) then calculate the deltas for all of the Pmc counters and add those
deltas to the process listed in the Old Process Name ( PID) column (column 8).

Sometimes there will be an Error: message inbetween the Pmc and CSwitch lines. Ignore those, but don't
be too forgiving about what you parse or else you may end up calculating garbage results.

Example:

                    Pmc,    2428274,         84, 45813769, 2146039
Error: Description for thread state (9) could not be found. Thread state array out of date!!
                CSwitch,    2428274,           System (   4),         84,   23,   -1,         220,        0,        csrss.exe ( 628),        732,   14,   -1,  <out of range>,  WrProcessInSwap, Swapable,     19,   2,   4,   68552704,    0,    0
 
"""

from __future__ import print_function

import re
import sys

if len(sys.argv) <= 1:
  print('Usage: %s xperfoutput [processname]' % sys.argv[0])
  print('The first parameter is the name of a file containing the results')
  print('of "xperf -i trace.etl". The second (optional) parameter is a')
  print('process name substring filter used to restrict which results are')
  print('shown - only processes that match are displayed.')
  sys.exit(0)

xperfoutputfilename = sys.argv[1]

l = open(xperfoutputfilename).readlines()

# Scan through the counter data looking for Pmc and CSwitch records.
# If adjacent records are found that contain Pmc and CSwitch data then
# combine the data. This gives us some counters that we can assign to
# a particular CPU. If we have already seen counters for that CPU then
# we can subtract the previous counters to get a delta.
# That delta can then be applied to the process that was *previously*
# assigned to that CPU.

lastLineByCPU = {}
countersByCPU = {}
lastCSwitchTimeByCPU = {}
countersByProcess = {}
contextSwitchesByProcess = {}
cpuTimeByProcess = {}
processByCPU = {} # Which process has been switched in to a particular CPU
description = None

for x in range(len(l) - 1):
  if l[x].startswith('                    Pmc,'):
    pmc_parts = l[x].split(',')
    if not description:
      # Grab the description of the Pmc counter records, see how many counters
      # there are, and print the description.
      num_counters = len(pmc_parts) - 3
      description = l[x].strip()
      continue
    counters = list(map(int, pmc_parts[3:]))
    assert(len(counters) == num_counters)
    # Look for a CSwitch line. Ideally it will be next, but sometimes an Error: line
    # might be in-between.
    cswitch_line = ''
    if l[x+1].startswith('                CSwitch,'):
      cswitch_line = l[x+1]
    elif l[x+1].startswith('Error: ') and l[x+2].startswith('                CSwitch,'):
      cswitch_line = l[x+2]
    if cswitch_line:
      cswitch_parts = cswitch_line.split(',')
      CPU = int(cswitch_parts[16].strip())
      process = cswitch_parts[2].strip()
      timeStamp = int(cswitch_parts[1])
      # See if we've got previous Pmc records for this CPU:
      if CPU in countersByCPU:
        diffs = list(map(lambda a,b : a - b, counters, countersByCPU[CPU]))
        old_process = cswitch_parts[8].strip()
        # Sanity checking...
        if old_process != processByCPU[CPU]:
          print('Old process mismatch at line %d, %s versus %s' % (x, old_process, processByCPU[CPU]))
          sys.exit(0)
        if old_process != 'Idle (   0)':
          countersByProcess[old_process] = list(map(lambda x, y: x + y, countersByProcess.get(old_process, num_counters * [0]), diffs))
          assert(len(countersByProcess[old_process]) == num_counters)
          contextSwitchesByProcess[old_process] = contextSwitchesByProcess.get(old_process, 0) + 1
          cpuTimeByProcess[old_process] = cpuTimeByProcess.get(old_process, 0) + (timeStamp - lastCSwitchTimeByCPU[CPU])

      lastCSwitchTimeByCPU[CPU] = timeStamp
      processByCPU[CPU] = process
      countersByCPU[CPU] = counters
      lastLineByCPU[CPU] = x
    else:
      print('Missing cswitch line at line %d' % x)
      sys.exit(0)

summarizeByName = True
filter_by_process_name = False
if len(sys.argv) == 2:
  mincounter = 500000
  print('Printing collated data for process names where the second counter exceeds %d' % mincounter)
else:
  mincounter = 0
  filter_substring = sys.argv[2].lower()
  print('Printing per-process-data for processes that contain "%s"' % filter_substring)
  filter_by_process_name = True
  # If we are filtering to a specific process name then we cannot
  # also summarize by name.
  summarizeByName = False

counterDescription = '<unknown counters>'
if description:
  counterDescription = ',' + ','.join(description.split(',')[3:])

print('%43s: cnt1/cnt2%s' % ('Process name', counterDescription))
totals_to_print = {}
for process in countersByProcess.keys():
  totals = countersByProcess[process]
  assert(len(totals) == num_counters)
  # Extract the .exe name and separate it from the PID.
  match = re.match(r'(.*).exe \(\d+\)', process)
  if match:
    if summarizeByName:
      procname = match.groups()[0]
    else:
      # If not summarizing by name then we group data by name (PID).
      procname = process
    # First num_counters values are the CPU performance counters. The next two are contextSwitches and cpuTime.
    data = totals_to_print.get(procname, (num_counters + 2) * [0])
    # Extend the totals list so that it also contains contextSwitches and cpuTime
    totals += [contextSwitchesByProcess[process], cpuTimeByProcess[process]]
    if not filter_by_process_name or process.lower().count(filter_substring) > 0:
      totals_to_print[procname] = list(map(lambda x, y: x + y, data, totals))

if totals_to_print:
  # Put one of the values and the keys into tuples, sort by the selected
  # value, extract back out into two lists and grab the keys, which are
  # now sorted by the specified value. The index in the map lambda specifies
  # which of the values from the stored tuple is used for sorting. The -1
  # index is the time field (unclear units).
  sortingValues = list(map(lambda x: x[-1], totals_to_print.values()))
  orderedKeys = list(list(zip(*sorted(zip(sortingValues, totals_to_print.keys()))))[1])
  orderedKeys.reverse()

  format = '%43s: %6.2f%%,   [' + ','.join(num_counters * ['%11d']) + '], %5d context switches, time: %8d'

  for procname in orderedKeys:
    totals0, totals1 = totals_to_print[procname][:2]
    # Arbitrary filtering to just get the most interesting data.
    if totals1 > mincounter:
      args = tuple([procname, totals0 * 100.0 / totals1] + totals_to_print[procname])
      print(format % args)
