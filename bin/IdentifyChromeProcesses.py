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

from __future__ import print_function

import sys
import os
import re

def main():
  if len(sys.argv) < 2:
    print("Usage: %s tracename" % sys.argv[0])
    sys.exit(0)

  # Typical output of -a process -withcmdline looks like:
  #        MIN,   24656403, Process, 0XA1141C60,       chrome.exe ( 748),      10760,          1, 0x11e8c260, "C:\...\chrome.exe" --type=renderer ...
  # Find the PID and ParentPID
  pidsRe = re.compile(r".*\(([\d ]*)\), *(\d*),.*")
  # Find the space-terminated word after 'type='. This used to require that it
  # be the first command-line option, but that is likely to not always be true.
  processTypeRe = re.compile(r".* --type=([^ ]*) .*")

  tracename = sys.argv[1]
  #-tle = tolerate lost events
  #-tti = tolerate time ivnersions
  #-a process = show process, thread, image information (see xperf -help processing)
  #-withcmdline = show command line in process reports (see xperf -help process)
  command = 'xperf -i "%s" -tle -tti -a process -withcmdline' % tracename
  # Group all of the chrome.exe processes by browser Pid, then by type.
  # pathByBrowserPid just maps from the browser Pid to the disk path to chrome.exe
  pathByBrowserPid = {}
  # pidsByParent is a dictionary that is indexed by the browser Pid. It contains
  # a dictionary that is indexed by process type with each entry's payload
  # being a list of Pids (for example, a list of renderer processes).
  pidsByParent = {}
  # Dictionary of Pids and their lines of data
  lineByPid = {}
  for line in os.popen(command).readlines():
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
          type = match.groups()[0]
          browserPid = parentPid
        else:
          type = "browser"
          browserPid = pid
          pathByBrowserPid[browserPid] = exePath
        # Retrieve or create the list of processes associated with this
        # browser (parent) pid.
        pidsByType = pidsByParent.get(browserPid, {})
        pidList = list(pidsByType.get(type, []))
        pidList.append(pid)
        pidsByType[type] = pidList
        pidsByParent[browserPid] = pidsByType
  # Scan a copy of the list of browser Pids looking for those with parents
  # in the list and no children. These represent child processes whose --type=
  # option was too far along in the command line for ETW's 512-character capture
  # to get. See crbug.com/614502 for how this happened.
  # This should probably be deleted at some point, along with the declaration and
  # initialization of lineByPid.
  for pid in pathByBrowserPid.keys()[:]:
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
        type = "gpu???"
        pidList = list(pidsByType.get(type, []))
        pidList.append(pid)
        pidsByType[type] = pidList
        pidsByParent[browserPid] = pidsByType
        # Delete the references to the process that we now know isn't a browser
        # process.
        del pathByBrowserPid[pid]
        del pidsByParent[pid]

  print("Chrome PIDs by process type:\r")
  for browserPid in pidsByParent.keys():
    # I hit one trace where there was a crash-handler process that was the
    # child of another crash-handler process, which caused this script to
    # fail here. Avoiding the script crash with .get() seems sufficient.
    exePath = pathByBrowserPid.get(browserPid, "Unknown exe path")
    # Any paths with no entries in them should be ignored.
    pidsByType = pidsByParent[browserPid]
    if len(pidsByType) == 0:
      assert False
      continue
    print("%s (%d)\r" % (exePath, browserPid))
    keys = list(pidsByType.keys())
    keys.sort()
    # Note the importance of printing the '\r' so that the
    # output will be compatible with Windows edit controls.
    for type in keys:
      print("    %-11s : " % type, end="")
      for pid in pidsByType[type]:
        print("%d " % pid, end="")
      print("\r")
    print("\r")

if __name__ == "__main__":
  main()
