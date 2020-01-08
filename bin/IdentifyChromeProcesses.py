# Copyright 2015 Google Inc. All Rights Reserved.
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

def _IdentifyChromeProcesses(tracename, show_cpu_usage, return_pid_map):
  if not os.path.exists(tracename):
    print("Trace file '%s' does not exist." % tracename)
    print("Usage: %s tracename [-cpuusage]" % sys.argv[0])
    sys.exit(0)

  script_dir = os.path.dirname(sys.argv[0])
  if len(script_dir) == 0:
    script_dir = "."

  cpu_usage_by_pid = {}
  context_switches_by_pid = {}
  if show_cpu_usage:
    csv_filename = os.path.join(script_dir, "CPU_Usage_(Precise)_Randomascii_CPU_Usage_by_Process.csv")
    profile_filename = os.path.join(script_dir, "CPUUsageByProcess.wpaProfile")
    try:
      # Try to delete any old results files but continue if this fails.
      os.remove(csv_filename)
    except:
      pass
    # -tle and -tti are undocumented for wpaexporter but they do work. They tell wpaexporter to ignore
    # lost events and time inversions, just like with xperf.
    command = 'wpaexporter "%s" -outputfolder "%s" -tle -tti -profile "%s"' % (tracename, script_dir, profile_filename)
    output = str(subprocess.check_output(command, stderr=subprocess.STDOUT))
    # Typical output in the .csv file looks like this:
    # New Process,Count,CPU Usage (in view) (ms)
    # Idle (0),7237,"26,420.482528"
    # We can't just split on commas because the CPU Usage often has embedded commas so
    # we need to use an actual csv reader.
    if os.path.exists(csv_filename):
      lines = open(csv_filename, "r").readlines()
      process_and_pid_re = re.compile(r"(.*) \(([\d ]*)\)")
      for row_parts in csv.reader(lines[1:], delimiter = ",", quotechar = '"', skipinitialspace=True):
        process, context_switches, cpu_usage = row_parts
        match = process_and_pid_re.match(process)
        if match:
          process_name, pid = match.groups()
        else:
          process_name, pid = "Unknown", "-1"
        # We could record this for all processes, but this script is all about
        # Chrome so I don't.
        if process_name == "chrome.exe":
          pid = int(pid)
          cpu_usage_by_pid[pid] = float(cpu_usage.replace(",", ""))
          context_switches_by_pid[pid] = int(context_switches)
        elif process_name in ['dwm.exe', 'audiodg.exe', 'System', 'MsMpEng.exe',
                              'software_reporter_tool.exe']:
          # Print information about other relevant processes:
          print("%11s - %6d context switches, %8.2f ms CPU\r" % (process_name,
                int(context_switches), float(cpu_usage.replace(",", ""))))
      print("\r")
    else:
      print("Expected output file not found.\r")
      print("Expected to find: %s\r" % csv_filename)
      print("Should have been produced by: %s\r" % command)

  # Typical output of -a process -withcmdline looks like:
  #        MIN,   24656403, Process, 0XA1141C60,       chrome.exe ( 748),      10760,          1, 0x11e8c260, "C:\...\chrome.exe" --type=renderer ...
  # Find the PID and ParentPID
  pidsRe = re.compile(r".*\(([\d ]*)\), *(\d*),.*")
  # Find the space-terminated word after 'type='. This used to require that it
  # be the first command-line option, but that is likely to not always be true.
  # Mark the first .* as lazy/ungreedy/reluctant so that if there are multiple
  # --type options (such as with the V8 Proxy Resolver utility process) the
  # first one will win.
  processTypeRe = re.compile(r".*? --type=([^ ]*) .*")

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
  try:
    output = subprocess.check_output(command, stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError:
    # Try again. If it succeeds then there were lost events or a time inversion.
    #-tle = tolerate lost events
    #-tti = tolerate time inversions
    command = 'xperf -i "%s" -tle -tti -a process -withcmdline' % tracename
    output = subprocess.check_output(command, stderr=subprocess.STDOUT)
    print('Trace had a time inversion or (most likely) lost events. Results may be anomalous.\r')
    print('\r')
  for line in output.splitlines():
    # Python 3 needs the line translated from bytes to str.
    line = line.decode()
    # Split the commandline from the .csv data and then extract the exePath.
    # It may or may not be quoted, and may or not have the .exe suffix.
    parts = line.split(", ")
    if len(parts) > 8:
      processName = parts[4]
      commandLine = parts[8]
      if commandLine[0] == '"':
        exePath = commandLine[1:commandLine.find('"', 1)]
      else:
        exePath = commandLine.split(" ")[0]
      # The exepath may omit the ".exe" suffix so we need to look at processName
      # instead.
      if processName.count("chrome.exe") > 0:
        pids = pidsRe.match(line)
        pid = int(pids.groups()[0])
        parentPid = int(pids.groups()[1])
        lineByPid[pid] = line
        match = processTypeRe.match(commandLine)
        if match:
          process_type = match.groups()[0]
          if commandLine.count(" --extension-process ") > 0:
            process_type = "extension"
          if process_type == "crashpad-handler":
            process_type = "crashpad" # Shorten the tag for better formatting
          browserPid = parentPid
        else:
          process_type = "browser"
          browserPid = pid
          pathByBrowserPid[browserPid] = exePath
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
      if pathByBrowserPid.has_key(parentPid):
        browserPid = parentPid
        # Retrieve the list of processes associated with this
        # browser (parent) pid.
        pidsByType = pidsByParent[browserPid]
        process_type = "gpu???"
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
    print("Chrome PIDs by process type:\r")
  else:
    print("No Chrome processes found.\r")
  # Make sure the browsers are printed in a predictable order, sorted by Pid
  browserPids = list(pidsByParent.keys())
  browserPids.sort()
  for browserPid in browserPids:
    # The crashpad fixes above should avoid this situation, but I'm leaving the
    # check to maintain robustness.
    exePath = pathByBrowserPid.get(browserPid, "Unknown parent")
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
    if show_cpu_usage:
      print("%s (%d) - %d context switches, %8.2f ms CPU, %d processes\r" % (
            exePath, browserPid, total_context_switches, total_cpu_usage,
            total_processes))
    else:
      print("%s (%d) - %d processes\r" % (exePath, browserPid, total_processes))
    # Note the importance of printing the '\r' so that the
    # output will be compatible with Windows edit controls.
    for process_type in keys:
      print("    %-11s : " % process_type, end="")
      context_switches = 0
      cpu_usage = 0.0
      if show_cpu_usage:
        for pid in pidsByType[process_type]:
          if pid in cpu_usage_by_pid:
            context_switches += context_switches_by_pid[pid]
            cpu_usage += cpu_usage_by_pid[pid]
        print("total - %6d context switches, %8.2f ms CPU" % (context_switches, cpu_usage), end="")
      list_by_type = pidsByType[process_type]
      # Make sure the PIDs are printed in a consistent order.
      list_by_type.sort()
      for pid in list_by_type:
        if show_cpu_usage:
          print("\r\n        ", end="")
          if pid in cpu_usage_by_pid: # Skip per-process details if there's only one.:
            print("%5d - %6d context switches, %8.2f ms CPU" % (pid, context_switches_by_pid[pid], cpu_usage_by_pid[pid]), end="")
          else:
            print("%5d" % pid, end="")
        else:
          print("%d " % pid, end="")
      print("\r")
    print("\r")

def GetPIDToTypeMap(trace_name):
  return _IdentifyChromeProcesses(trace_name, False, True)

def main():
  parser = argparse.ArgumentParser(description="Identify and categorize chrome processes in an ETW trace.")
  parser.add_argument("trace", type=str, nargs=1, help="ETW trace to be processed")
  parser.add_argument("-c", "--cpuusage", help="Summarize CPU usage and context switches per process", action="store_true")
  args = parser.parse_args()

  show_cpu_usage = args.cpuusage
  tracename = args.trace[0]

  _IdentifyChromeProcesses(tracename, show_cpu_usage, False)

if __name__ == "__main__":
  main()
