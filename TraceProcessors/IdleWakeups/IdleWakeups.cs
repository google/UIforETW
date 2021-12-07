/*
Copyright 2021 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

// Detect idle wakeups in Chrome in an ETW using TraceProcessing
// Explanations of the techniques can be found here:
// https://randomascii.wordpress.com/2020/01/05/bulk-etw-trace-analysis-in-c/

// See this blog post for details of the Trace Processor package used to
// drive this:
// https://blogs.windows.com/windowsdeveloper/2019/05/09/announcing-traceprocessor-preview-0-1-0/
// Note that this URL has changed once already, so caveat blog lector

using Microsoft.Windows.EventTracing;
using Microsoft.Windows.EventTracing.Symbols;

namespace IdleWakeups
{
    // A summary of a particular idle wakeup call stack including its count and all frames in the
    // stack.
    struct StackDetails
    {
        public long Count;
        public IReadOnlyList<StackFrame> Stack;
    }

    class SnapshotSummary
    {
        public SnapshotSummary(Dictionary<string, StackDetails> idleWakeupsByStackId)
        {
            idleWakeupsByStackId_ = idleWakeupsByStackId;
        }

        public Dictionary<string, StackDetails> idleWakeupsByStackId_;
        public Dictionary<Int32, long> previousCStates_ = null;
        public long chromeSwitches_ = 0;
        public long chromeIdleSwitches_ = 0;
    }

    class IdleWakeupsAnalysis
    {
        // Process a trace and create a summary.
        private static SnapshotSummary GetIdleWakeupsSummary(ITraceProcessor trace)
        {
            var pendingCpuSchedulingData = trace.UseCpuSchedulingData();
            var pendingSymbols = trace.UseSymbols();

            trace.Process();

            var cpuSchedData = pendingCpuSchedulingData.Result;
            var symbols = pendingSymbols.Result;
            symbols.LoadSymbolsForConsoleAsync(SymCachePath.Automatic).GetAwaiter().GetResult();

            // Key: string which identifies a callstack.
            // Value: counts the frequency of a given key and also stores a list of all stack
            // frames for the callstack.
            var idleWakeupsStack = new Dictionary<string, StackDetails>();

            long chromeSwitches = 0;
            long chromeIdleSwitches = 0;

            var previousCStates = new Dictionary<Int32, long>();

            // Scan all context switches and build up the dictionary based on all detected idle
            // wakeups where chrome.exe wakes up the Idle thread.
            foreach (var contextSwitch in cpuSchedData.ContextSwitches)
            {
                var switchInImageName = contextSwitch.SwitchIn.Process.ImageName;
                var switchOutImageName = contextSwitch.SwitchOut.Process.ImageName;
                var callStackIn = contextSwitch.SwitchIn.Stack;
                if (switchInImageName == "chrome.exe")
                {
                    chromeSwitches++;
                    if (switchOutImageName == "Idle")
                    {
                        // Idle wakeup is detected.
                        chromeIdleSwitches++;
                        var prevCState = contextSwitch.PreviousCState;
                        if (prevCState.HasValue)
                        {
                            previousCStates.TryGetValue(prevCState.Value, out long count);
                            count += 1;
                            previousCStates[prevCState.Value] = count;
                        }
                        if (callStackIn == null)
                        {
                            continue;
                        }
                        try
                        {
                            // Get a string that allows us to identify the callstack and use it as
                            // key in a dictionary which stores count and the complete stack as
                            // value.
                            var analyzerStringId = callStackIn.GetAnalyzerString();
                            // List of frames in the stack.
                            var stackFrames = callStackIn.Frames;
                            // Update the dictionary.
                            idleWakeupsStack.TryGetValue(analyzerStringId, out StackDetails value);
                            value.Count += 1;
                            value.Stack = callStackIn.Frames;
                            idleWakeupsStack[analyzerStringId] = value;
                        }
                        catch (Exception ex)
                        {
                            Console.WriteLine(ex.ToString());
                        }
                    }
                }
            }

            var result = new SnapshotSummary(idleWakeupsStack);
            result.previousCStates_ = previousCStates;
            result.chromeIdleSwitches_ = chromeIdleSwitches;
            result.chromeSwitches_ = chromeSwitches;

            return result;
        }
        static void Main(string[] args)
        {
            string traceName = "";
            bool showSummary = false;
            foreach (string arg in args)
            {
                if (arg == "-s" || arg == "--summary")
                    showSummary = true;
                else if (traceName.Length == 0)
                    traceName = arg;
                else
                {
                    Console.Error.WriteLine("error: unrecognized arguments: {0}", arg);
                    return;
                }
            }

            if (traceName.Length == 0)
            {
                Console.Error.WriteLine("usage: IdleWakeups.exe [-s,--summary] <filename>");
                Console.Error.WriteLine("error: too few arguments");
                return;
            }

            if (!File.Exists(traceName))
            {
                Console.Error.WriteLine("File '{0}' does not exist.", traceName);
                return;
            }
            using (ITraceProcessor trace = TraceProcessor.Create(traceName))
            {
                Console.WriteLine("Processing trace '{0}'", Path.GetFileName(traceName));

                // Scan all context switches and store callstacks where Chrome causes an idle
                // wakeup. Also store the frequency of each unique callstack.
                var iwakeups = GetIdleWakeupsSummary(trace);

                // Print out a summary of all callstacks that causes idle wakeups.
                // Example (can be later be used as input to separate Go-script which converts the
                // list into something that can be read by pprof):
                //
                // iwakeup:
                //         ntoskrnl.exe!SwapContext
                //         ntoskrnl.exe!KiSwapContext
                //         ...
                //         ntdll.dll!RtlUserThreadStart
                //         1070
                // iwakeup:
                //         ntoskrnl.exe!SwapContext
                //         ntoskrnl.exe!KiSwapContext
                //         ..
                //         ntdll.dll!RtlUserThreadStart
                //         1011
                const int maxPrinted = 40;
                var sortedStackEntries =
                    new List<KeyValuePair<string, StackDetails>>(iwakeups.idleWakeupsByStackId_);
                sortedStackEntries.Sort((x, y) => y.Value.Count.CompareTo(x.Value.Count));
                foreach (KeyValuePair<string, StackDetails> kvp in sortedStackEntries.Take(maxPrinted))
                {
                    Console.WriteLine("iwakeup:");
                    foreach (var entry in kvp.Value.Stack)
                    {
                        try
                        {
                            var stackFrame = entry.GetAnalyzerString();
                            Console.WriteLine("        {0}", stackFrame);
                        }
                        catch (Exception ex)
                        {
                            // Console.WriteLine(ex.ToString());
                        }
                    }
                    Console.WriteLine("        {0}", kvp.Value.Count);
                }

                // Only append a summary if it was explicitly specified using the '-s' argument.
                if (!showSummary)
                {
                    return;
                }

                // Append a short summary of the relationship between the total amount of detected
                // context switches caused by Chrome and those that caused an idle wakeup.
                Console.WriteLine("{0} idlewakeups out of {1} context switches ({2:P}).",
                    iwakeups.chromeIdleSwitches_, iwakeups.chromeSwitches_,
                    iwakeups.chromeIdleSwitches_ / (double)iwakeups.chromeSwitches_);

                // Append summary of C-state residencies (@ Idle -> chrome.exe).
                var list = iwakeups.previousCStates_.Keys.ToList();
                list.Sort();
                Console.WriteLine("Previous C-State (Idle -> chrome.exe):");
                foreach (var key in list)
                {
                    Console.WriteLine("  C{0}: {1,7} ({2,6:P})",
                        key, iwakeups.previousCStates_[key],
                        iwakeups.previousCStates_[key] / (double)iwakeups.chromeIdleSwitches_);
                }
            }
        }
    }
}
