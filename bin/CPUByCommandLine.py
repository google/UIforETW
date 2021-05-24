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
This script is designed to get CPU-time by process command line. It is used
after running:
wpaexporter -i trace.etl -profile CPUUsageByCommandLine.wpaProfile

Running this wpaexporter command creates two .csv files, one mapping from
process to command line and another mapping from process to CPU usage.
Due to PID reuse the results are not guaranteed but will be mostly accurate.
This was initially written in order to analyze CPU usage by Python scripts in
Chromium's build.
'''

from __future__ import print_function

import re
import os
import sys

command_line_csv = 'Processes_ProcessCommandLines.csv'
cpu_usage_csv = 'CPU_Usage_(Precise)_Randomascii_CPU_Summary_by_Process.csv'

if not os.path.exists(command_line_csv) or not os.path.exists(cpu_usage_csv):
  print('Required .csv files are missing. To generate them run:')
  print('wpaexporter -i trace.etl -profile CPUUsageByCommandLine.wpaProfile')
  sys.exit(0)

commandlines = open(command_line_csv).readlines()
cpuusages = open(cpu_usage_csv).readlines()

commandLinesByProcess = {}
for line in commandlines[1:]:
  # Typical lines look like this:
  # conhost.exe (22628),\??\C:\WINDOWS\system32\conhost.exe 0x4
  process, commandline = re.match(r'(.* \(\d+\)),(.*)\n', line).groups()
  commandLinesByProcess[process] = commandline

cpuusageByProcess = {}
for line in cpuusages[1:]:
  # Typical lines look like this:
  # Idle (0),23871651,"20,726,984.943082"
  # Quotes will be present when the numbers contain embedded commas.
  match = re.match(r'(.*),\d+,"?([\d,.]*)"?\n', line)
  process, cpuusage = match.groups()
  # If a process ID is reused for a process of the same name then some data
  # will be overwritten. Print a warning in that case.
  if process in cpuusageByProcess:
    print('Duplicate process/pid pair: %s' % process, file=sys.stderr)
  cpuusageByProcess[process] = cpuusage

cpuusageAndCommandline = []
for process in commandLinesByProcess.keys():
  # Some processes will not have run at all during the trace. That is fine.
  if process in cpuusageByProcess:
    # wpaexporter puts commas in some large numbers. This fixup code will
    # presumably fail in some locales. Oh well.
    cpuusage = float(cpuusageByProcess[process].replace(',', ''))
    cpuusageAndCommandline += [[cpuusage, commandLinesByProcess[process]]]

# Put the processes with the most CPU usage at the top.
cpuusageAndCommandline.sort()
cpuusageAndCommandline.reverse()

for cpuusage, commandline in cpuusageAndCommandline:
  print('%6.3f s - %s' % (cpuusage / 1000, commandline))
