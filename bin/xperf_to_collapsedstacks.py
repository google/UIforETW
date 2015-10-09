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

This script converts sampling profiler call stacks from a time-range of an xperf trace
file to collapsed call stack files of the type used as input to flamegraph.pl.
The initial xperf conversion is done with this under-documented command:
	"xperf -i trace.etl -symbols -o tempoutput.txt -a dumper -range start end"
start and end are integers representing microseconds from the start of the trace.
See "xperf -help processing" for more clues about how this works.

The format of the SampledProfile and Stack events looks like this:

	SampledProfile,  TimeStamp,     Process Name ( PID),   ThreadID,           PrgrmCtr, CPU, ThreadStartImage!Function,            Image!Function, Count, SampledProfile type
	Stack,  TimeStamp,   ThreadID, No.,            Address,            Image!Function

Typical data looks like this:

	SampledProfile,      36765,        xperf.exe ( 288),       4848, 0x81c866e4,   1,        xperf.exe!0x003733b4,     ntoskrnl.exe!0x81c866e4,     1, Unbatched
	SampledProfile,      36765,      svchost.exe (1100),       1668, 0x81c3617b,   0,      sechost.dll!ScSvcctrlThreadW,     halmacpi.dll!0x81c3617b,     1, Unbatched
	SampledProfile,      36765,         dota.exe (  56),       3044, 0x551ea026,   3,         dota.exe!WinMainCRTStartup,       client.dll!CInterpolatedVarArrayBase<Vector,0>::NoteChanged,     1, Unbatched
			 Stack,      36765,       1668,   1, 0x81c3617b,     halmacpi.dll!0x81c3617b
			 Stack,      36765,       1668,   2, 0x81ceec6f,     ntoskrnl.exe!0x81ceec6f
			 Stack,      36765,       1668,   3, 0x81d96478,     ntoskrnl.exe!0x81d96478
			 Stack,      36765,       1668,   4, 0x81d965c3,     ntoskrnl.exe!0x81d965c3
			 Stack,      36765,       1668,   5, 0x81dea6e0,     ntoskrnl.exe!0x81dea6e0

Lines that start with SampledProfile followed by a time-stamp specify a sample
in a process. Contiguous blocks of lines that start with Stack specify a call stack.
You have to use the time-stamp and thread ID to figure out what the call stack is
associated with. Many of the call stacks will be associated with context switches
or other events, and the call stacks associated with samples may take a little while
(dozens of call stacks later?) to show up.

Each output line represents a call stack with semi-colon separated entries and
a count at the end. The first entry on the call stack is always the process
name, process ID, and thread ID. Typical output looks like this:
	devenv.exe_(8872)(6148);ntoskrnl.exe!KiSystemServiceCopyEnd;ntoskrnl.exe!_??_::NNGAKEGL::`string`;ntoskrnl.exe!CmpCallCallBacks 4
	devenv.exe_(8872)(6148);ntoskrnl.exe!KiSystemServiceRepeat 1
	devenv.exe_(8872)(6148);rsaenh.dll!TransformMD5 2
