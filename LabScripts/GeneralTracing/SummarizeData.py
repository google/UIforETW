# Copyright 2017 Google Inc. All Rights Reserved.
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
This script looks for *Summary*.csv files and summarizes them into various
buckets:
- Browser (chrome.exe or Edge processes)
- dwm.exe
- audiodg.exe
- System
- other

The summarized data is currently CPU usage, GPU usage and private working set.

Sample usage:
    call python SummarizeData.py OutputDir
"""

import csv
import json
import os
import re
import sys

process_name_re = re.compile(r'(.*) \(\d+\)')

def Summarize(filename, interesting_processes, label, units, results,
      count_browser_processes=False):
  chrome_processes = ['chrome.exe']
  edge_processes = ['MicrosoftEdge.exe <MicrosoftEdge>',
                    'MicrosoftEdgeCP.exe <MicrosoftEdge>',
                    'MicrosoftEdgeCP.exe <ContentProcess>']
  staging = {}
  for process_name in interesting_processes:
    staging[process_name] = 0.0
  staging['Other'] = 0.0
  staging['Browser'] = 0.0

  is_edge = False
  is_chrome = False
  record_other_process = True
  interesting_processes = (
      chrome_processes + edge_processes + interesting_processes)
  other_process = None

  first_line = True
  browser_process_count = 0
  with open(filename) as file_handle:
    for row in csv.reader(
        file_handle, delimiter = ',', quotechar = '"', skipinitialspace=True):
      if first_line:
        first_line = False
        continue
      # These two don't actually represent memory consumption in any normal
      # sense, so skip them.
      if row[0].startswith('PagedPoolWs') or row[0].startswith('SystemCacheWs'):
        continue
      match = process_name_re.match(row[0])
      # Anything that doesn't match the process-name pattern, like "Unknown" is
      # tagged as unknown.
      if match:
        process_name = match.groups()[0]
      else:
        process_name = 'Unknown'
      # Don't count resource usage from Idle. Memory and GPU usage should be
      # zero or non-existent.
      if process_name == 'Idle':
        continue

      payload = float(row[-1].replace(',', ''))
      if process_name in interesting_processes:
        if process_name in edge_processes:
          process_name = 'Browser'
          is_edge = True
          browser_process_count += 1
        if process_name in chrome_processes:
          process_name = 'Browser'
          is_chrome = True
          browser_process_count += 1
        staging[process_name] += payload
      else:
        if record_other_process:
          other_process = process_name
          record_other_process = False
        staging['Other'] += payload

  assert (not is_edge) or (not is_chrome), (
      'Both Chrome and Edge are running, and these lab scripts are '
      'set up to only measure one of them at a time.')

  # Append the dictionary of results to an array of dictionaries.
  for key in staging.keys():
    results.append({'label' : key + ' ' + label,
                    'value' : staging[key],
                    'units' : units})
  if other_process:
    results.append({'label' : 'Biggest other ' + label,
                    'value' : other_process})
  if count_browser_processes:
    results.append({'label' : 'Browser process count',
                    'value' : browser_process_count})


def main():
  data_dir = sys.argv[1]

  results = []
  Summarize(os.path.join(data_dir,
              'CPU_Usage_(Precise)_Randomascii_CPU_Summary_by_Process.csv'),
            ['dwm.exe', 'audiodg.exe', 'System'], 'CPU usage', 'ms', results)
  Summarize(os.path.join(data_dir,
              'GPU_Utilization_Table_Randomascii_GPU_Summary_by_Process.csv'),
            ['dwm.exe', 'csrss.exe'], 'GPU usage', 'ms', results)
  Summarize(os.path.join(data_dir,
              'Virtual_Memory_Snapshots_Randomascii_Private_Working_Set_Summary'
              '_by_Process.csv'),
            [], 'Private Working set', 'MiB', results,
            count_browser_processes=True)

  json.dump(results, open(os.path.join(data_dir, 'results.json'), 'wt'))


if __name__ == '__main__':
  sys.exit(main())
