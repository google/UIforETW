/*
Copyright 2020 Google Inc. All Rights Reserved.

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

// This program summarizes CPU usage and CPU perf counters for the processes
// involved in Chrome video conferencing.

// See these blog posts for details of the Trace Processor package used to
// drive this:
// https://blogs.windows.com/windowsdeveloper/2019/05/09/announcing-traceprocessor-preview-0-1-0/
// https://blogs.windows.com/windowsdeveloper/2019/08/07/traceprocessor-0-2-0/#L2W90BVvLzJ8XwEY.97
// https://randomascii.wordpress.com/2020/01/05/bulk-etw-trace-analysis-in-c/
// This uses the Microsoft.Windows.EventTracing.Processing.All package from NuGet

using Microsoft.Windows.EventTracing;
using Microsoft.Windows.EventTracing.Processes;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace VideoConfCPUCounters
{
    class Counters
    {
        public Counters(string processDescription)
        {
            description = processDescription;
            counters = new Dictionary<string, ulong>();
        }
        public Dictionary<string, ulong> counters;
        public long runTime_ns;
        public long contextSwitches;
        public string description;
    };

    class VideoConfCPUCounters
    {
        static Dictionary<IProcess, Counters> FindInterestingProcesses(IProcessDataSource processData)
        {
            var countersByProcess = new Dictionary<IProcess, Counters>();

            foreach (var process in processData.Processes)
            {
                string description = "";
                // We are interested in all Chrome processes.
                if (process.ImageName == "chrome.exe")
                {
                    // Find the space-terminated word after 'type='.
                    // Mark the first .* as lazy/ungreedy/reluctant so that if
                    // there are multiple --type options (such as with the V8
                    // Proxy Resolver utility process) the first one will win.
                    // Or, at least, that's what the comments in the Python
                    // version of this said.
                    var r = new Regex(@".*? --type=(?<type>[^ ]*) .*");
                    // Find the utility sub-type, if present.
                    var r_sub = new Regex(@".*? --utility-sub-type=(?<subtype>[^ ]*) .*");
                    string type;
                    var match = r.Match(process.CommandLine);
                    if (match.Success)
                    {
                        type = match.Groups["type"].ToString();
                        // Shorten the tag for better formatting
                        if (type == "crashpad-handler")
                            type = "crashpad";
                        if (type == "renderer" &&
                            process.CommandLine.Contains(" --extension-process "))
                        {
                            // Extension processes are renderers with
                            // --extension-process on the command line.
                            type = "extension";
                        }

                        var match_sub = r_sub.Match(process.CommandLine);
                        if (match_sub.Success)
                        {
                            // Split utility-process sub-types on period
                            // boundaries and take the last component. This
                            // changes video_capture.mojom.VideoCaptureService
                            // into just VideoCaptureService.
                            char[] separator = { '.' };
                            var parts = match_sub.Groups["subtype"].ToString()
                                .Split(separator);
                            type = parts[parts.Length - 1];
                        }
                    }
                    else
                    {
                        type = "browser";
                    }

                    description = "chrome - " + type;
                }
                else if (process.ImageName == "svchost.exe")
                {
                    if (process.CommandLine.Contains("-k Camera") ||
                            process.CommandLine.Contains("-s FrameServer"))
                        description = "svchost - FrameServer";
                }
                else if (process.ImageName == "dwm.exe" ||
                        process.ImageName == "audiodg.exe" ||
                        process.ImageName == "System")
                    description = process.ImageName;

                if (description.Length > 0)
                {
                    countersByProcess[process] = new Counters(description);
                }
            }

            return countersByProcess;
        }

        static void Main(string[] args)
        {
            if (args.Length != 1)
            {
                Console.WriteLine("Specify the name of one trace to be summarized.");
                return;
            }

            var traceName = args[0];

            if (!File.Exists(traceName))
            {
                Console.Error.WriteLine("File '{0}' does not exist.", traceName);
                return;
            }

            var settings = new TraceProcessorSettings
            {
                // Don't print a setup message on first run.
                SuppressFirstTimeSetupMessage = true
            };

            using (ITraceProcessor trace = TraceProcessor.Create(traceName, settings))
            {
                // Get process details, including command lines.
                var pendingProcessData = trace.UseProcesses();
                // Get CPU performance counters, on every context switch.
                var pendingCounterData = trace.UseProcessorCounters();

                trace.Process();

                var processData = pendingProcessData.Result;
                var counterData = pendingCounterData.Result;

                var countersByProcess = FindInterestingProcesses(processData);

                // Accumulate data for all of the interesting processes.
                foreach (var entry in counterData.ContextSwitchCounterDeltas)
                {
                    // This sometimes happens - handle it.
                    if (entry.Process == null)
                        continue;

                    Counters last;
                    if (!countersByProcess.TryGetValue(entry.Process, out last))
                        continue;

                    // Accumulate counter values and execution time.
                    foreach (var key in entry.RawCounterDeltas.Keys)
                    {
                        last.counters.TryGetValue(key, out ulong lastCount);
                        lastCount += entry.RawCounterDeltas[key];
                        last.counters[key] = lastCount;
                    }
                    last.runTime_ns += (entry.StopTime - entry.StartTime).Nanoseconds;
                    last.contextSwitches += 1;

                    countersByProcess[entry.Process] = last;
                }

                // Sort the data by CPU time and print it.
                var sortedCounterData = new List<KeyValuePair<IProcess, Counters>>(countersByProcess);
                sortedCounterData.Sort((x, y) => y.Value.runTime_ns.CompareTo(x.Value.runTime_ns));

                bool printHeader = true;
                foreach (var entry in sortedCounterData)
                {
                    if (printHeader)
                    {
                        Console.Write("{0,-29} - CPU time (s) - context switches", "Image name");
                        foreach (var counterName in entry.Value.counters.Keys)
                        {
                            int fieldWidth = Math.Max(13, counterName.Length);
                            Console.Write(", {0}", counterName.PadLeft(fieldWidth));
                        }
                        Console.WriteLine();
                        printHeader = false;
                    }

                    // Arbitrary cutoff for what is "interesting"
                    if (entry.Value.runTime_ns < 100 * 1000 * 1000)
                        continue;

                    Console.Write("{0,-29} -     {1,8:0.00} - {2,16}", entry.Value.description,
                                    entry.Value.runTime_ns / 1e9, entry.Value.contextSwitches);
                    foreach (var counterName in entry.Value.counters.Keys)
                    {
                        int fieldWidth = Math.Max(13, counterName.Length);
                        Console.Write(", {0}",
                            entry.Value.counters[counterName].ToString().PadLeft(fieldWidth));
                    }
                    Console.WriteLine();
                }
            }
        }
    }
}
