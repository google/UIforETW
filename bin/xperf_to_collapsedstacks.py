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
For more information see http://randomascii.wordpress.com/2013/03/26/summarizing-xperf-cpu-usage-with-flame-graphs/

This script converts sampling profiler call stacks from an xperf trace file
to collapsed call stack files of the type used as input to flamegraph.pl.
The initial xperf conversion is done using wpaexporter
	"wpaexporter.EXE trace.etl -profile ExportCPUUsageSampled.wpaProfile -symbols"
or:
	"wpaexporter.EXE trace.etl -range 1.0s 2.0s -profile ExportCPUUsageSampled.wpaProfile -symbols"
1.0s and 2.0s represent a time range - time values (without the 's' suffix) can be optionally
passed to this script.
See https://randomascii.wordpress.com/2013/11/04/exporting-arbitrary-data-from-xperf-etl-files/ for
details on wpaexporter.

The output from wpaexporter goes to CPU_Usage_(Sampled)_Randomascii_Export.csv. The first line is
column labels and the rest is the fields from the profile - process, threadID, and '/' separated
stack.
"""
import sys
import os
import time
import subprocess

# How many threads to create collapsed stacks for.
numToShow = 1

scriptPath = os.path.abspath(sys.argv[0])
scriptDir = os.path.split(scriptPath)[0]
flameGraphPath = os.path.join(scriptDir, "flamegraph.pl")
if not os.path.exists(flameGraphPath):
	print "Couldn't find \"%s\". Download it from https://github.com/brendangregg/FlameGraph/blob/master/flamegraph.pl" % flameGraphPath
	sys.exit(0)

profilePath = os.path.join(scriptDir, "ExportCPUUsageSampled.wpaProfile")
if not os.path.exists(profilePath):
	print "Couldn't find \"%s\". This should be part of the UIforETW repo and releases" % profilePath
	sys.exit(0)

#if not os.environ.has_key("_NT_SYMBOL_PATH"):
#	print "_NT_SYMBOL_PATH is not set. Exiting."
#	sys.exit(0)

if os.environ.has_key("ProgramFiles(x86)"):
	progFilesx86 = os.environ["ProgramFiles(x86)"]
else:
	progFilesx86 = os.environ["ProgramFiles"]
wpaExporterPath = os.path.join(progFilesx86, r"Windows Kits\10\Windows Performance Toolkit\wpaexporter.EXE")
if not os.path.exists(wpaExporterPath):
	print "Couldn't find \"%s\". Make sure WPT 10 is installed." % wpaExporterPath
	sys.exit(0)

if len(sys.argv) < 2:
	print "Usage: %s trace.etl begin end" % sys.argv[0]
	print "Begin and end specify the time range to be processed, in seconds."
	sys.exit(0)

etlFilename = sys.argv[1]
if len(sys.argv) >= 4:
	begin = float(sys.argv[2])
	end = float(sys.argv[3])
	wpaCommand = r'"%s" "%s" -range %ss %ss -profile "%s" -symbols' % (wpaExporterPath, etlFilename, begin, end, profilePath)
else:
	wpaCommand = r'"%s" "%s" -profile "%s" -symbols' % (wpaExporterPath, etlFilename, profilePath)

print "> %s" % wpaCommand
start = time.clock()
print subprocess.check_output(wpaCommand, stderr=subprocess.STDOUT)
print "Elapsed time for wpaexporter: %1.3f s" % (time.clock() - start)


# This dictionary of dictionaries accumulates sample data. The first key is
# the process name with the thread ID appended to it, like this:
#     devenv.exe_8872_6148
# The second key is the semi-colon separated call stack.
# The final data field is the count of how many times that exact call stack
# has been hit.
samples = {}

# Process all of the lines in the output of wpaexporter, skipping the first line
# which is just the column names.
csvName = "CPU_Usage_(Sampled)_Randomascii_Export.csv"
for line in open(csvName).readlines()[1:]:
	line = line.strip()
	firstCommaPos = line.find(",")
	process = line[:firstCommaPos]
	secondCommaPos = line.find(",", firstCommaPos + 1)
	threadID = line[firstCommaPos + 1 : secondCommaPos]
	stackSummary = line[secondCommaPos + 1:]
	if stackSummary == "n/a":
		continue
	# Since we are using semicolon separators we can't have semicolons
	# in the function names.
	stackSummary = stackSummary.replace(";", ":")
	# Spaces seem like a bad idea also.
	stackSummary = stackSummary.replace(" ", "_")
	# Having single-quote characters in the call stacks gives flamegraph.pl heartburn.
	# Replace them with back ticks.
	stackSummary = stackSummary.replace("'", "`")
	# Double-quote characters also cause problems. Remove them.
	stackSummary = stackSummary.replace('"', "")
	# Remove <PDB_not_found> labels
	stackSummary = stackSummary.replace("<PDB_not_found>", "Unknown")
	# Convert the wpaexporter stack separators to flamegraph stack separators
	stackSummary = stackSummary.replace("/", ";")
	#stackSummary = "A;B;C"

	processAndThread = "%s_%s" % (process.replace(" ", "_"), threadID)
	processAndThread = processAndThread.replace("(", "")
	processAndThread = processAndThread.replace(")", "")
	# Add the record to the nested dictionary, and increment the count
	# if it already exists.
	if not processAndThread in samples:
		samples[processAndThread] = {}
	if not stackSummary in samples[processAndThread]:
		samples[processAndThread][stackSummary] = 0
	# Accumulate counts for this call stack.
	samples[processAndThread][stackSummary] += 1

# Now find the threads with the most samples - we'll dump them.
sortedThreads = []
totalSamples = 0
for processAndThread in samples.keys():
	numSamples = 0
	for stack in samples[processAndThread]:
		numSamples += samples[processAndThread][stack]
	sortedThreads.append((numSamples, processAndThread))
	totalSamples += numSamples

sortedThreads.sort() # Put the threads in order by number of samples
sortedThreads.reverse() # Put the thread with the most samples first

print "Found %d samples from %d threads." % (totalSamples, len(samples))

tempDir = os.environ["temp"]
count = 0
for numSamples, processAndThread in sortedThreads[:numToShow]:
	threadSamples = samples[processAndThread]
	outputName = os.path.join(tempDir, "collapsed_stacks_%d.txt" % count)
	count += 1
	print "Writing %d samples to temporary file %s" % (numSamples, outputName)
	sortedStacks = []
	for stack in threadSamples:
		sortedStacks.append("%s %d\n" % (stack, threadSamples[stack]))
	sortedStacks.sort()
	# Some versions of perl (the version that ships with Chromium's depot_tools
	# for one) can't handle reading files with CRLF line endings, so write the
	# file as binary to avoid line-ending translation.
	out = open(outputName, "wb")
	for stack in sortedStacks:
		out.write(stack)
	# Force the file closed so that the results will be available when we the
	# perl script is run.
	out.close()

	destPath = os.path.join(tempDir, "%s.svg" % processAndThread)
	title = "CPU Usage flame graph of %s" % processAndThread
	perlCommand = 'perl "%s" --title="%s" "%s"' % (flameGraphPath, title, outputName)
	print "> %s" % perlCommand
	svgOutput = subprocess.check_output(perlCommand)
	if len(svgOutput) > 100:
		open(destPath, "wt").write(svgOutput)
		os.popen(destPath)
		print 'Results are in "%s" - they should be auto-opened in the default SVG viewer.' % destPath
	else:
		print "Result size is %d bytes - is perl in your path?" % len(svgOutput)
