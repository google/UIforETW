# Copyright 2021 Google Inc. All Rights Reserved.
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
This script summarizes VirtualFree data from an ETW trace. You need to record
an ETW trace with the VIRT_ALLOC provider (on by default with UIforETW) and
also have VirtualAlloc+VirtualFree specified in the set of stack walks. The
VirtualAlloc+VirtualFree stack walks are off by default in UIforETW but you can
enable them by checking "VirtualAlloc stacks always" in the settings dialog.

This script is necessary because WPA and the Trace Processor library don't seem
to offer a way to summarize VirtualFree stacks. This can be extremely useful if
you encounter a process that has allocated a lot of memory but you don't know
where it allocated it. If it frees it during shutdown and you record a trace
then you can get the free call stacks.

This script defaults to summarizing the processes that have freed the most
memory. It then prints out the call stacks for the process that freed the most.

The text file which this file parses was created with this xperf command:
     xperf -i trace.etl -symbols -target machine -o output.csv -a dumper
'''

from __future__ import print_function

import os
import sys
import re

def main():
  if len(sys.argv) < 2:
    print('Required argument missing.')
    print('Usage:')
    print('    %s <tracesummary.csv>' % sys.argv[0])
    print('tracesummary.csv should be created beforehand like this:')
    print('xperf -i trace.etl -symbols -target machine -o tracesummary.csv -a dumper')
    return 0

  if os.path.splitext(sys.argv[1])[1].lower() == '.etl':
    print('Argument should be a .csv file, not a .etl file.')
    return 0

  total = 0
  total_by_process = {}
  total_by_stacks = {}

  # The VirtualFree lines are in this form:
  #             VirtualFree,  TimeStamp,     Process Name ( PID),           BaseAddr,            EndAddr, Flags
  # Typical lines look like this:
  #             VirtualFree,     206764,       chrome.exe (22128), 0x00001a0c08700000, 0x00001a0c08740000,  DECOMMIT
  # The next line should look like this (the '13012' value is presumably the thread ID):
  #                   Stack,     206764,      13012, ntoskrnl.exe!MmFreeVirtualMemory, ntoskrnl.exe!NtFreeVirtualMemory, 

  # If not-None then this records where dictionary where the stack data should be
  # stored.
  stack_store = None
  for line in open(sys.argv[1]):
    if stack_store != None and line.startswith('                  Stack'):
      stack_match = re.match(r' *Stack, *\d*, *\d*, (.*)', line)
      if stack_match:
        stack = stack_match.groups()[0]
        # Use amount from the last loop and count up how many bytes and how many
        # allocations came in on this stack.
        stack_total, stack_count = stack_store.get(stack, (0, 0))
        stack_store[stack] = (stack_total + amount, stack_count + 1)
    stack_store = None
    if not line.startswith('            VirtualFree'):
      continue
    parts = line.split(', ')
    if len(parts) != 6:
      continue
    if parts[5].strip() != 'DECOMMIT':
      continue
    base = int(parts[3], 16)
    end = int(parts[4], 16)
    amount = end - base
    total += amount
    process = parts[2].strip()
    total_by_process[process] = total_by_process.get(process, 0) + amount
    total_by_stacks[process] = stack_store = total_by_stacks.get(process, {})

  print('%1.1f MB decommitted, mostly by these processes:' % (total / 1e6))
  sorted = []
  for process in total_by_process.keys():
    sorted.append((total_by_process[process], process))

  sorted.sort()
  sorted.reverse()

  for record in sorted[:10]:
    print('%20s: %1.1f MB decommitted.' % (record[1], record[0] / 1e6))

  print()
  biggest_process = sorted[0][1]
  print('%d stacks found in %s' % (len(total_by_stacks[biggest_process]),
        biggest_process))

  # Now print all of the free stacks for the biggest process.
  sorted_stacks = []
  for stack in total_by_stacks[biggest_process].keys():
    sorted_stacks.append((total_by_stacks[biggest_process][stack], stack))

  sorted_stacks.sort()
  sorted_stacks.reverse()

  for record in sorted_stacks[:10]:
    print('%1.1f MB decommitted in %d calls from stack:' %
          (record[0][0] / 1e6, record[0][1]))
    print('    %s' % record[1].replace(', ', '\n    '))


if __name__ == '__main__':
  main()
