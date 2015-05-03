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

import os
import re
import shutil
import sys

def main():

  if len(sys.argv) < 3:
    print("Syntax: PackETWSymbols ETWFilename.etl destdirname [-verbose]")
    print("This script looks for symbols needed to decode the specified trace, and")
    print("copies them to the specified directory. This allows moving traces to")
    print("other machines for analysis and sharing.")
    sys.exit(0)

  ETLName = sys.argv[1]
  DestDirName = sys.argv[2]
  if not os.path.exists(DestDirName):
    os.mkdir(DestDirName)

  verbose = False
  if len(sys.argv) > 3 and sys.argv[3].lower() == "-verbose":
    verbose = True

  print("Extracting symbols from ETL file '%s'." % ETLName)

  # This command is slow but thorough -- it tries to build the symbol cache.
  #command = "xperf.exe -i \"%s\" -tle -symbols -a symcache -quiet -build -imageid -dbgid" % ETLName
  # This command is faster. It relies on symbols being loaded already for the modules of interest.
  command = "xperf.exe -i \"%s\" -tle -a symcache -quiet -imageid -dbgid" % ETLName

  print("Executing command '%s'" % command)
  lines = os.popen(command).readlines()

  if len(lines) < 30:
    print("Error:")
    for line in lines:
      print(line, end='')
    sys.exit(0)

  # Typical output lines (including one heading) look like this:
  #TimeDateStamp,  ImageSize, OrigFileName, CodeView Record
  #   0x4da89d03, 0x00bcb000, "client.dll", "[RSDS] PdbSig: {7b2a9028-87cd-448d-8500-1a18cdcf6166}; Age: 753; Pdb: u:\buildbot\dota_staging_win32\build\src\game\client\Release_dota\client.pdb"

  scan = re.compile(r'   0x(.*), 0x(.*), "(.*)", "\[RSDS\].*; Pdb: (.*)"')

  matchCount = 0
  matchExists = 0
  ourModuleCount = 0

  # Get the users build directory
  vgame = os.getenv("vgame")
  if vgame == None:
    print("Environment variable 'vgame' not found!")
    sys.exit(-1)
  vgame = vgame[:-5].lower()

  prefixes = ["u:\\", "e:\\build_slave", vgame]

  print("Looking for symbols built to:")
  for prefix in prefixes:
    print("    %s" % prefix)

  # Default to looking for the SymCache on the C drive
  prefix = "c"
  # Look for a drive letter in the ETL Name and use that if present
  if len(ETLName) > 1 and ETLName[1] == ':':
    prefix = ETLName[0]
  else:
    # If there's no drive letter in the ETL name then look for one
    # in the current working directory.
    curwd = os.getcwd()
    if len(curwd) > 1 and curwd[1] == ':':
      prefix = curwd[0]

  symCachePathBase = os.getenv("_NT_SYMCACHE_PATH");
  if symCachePathBase == None or len(symCachePathBase) == 0:
    symCachePathBase = "%s:\\symcache\\" % prefix
  elif symCachePathBase[-1] != '\\':
    symCachePathBase += '\\'

  for line in lines:
    result = scan.match(line)
    if result is not None:
      #print result.groups()
      matchCount += 1
      TimeDateStamp = result.groups()[0]
      ImageSize = result.groups()[1]
      OrigFileName = result.groups()[2]
      PDBPath = result.groups()[3].lower()

      # Find out which PDBs are 'interesting'. There is no obvious heuristic
      # for this, but having a list of prefixes seems like a good start.
      ours = False
      for prefix in prefixes:
        if PDBPath.startswith(prefix):
          ours = True
      if ours:
        ourModuleCount += 1
        ours = True
        symFilePath = OrigFileName + "-" + TimeDateStamp + ImageSize + "v1.symcache"
        symCachePath = symCachePathBase + symFilePath
        if os.path.isfile(symCachePath):
          matchExists += 1
          print("Copying %s" % symCachePath)
          shutil.copyfile(symCachePath, DestDirName + "\\" + symFilePath)
        else:
          print("Symbols for '%s' are not in %s" % (OrigFileName, symCachePathBase))
      else:
        #This is normally too verbose
        if verbose:
          print("Skipping %s" % PDBPath)

  print("%d symbol files found in the trace, %d appear to be ours, and %d of those exist in symcache." % (matchCount, ourModuleCount, matchExists))


if __name__ == "__main__":
  main()
