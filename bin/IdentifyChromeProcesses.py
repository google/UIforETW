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
  # Find the space-terminated word after 'type='
  processTypeRe = re.compile(r".*.exe\" --type=([^ ]*) .*")

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
  for line in os.popen(command).readlines():
    # Split the commandline from the .csv data and then extract the exePath.
    # It may or may not be quoted.
    parts = line.split(", ")
    if len(parts) > 8:
      commandLine = parts[8]
      if commandLine[0] == '"':
        exePath = commandLine[1:commandLine.find('"', 1)]
      else:
        exePath = commandLine.split(" ")[0]
      if exePath.count("chrome.exe") > 0:
        pids = pidsRe.match(line)
        pid = int(pids.groups()[0])
        parentPid = int(pids.groups()[1])
        match = processTypeRe.match(line)
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

  print("Chrome PIDs by process type:\r")
  for browserPid in pidsByParent.keys():
    exePath = pathByBrowserPid[browserPid]
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
      print("%-11s : " % type, end="")
      for pid in pidsByType[type]:
        print("%d " % pid, end="")
      print("\r")
    print("\r")

if __name__ == "__main__":
  main()