"""
import sys
import re
import os

# How many threads to create collapsed stacks for.
numToShow = 1

scriptPath = os.path.abspath(sys.argv[0])
scriptDir = os.path.split(scriptPath)[0]
flameGraphPath = os.path.join(scriptDir, "flamegraph.pl")
if not os.path.exists(flameGraphPath):
	print "Couldn't find %s. Download it from https://github.com/brendangregg/FlameGraph/blob/master/flamegraph.pl" % flameGraphPath

if len(sys.argv) < 2:
	print "Usage: %s trace.etl begin end" % sys.argv[0]
	print "Begin and end specify the time range to be processed, in seconds."
	sys.exit(0)

etlFilename = sys.argv[1]
textFilename = os.path.join(os.environ["temp"], os.path.split(etlFilename)[1])
if len(sys.argv) >= 4:
	begin = int(float(sys.argv[2])*1e6)
	end = int(float(sys.argv[3])*1e6)
	textFilename = textFilename.replace(".etl", "_%d_%d.txt" % (begin, end))
	command = 'xperf -i "%s" -symbols -o "%s" -a dumper -stacktimeshifting -range %d %d' % (etlFilename, textFilename, begin, end)
else:
	textFilename = textFilename.replace(".etl", ".txt")
	command = 'xperf -i "%s" -symbols -o "%s" -a dumper -stacktimeshifting' % (etlFilename, textFilename)

# Optimization for when working on the script -- don't reprocess the xperf
# trace if the parameters haven't changed.
if not os.path.exists(textFilename):
	print command
	for line in os.popen(command).readlines():
		print line
else:
	print "Using cached xperf results in '%s'." % textFilename

# Create regular expressions for parsing the SampledProfile and Stack lines.
# SampledProfile,  TimeStamp,     Process Name ( PID),   ThreadID,           PrgrmCtr, CPU, ThreadStartImage!Function,            Image!Function, Count, SampledProfile type
sampledProfileRe = re.compile( r"SampledProfile, *(\d*), *(.*), *(\d*), 0x.*" )
# Stack,  TimeStamp,   ThreadID, No.,            Address,            Image!Function
stackRe = re.compile( r"Stack, *(\d*), *(\d*), *\d*, 0x([0-9a-f]*), *(.*)" )

# Do we have a call stack in progress to add to?
BuildingStack = False
# This is a dictionary with TimeStamp/ThreadID and process names as payload.
# SampledProfile lines are added to here.
unresolvedSamples = {}
# This dictionary of dictionaries accumulates sample data. The first key is
# the process name with the thread ID appended to it, like this:
#     devenv.exe_(8872)(6148)
# The second key is the semi-colon separated collapsed call stack.
# The final data field is the count of how many times that exact call stack
# has been hit.
samples = {}

# Process all of the lines in the output of the xperf -a dumper command.
# Make sure we end with a blank line so that the last stack always
# gets processed.
for line in open(textFilename).readlines() + ["\n"]:
	line = line.strip()
	if line.startswith("Stack,"):
		# Accumulate contiguous Stack lines as an array of
		# lines of text.
		if not BuildingStack:
			stack = []
			BuildingStack = True
		stack.append(line)
	elif BuildingStack:
		# We're finished building that call stack. Now we need to see if there
		# are any samples we can associate it with.
		BuildingStack = False
		# Do a regex match on the first line. This should succeed
		# every time except the first time -- the first Stack line
		# just lists the column names.
		match = stackRe.match(stack[0])
		if match:
			# Extract the matched fields and convert them to integers as appropriate.
			TimeStamp, ThreadID, Address, ImageAndFunction = match.groups()
			TimeStamp = int(TimeStamp)
			ThreadID = int(ThreadID)
			# Create a key suitable for matching up Stacks to SampledProfile events.
			key = (TimeStamp, ThreadID)
			# Let's see if this stack's signature matches any of our unresolved
			# samples.
			if key in unresolvedSamples:
				# Get the name of the process in which this sample occurred.
				# Delete the entry while we're at it, in order to avoid unbounded growth.
				process = unresolvedSamples.pop(key)
				# Create a fully specified process and thread name under which these
				# samples will be stored.
				#processAndThread = "%s(%d)" % (process.replace(" ", "_"), ThreadID)
				processAndThread = "%s_%d" % (process.replace(" ", "_"), ThreadID)
				processAndThread = processAndThread.replace("(", "")
				processAndThread = processAndThread.replace(")", "")
				# Convert the array of stack lines to a single call stack string.
				stackSummary = []
				for entry in stack:
					match = stackRe.match(entry)
					functionName = match.groups()[3]
					# Since we are using semicolong separators we can't have semicolons
					# in the function names.
					functionName = functionName.replace(";", ":")
					# Spaces seem like a bad idea also.
					functionName = functionName.replace(" ", "_")
					# Having single-quote characters in the call stacks gives flamegraph.pl heartburn.
					# Replace them with back ticks.
					functionName = functionName.replace("'", "`")
					# Double-quote characters also cause problems. Remove them.
					functionName = functionName.replace('"', "")
					stackSummary.append(functionName)
				# Add the process and thread ID as the base
				# so that we can tell what the samples are from.
				stackSummary.append(processAndThread)
				# The stack entries come in order from leaf to root and we need them
				# reversed, so let's do that.
				stackSummary.reverse()
				# Convert to a semi-colon separated string
				stackSummary = ";".join(stackSummary)
				# Add the record to the nested dictionary, and increment the count
				# if it already exists.
				if not processAndThread in samples:
					samples[processAndThread] = {}
				if not stackSummary in samples[processAndThread]:
					samples[processAndThread][stackSummary] = 0
				# Accumulate counts for this call stack.
				samples[processAndThread][stackSummary] += 1

	if line.startswith("SampledProfile,"):
		match = sampledProfileRe.match( line )
		if match:
			TimeStamp, process, ThreadID = match.groups()
			TimeStamp = int(TimeStamp)
			ThreadID = int(ThreadID)
			if process != "Idle (   0)":
				# Add an entry to say that there was a sample with this signature.
				# If we get a stack with the same signature then we assume it is
				# a sampled profile event stack.
				unresolvedSamples[(TimeStamp, ThreadID)] = process

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
	#outputName = "%s_collapse.txt" % processAndThread
	outputName = os.path.join(tempDir, "collapsed_stacks_%d.txt" % count)
	count += 1
	print "Writing %d samples to temporary file %s" % (numSamples, outputName)
	sortedStacks = []
	for stack in threadSamples:
		sortedStacks.append("%s %d\n" % (stack, threadSamples[stack]))
	sortedStacks.sort()
	out = open(outputName, "wt")
	for stack in sortedStacks:
		out.write(stack)


	destPath = os.path.join(tempDir, "%s.svg" % processAndThread)
	title = "CPU Usage flame graph of %s" % processAndThread
	perlCommand = 'perl "%s" --title="%s" "%s" >"%s"' % (flameGraphPath, title, outputName, destPath)
	#print perlCommand
	for line in os.popen(perlCommand).readlines():
		print line
	resultSize = os.path.getsize(destPath)
	if resultSize < 100: # Arbitrary sane minimum
		print "Result size is %d bytes - is perl in your path?" % resultSize
	else:
		os.popen(destPath)
		print "Results are in '%s' - they should be auto-opened in the default SVG viewer." % destPath
