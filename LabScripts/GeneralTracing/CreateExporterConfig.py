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
This script takes the name of an ETW file. It is expected that this file will
contain marks saying "Starting" and "Stopping" which represent the area of
interest in the file.

This script finds the time of these marks, scans for all .wpaProfile files
in the directory, and creates a wpaExporter config file (a .json file) that
says to export data for the Starting/Stopping time range for each .wpaProfile
file. This is much faster than invoking wpaExporter once for each .wpaProfile
file.

Sample usage:
    call python CreateExporterConfig.py trace.etl >exporterconfig.json
"""

import glob
import os
import subprocess
import sys

def main():
  etl_name = sys.argv[1]

  script_dir = os.path.dirname(sys.argv[0])
  if len(script_dir) == 0:
    script_dir = "."

  # Use the built-in xperf action to export the marks. This is much faster than
  # invoking wpaExporter. These marks are required.
  command = 'xperf -i "%s" -a marks' % etl_name
  lines = str(subprocess.check_output(command)).splitlines()

  # Typical output looks like this:
  #              Mark Type,  TimeStamp, Label
  #                   Mark,     753176, Starting
  #                   Mark,    5418754, Stopping

  assert(len(lines) == 3)
  parts0 = map(lambda a : a.strip(), lines[1].split(','))
  assert(parts0[2] == 'LabScriptsStarting')
  assert(parts0[0] == 'Mark')
  parts1 = map(lambda a : a.strip(), lines[2].split(','))
  assert(parts1[2] == 'LabScriptsStopping')
  assert(parts1[0] == 'Mark')

  # The times are in microseconds.
  start_time = '%ss' % (int(parts0[1]) / 1e6)
  stop_time = '%ss' % (int(parts1[1]) / 1e6)

  # Print out a .json file, as discussed here:
  # https://social.msdn.microsoft.com/Forums/en-US/780b0b63-1fb4-4461-a5f2-9b725aa5cc3d/exporterconfig-file-format
  # Could use the json module but this worked out simplest.

  config_start = """
  {
    "TraceNames": [{
        "Key": "trace1",
        "Value": "%s"
      }
    ],
    "Profiles": ["""

  profile_base = """
      {
        "Name": "%s",
        "Traces": [{
            "Name": "trace1",
            "TimeRange": {
              "Start": "%s",
              "End": "%s"
            }
          }
        ]
      }"""

  config_end = """
    ]
  }
  """

  print config_start % etl_name.replace("\\", "\\\\"),

  profiles = glob.glob(os.path.join(script_dir, "*.wpaprofile"))
  for index, profile in enumerate(profiles):
    print profile_base % (profile.replace("\\", "\\\\"), start_time, stop_time),
    if index != len(profiles)-1:
      print ",",

  print config_end,


if __name__ == '__main__':
  sys.exit(main())
