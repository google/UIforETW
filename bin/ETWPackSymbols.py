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

"""
This script copies all of the .symcache files referenced by the specified
ETW trace to a specified directory so that they can easily be shared with
other people. The package export option in WPA (File-> Export Package) makes
this functionality mostly obsolete (Export Package puts the .symcache files in
a .wpapk file along with the trace and the current profile) but this script is
retained because it does give additional flexibility and serves as an example.
"""

import os
import re
import shutil
import sys
import csv
import subprocess

# This regular expression takes apart the CodeView Record block.
pdb_re = re.compile(r'\[RSDS\] PdbSig: {(.*-.*-.*-.*-.*)}; Age: (.*); Pdb: (.*)')

def ParseRow(row):
  """Take a CSV row record from an xperf -i -a symcache command and parse it.
  The cvRecord is broken up into its constituent guid, age, and PDBPath
  parts and the integer components are turned into integers (stripping 0x
  headers in the process). The "-" characters in the guid are removed.
  If the record contains the file header that labels the columns then None
  is returned.
  """
  TimeDateStamp, ImageSize, OrigFileName, cvRecord = row
  if TimeDateStamp == "TimeDateStamp":
    # Ignore the column labels
    return None
  TimeDateStamp = int(TimeDateStamp, 0)
  ImageSize = int(ImageSize, 0)
  # Assume that this re match will always succeed.
  result = pdb_re.match(cvRecord)
  guid, age, PDBPath = result.groups()
  guid = guid.replace("-", "")
  age = int(age) # Note that the age is in decimal here
  return TimeDateStamp, ImageSize, OrigFileName, guid, age, PDBPath

def main():

  if len(sys.argv) < 3:
    print("Syntax: PackETWSymbols ETWFilename.etl destdirname [-verbose]")
    print("This script looks for symbols needed to decode the specified trace, and")
    print("copies them to the specified directory. This allows moving traces to")
    print("other machines for analysis and sharing.")
    sys.exit(0)

  # Our usage of subprocess seems to require Python 2.7+
  if sys.version_info.major == 2 and sys.version_info.minor < 7:
    print("Your python version is too old - 2.7 or higher required.")
    print("Python version is %s" % sys.version)
    sys.exit(0)

  ETLName = sys.argv[1]
  DestDirName = sys.argv[2]
  if not os.path.exists(DestDirName):
    os.mkdir(DestDirName)

  verbose = False
  if len(sys.argv) > 3 and sys.argv[3].lower() == "-verbose":
    verbose = True

  print("Extracting symbols from ETL file '%s'." % ETLName)
  print("Note that you will probably need to update the 'interesting' calculation for your purposes.")

  # -tle = tolerate lost events
  # -tti = tolerate time ivnersions
  # -a symcache = show image and symbol identification (see xperf -help processing)
  #  Options to the symcache option (see xperf -help symcache)
  #  -quiet = don't issue warnings
  #  -build = build the symcache, including downloading symbols
  #  -imageid = show module size/data/name information
  #  -dbgid = show PDB guid/age/name information
  command = "xperf.exe -i \"%s\" -tle -tti -a symcache -quiet -imageid -dbgid" % ETLName
  # The -symbols option can be added in front of -a if symbol loading from
  # symbol servers is desired.

  # Typical output lines (including the heading) look like this:
  #TimeDateStamp,  ImageSize, OrigFileName, CodeView Record
  #   0x4da89d03, 0x00bcb000, "client.dll", "[RSDS] PdbSig: {7b2a9028-87cd-448d-8500-1a18cdcf6166}; Age: 753; Pdb: u:\buildbot\dota_staging_win32\build\src\game\client\Release_dota\client.pdb"

  print("Executing command '%s'" % command)
  lines = str(subprocess.check_output(command)).splitlines()

  matchCount = 0
  matchExists = 0
  interestingModuleCount = 0

  symCachePathBase = os.getenv("_NT_SYMCACHE_PATH");
  if symCachePathBase == None or len(symCachePathBase) == 0:
    # Set SymCache to the C drive if not specified.
    symCachePathBase = "c:\\symcache\\"
    print("_NT_SYMCACHE_PATH not set. Looking for symcache in %s" % symCachePathBase)
  if symCachePathBase[-1] != '\\':
    symCachePathBase += '\\'

  for row in csv.reader(lines, delimiter = ",", quotechar = '"', skipinitialspace=True):
    results = ParseRow(row)
    if not results:
      continue
    TimeDateStamp, ImageSize, OrigFileName, guid, age, PDBPath = results
    matchCount += 1

    # Find out which PDBs are 'interesting'. There is no obvious heuristic
    # for this, so for now all symbols whose PDB path name contains a colon
    # are counted, which filters out many Microsoft symbols. The ideal filter
    # would return all locally built symbols - all that cannot be found on
    # symbol servers - and this gets us partway there.
    interesting = PDBPath.count(":") > 0 or PDBPath.count("chrome") > 0
    if interesting:
      interestingModuleCount += 1
      # WPT has three different .symcache file patterns. None are documented but
      # all occur in the symcache directory.
      symCachePathv1 = "%s-%08x%08xv1.symcache" % (OrigFileName, TimeDateStamp, ImageSize)
      symCachePathv2 = "%s-%s%xv2.symcache" % (OrigFileName, guid, age)
      pdb_file = os.path.split(PDBPath)[1]
      symCachePathv3 = r"%s\%s%s\%s-v3.1.0.symcache" % (pdb_file, guid, age, pdb_file)
      symCachePathv1 = os.path.join(symCachePathBase, symCachePathv1)
      symCachePathv2 = os.path.join(symCachePathBase, symCachePathv2)
      symCachePathv3 = os.path.join(symCachePathBase, symCachePathv3)
      foundPath = None
      if os.path.isfile(symCachePathv1):
        foundPath = symCachePathv1
      elif os.path.isfile(symCachePathv2):
        foundPath = symCachePathv2
      elif os.path.isfile(symCachePathv3):
        foundPath = symCachePathv3

      if foundPath:
        matchExists += 1
        print("Copying %s" % foundPath)
        dest = foundPath[len(symCachePathBase):]
        dest_dir, dest_file = os.path.split(dest)
        if dest_dir:
          try:
            os.makedirs(os.path.join(DestDirName, dest_dir))
          except:
            pass # Continue on exceptions, directly probably already exists.
        shutil.copyfile(foundPath, os.path.join(DestDirName, dest_dir, dest_file))
      else:
        if verbose:
          print("Symbols for '%s' are not in %s or %s" % (OrigFileName, symCachePathv1, symCachePathv2))
    else:
      #This is normally too verbose
      if verbose:
        print("Skipping %s" % PDBPath)

  if matchCount == interestingModuleCount:
    print("%d symbol files found in the trace and %d of those exist in symcache." % (matchCount, matchExists))
  else:
    print("%d symbol files found in the trace, %d appear to be interesting, and %d of those exist in symcache." % (matchCount, interestingModuleCount, matchExists))
  if matchExists > 0:
    print("Symbol files found were copied to %s" % DestDirName)


if __name__ == "__main__":
  main()
