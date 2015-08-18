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

  # Find the space-terminated word after 'type='
  pidRe = re.compile(r".*\(([\d ]*)\),.*")
  processTypeRe = re.compile(r".*.exe\" --type=([^ ]*) .*")

  tracename = sys.argv[1]
  #-tle = tolerate lost events
  #-tti = tolerate time ivnersions
  #-a process = show process, thread, image information (see xperf -help processing)
  #-withcmdline = show command line in process reports (see xperf -help process)
  command = 'xperf -i "%s" -tle -tti -a process -withcmdline' % tracename
  # Group all of the chrome.exe processes by exePath, then by type.
  pidsByPath = {}
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
        match = processTypeRe.match(line)
        type = "browser"
        if match:
          type = match.groups()[0]
        pid = int(pidRe.match(line).groups()[0])
        pidsByType = pidsByPath.get(exePath, {})
        pidList = list(pidsByType.get(type, []))
        pidList.append(pid)
        pidsByType[type] = pidList
        pidsByPath[exePath] = pidsByType

  # Sometimes Chrome is launched with a relative path and then it launches
  # child processes with an absolute path. This leads to two 'different'
  # browser instances being detected. As an example one can easily end up with
  # one at r".\out\Release\chrome.exe" or out\Release\chrome.exe and the other
  # at r"D:\src\chromium2\src\out\Release\chrome.exe". This loop attempts to
  # detect this and fix it up. It's not perfect, but it's better than nothing.
  for exePath in pidsByPath.keys():
    if len(exePath) > len(r".\chrome.exe") and exePath[1] != ':':
      # Strip off the leading period
      if exePath.startswith(".\\"):
        subPath = exePath[1:]
      else:
        subPath = exePath
      for otherPath in pidsByPath.keys():
        if otherPath.endswith(subPath):
          # If the end paths match then move the contents of pidsByPath[exePath]
          # over to pidsByPath[otherPath]
          for key in pidsByPath[exePath]:
            pidsByPath[otherPath][key] = pidsByPath[exePath][key]
          pidsByPath[exePath] = {}
          break

  print("Chrome PIDs by process type:\r")
  for exePath in pidsByPath.keys():
    # Any paths with no entries in them should be ignored.
    if len(pidsByPath[exePath]) == 0:
      continue
    if len(pidsByPath.keys()) > 1:
      print("%s\r" % exePath)
    pidsByType = pidsByPath[exePath]
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
