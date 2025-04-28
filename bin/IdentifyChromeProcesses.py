﻿# Copyright 2015 Google Inc. All Rights Reserved.
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
This script analyzes the specified trace to identify Chrome's processes by path
and by type. It optionally prints information about the number of context
switches by process and type, as well as CPU usage.
"""

from __future__ import print_function

import argparse
import csv
import os
import re
import subprocess
import sys

def _IdentifyChromeProcesses(tracename, show_cpu_usage, tabbed_output, return_pid_map):
  if not os.path.exists(tracename):
    print('Trace file "%s" does not exist.' % tracename)
    sys.exit(0)

  script_dir = os.path.dirname(sys.argv[0])
  if len(script_dir) == 0:
    script_dir = '.'

  cpu_usage_by_pid = {}
  context_switches_by_pid = {}
  if show_cpu_usage:
    csv_filename = os.path.join(script_dir, 'CPU_Usage_(Precise)_Randomascii_CPU_Usage_by_Process.csv')
    profile_filename = os.path.join(script_dir, 'CPUUsageByProcess.wpaProfile')
    try:
      # Try to delete any old results files but continue if this fails.
      os.remove(csv_filename)
    except:
      pass
    # -tle and -tti are undocumented for wpaexporter but they do work. They tell wpaexporter to ignore
    # lost events and time inversions, just like with xperf.
    command = 'wpaexporter "%s" -outputfolder "%s" -tle -tti -profile "%s"' % (tracename, script_dir, profile_filename)
    # If there is no CPU usage data then this will return -2147008507.
    try:
      output = str(subprocess.check_output(command, stderr=subprocess.STDOUT))
    except subprocess.CalledProcessError as e:
      if e.returncode == -2147008507:
        print('No CPU Usage (Precise) data found, no report generated.')
        return
      raise(e)
    # Typical output in the .csv file looks like this:
    # New Process,Count,CPU Usage (in view) (ms)
    # Idle (0),7237,"26,420.482528"
    # We can't just split on commas because the CPU Usage often has embedded commas so
    # we need to use an actual csv reader.
    if os.path.exists(csv_filename):
      lines = open(csv_filename, 'r').readlines()
      process_and_pid_re = re.compile(r'(.*) \(([\d ]*)\)')
      for row_parts in csv.reader(lines[1:], delimiter = ',', quotechar = '"', skipinitialspace=True):
        process, context_switches, cpu_usage = row_parts
        match = process_and_pid_re.match(process)
        if match:
          _, pid = match.groups()
          pid = int(pid)
          cpu_usage_by_pid[pid] = float(cpu_usage.replace(',', ''))
          context_switches_by_pid[pid] = int(context_switches)
    else:
      print('Expected output file not found.')
      print('Expected to find: %s' % csv_filename)
      print('Should have been produced by: %s' % command)

  # Typical output of -a process -withcmdline looks like:
  #        MIN,   24656403, Process, 0XA1141C60,       chrome.exe ( 748),      10760,          1, 0x11e8c260, "C:\...\chrome.exe" --type=renderer ...
  # Find the PID and ParentPID
  pidsRe = re.compile(r'.*\(([\d ]*)\), *(\d*),.*')
  # Find the space-terminated word after 'type='. This used to require that it
  # be the first command-line option, but that is likely to not always be true.
  # Mark the first .* as lazy/ungreedy/reluctant so that if there are multiple
  # --type options (such as with the V8 Proxy Resolver utility process) the
  # first one will win.
  processTypeRe = re.compile(r'.*? --type=([^ ]*) .*')

  # Starting around M84 Chrome's utility processes have a --utility-sub-type
  # parameter which identifies the type of utility process. Typical command
  # lines look something like this:
  # --type=utility --utility-sub-type=audio.mojom.AudioService --field-trial...
  processSubTypeRe = re.compile(r'.*? --utility-sub-type=([^ ]*) .*')

  #-a process = show process, thread, image information (see xperf -help processing)
  #-withcmdline = show command line in process reports (see xperf -help process)
  command = 'xperf -i "%s" -a process -withcmdline' % tracename
  # Group all of the chrome.exe processes by browser Pid, then by type.
  # pathByBrowserPid just maps from the browser Pid to the disk path to chrome.exe
  pathByBrowserPid = {}
  # pidsByParent is a dictionary that is indexed by the browser Pid. It contains
  # a dictionary that is indexed by process type with each entry's payload
  # being a list of Pids (for example, a list of renderer processes).
  pidsByParent = {}
  # Dictionary of Pids and their lines of data
  lineByPid = {}
  # Dictionary of Pids and their types.
  types_by_pid = {}
  # Dictionary of Pids and their sub-types (currently utility-processes only).
  sub_types_by_pid = {}
  try:
    output = subprocess.check_output(command, stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError:
    # Try again. If it succeeds then there were lost events or a time inversion.
    #-tle = tolerate lost events
    #-tti = tolerate time inversions
    command = 'xperf -i "%s" -tle -tti -a process -withcmdline' % tracename
    output = subprocess.check_output(command, stderr=subprocess.STDOUT)
    print('Trace had a time inversion or (most likely) lost events. Results may be anomalous.')
    print()
  
  # Extra processes to print information about, when cpu_usage is requested
  extra_processes = []

  for line in output.splitlines():
    # Python 3 needs the line translated from bytes to str.
    line = line.decode(encoding='utf-8', errors='ignore')
    # Split the commandline from the .csv data and then extract the exePath.
    # It may or may not be quoted, and may or not have the .exe suffix.
    parts = line.split(', ')
    if len(parts) > 8:
      pids = pidsRe.match(line)
      if not pids:
        continue
      pid = int(pids.groups()[0])
      parentPid = int(pids.groups()[1])
      processName = parts[4]
      commandLine = parts[8]
      # Deal with quoted and unquoted command lines.
      if commandLine[0] == '"':
        exePath = commandLine[1:commandLine.find('"', 1)]
      else:
        exePath = commandLine.split(' ')[0]
      # The exepath may omit the ".exe" suffix so we need to look at processName
      # instead. Split out the process name from the PID (this is imperfect but
      # good enough for the processes we care about).
      processName = processName.strip().split(' ')[0]
      if show_cpu_usage:
        # Look for the FrameServer service used in video conferencing. Full
        # command-line seems to be -k Camera -s FrameServer but I don't know how
        # stable/consistent that is. Also report on OS processes that Chrome
        # frequently triggers:
        if processName == 'svchost.exe' and commandLine.count('-s FrameServer') > 0:
          processName = 'svchost (FrameServer)'
        if processName in ['dwm.exe', 'audiodg.exe', 'System', 'MsMpEng.exe',
                                'software_reporter_tool.exe', 'svchost (FrameServer)']:
          # Use get() because if the process has not run during this trace
          # there will be no entries in the dictionary.
          extra_processes.append((processName, pid, context_switches_by_pid.get(pid, 0), cpu_usage_by_pid.get(pid, 0)))
      if processName == 'chrome.exe':
        lineByPid[pid] = line
        match = processTypeRe.match(commandLine)
        if match:
          process_type = match.groups()[0]
          if commandLine.count(' --extension-process ') > 0:
            # Extension processes have renderer type, but it is helpful to give
            # them their own meta-type.
            process_type = 'extension'
          if process_type == 'crashpad-handler':
            process_type = 'crashpad' # Shorten the tag for better formatting
          browserPid = parentPid
        else:
          process_type = 'browser'
          browserPid = pid
          pathByBrowserPid[browserPid] = exePath
        sub_type_match = processSubTypeRe.match(commandLine)
        if sub_type_match:
          sub_types_by_pid[pid] = sub_type_match.groups()[0].split('.')[-1]
        types_by_pid[pid] = process_type
        # Retrieve or create the list of processes associated with this
        # browser (parent) pid.
        pidsByType = pidsByParent.get(browserPid, {})
        pidList = list(pidsByType.get(process_type, []))
        pidList.append(pid)
        pidsByType[process_type] = pidList
        pidsByParent[browserPid] = pidsByType

  if return_pid_map:
    return types_by_pid

  if extra_processes:
    if tabbed_output:
      print('Process name\tPID\tContext switches\tCPU Usage (ms)')
    # Make sure the extra processes are printed in a consistent order.
    extra_processes.sort(key=lambda process: process[0].lower())
    for process in extra_processes:
      processName, pid, context_switches, cpu_usage = process
      if tabbed_output:
        print('%s\t%d\t%d\t%.2f' % (processName, pid, context_switches, cpu_usage))
      else:
        print('%21s - %6d context switches, %8.2f ms CPU' % (processName,
              context_switches, cpu_usage))
    print()

  # Scan a copy of the list of browser Pids looking for those with parents
  # in the list and no children. These represent child processes whose --type=
  # option was too far along in the command line for ETW's 512-character capture
  # to get. See crbug.com/614502 for how this happened.
  # This should probably be deleted at some point, along with the declaration and
  # initialization of lineByPid.
  for pid in list(pathByBrowserPid.keys())[:]:
    # Checking that there is only one entry (itself) in the list is important
    # to avoid problems caused by Pid reuse that could cause one browser process
    # to appear to be another browser process' parent.
    if len(pidsByParent[pid]) == 1: # The 'browser' appears in its own list
      line = lineByPid[pid]
      pids = pidsRe.match(line)
      pid = int(pids.groups()[0])
      parentPid = int(pids.groups()[1])
      if parentPid in pathByBrowserPid:
        browserPid = parentPid
        # Retrieve the list of processes associated with this
        # browser (parent) pid.
        pidsByType = pidsByParent[browserPid]
        process_type = 'gpu???'
        pidList = list(pidsByType.get(process_type, []))
        pidList.append(pid)
        pidsByType[process_type] = pidList
        pidsByParent[browserPid] = pidsByType
        # Delete the references to the process that we now know isn't a browser
        # process.
        del pathByBrowserPid[pid]
        del pidsByParent[pid]

  # In many cases there are two crash-handler processes and one is the
  # parent of the other. This seems to be related to --monitor-self.
  # This script will initially report the parent crashpad process as being a
  # browser process, leaving the child orphaned. This fixes that up by
  # looking for singleton crashpad browsers and then finding their real
  # crashpad parent. This will then cause two crashpad processes to be
  # listed, which is correct.
  for browserPid in list(pidsByParent.keys()):
    childPids = pidsByParent[browserPid]
    if len(childPids) == 1 and 'crashpad' in childPids:
      # Scan the list of child processes to see if this processes parent can be
      # found, as one of the crashpad processes.
      for innerBrowserPid in list(pidsByParent.keys()):
        innerChildPids = pidsByParent[innerBrowserPid]
        if 'crashpad' in innerChildPids and browserPid in innerChildPids['crashpad']:
          # Append the orphaned crashpad process to the right list, then
          # delete it from the list of browser processes.
          innerChildPids['crashpad'].append(childPids['crashpad'][0])
          del pidsByParent[browserPid]
          # It's important to break out of this loop now that we have
          # deleted an entry, or else we might try to look it up.
          break

  if len(pidsByParent.keys()) > 0:
    if not tabbed_output:
      print('Chrome PIDs by process type:')
  else:
    print('No Chrome processes found.')
  # Make sure the browsers are printed in a predictable order, sorted by Pid
  browserPids = list(pidsByParent.keys())
  browserPids.sort()
  for browserPid in browserPids:
    # The crashpad fixes above should avoid this situation, but I'm leaving the
    # check to maintain robustness.
    exePath = pathByBrowserPid.get(browserPid, 'Unknown parent')
    # Any paths with no entries in them should be ignored.
    pidsByType = pidsByParent[browserPid]
    if len(pidsByType) == 0:
      assert False
      continue
    keys = list(pidsByType.keys())
    keys.sort()
    total_processes = 0
    total_context_switches = 0
    total_cpu_usage = 0.0
    for process_type in keys:
      for pid in pidsByType[process_type]:
        total_processes += 1
        if show_cpu_usage and pid in cpu_usage_by_pid:
          total_context_switches += context_switches_by_pid[pid]
          total_cpu_usage += cpu_usage_by_pid[pid]
    # Summarize all of the processes for this browser process.
    if show_cpu_usage:
      print('%s (%d) - %d context switches, %8.2f ms CPU, %d processes' % (
            exePath, browserPid, total_context_switches, total_cpu_usage,
            total_processes))
    else:
      print('%s (%d) - %d processes' % (exePath, browserPid, total_processes))
    for process_type in keys:
      if not tabbed_output:
        print('    %-11s : ' % process_type, end='')
      context_switches = 0
      cpu_usage = 0.0
      num_processes_of_type = 0
      if show_cpu_usage:
        for pid in pidsByType[process_type]:
          if pid in cpu_usage_by_pid:
            context_switches += context_switches_by_pid[pid]
            cpu_usage += cpu_usage_by_pid[pid]
            num_processes_of_type += 1
        if num_processes_of_type > 1:
          # Summarize by type when relevant.
          if not tabbed_output:
            print('total - %6d context switches, %8.2f ms CPU' % (context_switches, cpu_usage), end='')
      list_by_type = pidsByType[process_type]
      # Make sure the PIDs are printed in a consistent order.
      list_by_type.sort()
      for pid in list_by_type:
        sub_type_text = ''
        if pid in sub_types_by_pid:
          sub_type_text = ' (%s)' % sub_types_by_pid[pid]
        if show_cpu_usage:
          if tabbed_output:
            type = 'utility (%s)' % sub_types_by_pid[pid] if pid in sub_types_by_pid else process_type
            print('%s\t%s\t%d\t%.2f' % (type, pid, context_switches_by_pid.get(pid, 0), cpu_usage_by_pid.get(pid, 0)))
          else:
            print('\n        ', end='')
            if pid in cpu_usage_by_pid:
              # Print CPU usage details if they exist
              print('%5d - %6d context switches, %8.2f ms CPU%s' % (pid, context_switches_by_pid[pid], cpu_usage_by_pid[pid], sub_type_text), end='')
            else:
              print('%5d%s' % (pid, sub_type_text), end='')
        else:
          print('%d%s ' % (pid, sub_type_text), end='')
      if not tabbed_output:
        print()
    print()

def GetPIDToTypeMap(trace_name):
  return _IdentifyChromeProcesses(trace_name, False, False, True)

def main():
  parser = argparse.ArgumentParser(description='Identify and categorize chrome processes in an ETW trace.')
  parser.add_argument('trace', type=str, nargs=1, help='ETW trace to be processed')
  parser.add_argument('-c', '--cpuusage', help='Summarize CPU usage and context switches per process', action='store_true')
  parser.add_argument('-t', '--tabbed', help='Print CPU usage as a tab-separated grid', action='store_true')
  args = parser.parse_args()

  if args.tabbed and not args.cpuusage:
    print('Tabbed output is only supported when cpuusage is displayed.')
    return

  _IdentifyChromeProcesses(args.trace[0], args.cpuusage, args.tabbed, False)

if __name__ == '__main__':
  main()
