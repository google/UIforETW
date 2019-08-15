/*
Copyright 2019 Google Inc. All Rights Reserved.

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

// This script processes an ETW trace and generates a summary of the CPU
// activity within it.
// See these blog posts for details of the Trace Processor package used to
// drive this:
// https://blogs.windows.com/windowsdeveloper/2019/05/09/announcing-traceprocessor-preview-0-1-0/
// https://blogs.windows.com/windowsdeveloper/2019/08/07/traceprocessor-0-2-0/#L2W90BVvLzJ8XwEY.97 
// This uses the Microsoft.Windows.EventTracing.Processing.All package from NuGet

using Microsoft.Windows.EventTracing;
using Microsoft.Windows.EventTracing.Processes;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

class CPUSummary
{
    struct CPUUsageDetails
    {
        public long ns; // Nanoseconds of execution time.
        public long contextSwitches;
    }

    // Scan through a trace and print a summary of the system activity, with
    // svchost.exe and powershell process purposes annotated, chrome and
    // some other processes grouped, and with sampled data summarized by the
    // module it hits in and which modules are on the stacks.
    static void ProcessTrace(ITraceProcessor trace, string[] moduleList)
    {
        // metadata is retrieved directly instead of being retrieved after
        // trace.Process().
        var metadata = trace.UseMetadata();

        // Needed for precise CPU usage calculations.
        var pendingSchedulingData = trace.UseCpuSchedulingData();
        // Needed for processor count and memory size.
        var pendingSystemMetadata = trace.UseSystemMetadata();
        // Needed for PID to service mapping.
        var pendingServices = trace.UseServices();
        // Needed for seeing what modules samples hit in.
        var pendingSamplingData = trace.UseCpuSamplingData();

        trace.Process();

        // Convert from pending to actual data.
        var schedulingData = pendingSchedulingData.Result;
        var systemMetadata = pendingSystemMetadata.Result;
        var services = pendingServices.Result;
        var samplingData = pendingSamplingData.Result;

        // Print some high-level data about the trace and system.
        var traceDuration = metadata.AnalyzerDisplayedDuration;
        Console.WriteLine("Trace length is {0:0.00} seconds, system has {1} logical processors, {2}.",
            traceDuration.TotalSeconds, systemMetadata.ProcessorCount, systemMetadata.UsableMemorySize);
        Console.WriteLine();

        // Map from an IProcess to CPUUsageDetails.
        var execTimes = new Dictionary<IProcess, CPUUsageDetails>();
        // Scan through the context-switch data to build up how much time
        // was spent by each process.
        foreach (var slice in schedulingData.CpuTimeSlices)
        {
            // Yep, this happens.
            if (slice.Process == null)
                continue;
            execTimes.TryGetValue(slice.Process, out CPUUsageDetails last);
            last.ns += slice.Duration.Nanoseconds;
            last.contextSwitches += 1;
            execTimes[slice.Process] = last;
        }

        // Report on CPU usage for theses processes by name instead of by
        // process. That is, add same-named processes together.
        string[] sumNames = { "chrome.exe", "WmiPrvSE.exe", };
        var sumsNs = new long[sumNames.Length];

        // Scan through the per-process CPU consumption data
        var report = new List<Tuple<double, string>>();
        foreach (var times in execTimes)
        {
            var process = times.Key;
            CPUUsageDetails details = times.Value;

            // See if we want to sum this process name.
            int i = Array.IndexOf(sumNames, process.ImageName);
            if (i >= 0)
            {
                sumsNs[i] += times.Value.ns;
                continue;
            }

            // Find the script name or service name that is associated with this
            // process so that our report associates the CPU time with a script
            // or service.
            string context = "";
            var commandLine = process.CommandLine;
            if (process.ImageName == "svchost.exe")
            {
                // Look for this pid in the snapshots. This should handle it if
                // multiple services exist in one process but this has not been
                // tested.
                foreach (var service in services.Snapshots)
                {
                    if (service.ProcessId == process.Id)
                    {
                        if (context.Length > 0)
                            context += ' ';
                        context += service.Name;
                    }
                }
            }
            else if (process.ImageName == "powershell.exe")
            {
                // Look for .ps1 and then search backwards for a space or slash character.
                var scriptEnd = commandLine.IndexOf(".ps1");
                if (scriptEnd > 0)
                {
                    // Look for a slash or space character to mark the beginning of
                    // the file name of the script.
                    var slash = commandLine.LastIndexOf('\\', scriptEnd);
                    var space = commandLine.LastIndexOf(' ', scriptEnd);
                    var start = Math.Max(slash, space) + 1;
                    context = commandLine.Substring(start, scriptEnd + 4 - start);
                }

                // If no .ps1 filename was found then try something else.
                if (context.Length == 0)
                {
                    // Grab the last parameter. This really needs to handle quotes to
                    // be useful.
                    var parts = commandLine.Split(' ');
                    context = parts[parts.Length - 1];
                }
            }
            else if (process.ImageName == "ruby.exe")
            {
                // Grab the start of the command-line and hope that helps.
                context = commandLine.Substring(0, Math.Min(20, commandLine.Length));
            }

            double timeSeconds = details.ns / 1e9;
            report.Add(new Tuple<double, string>(timeSeconds, string.Format("{0,20} ({1,5}) - {2}", process.ImageName, process.Id, context)));
        }

        // Add in the processes that we sum by name instead of by processes.
        for (int i = 0; i < sumNames.Length; ++i)
        {
            double timeSeconds = sumsNs[i] / 1e9;
            report.Add(new Tuple<double, string>(timeSeconds, string.Format("{0,20}", sumNames[i])));
        }

        // Sort the data by CPU time consumed and print the busiest processes:
        report.Sort();
        report.Reverse();
        foreach (var r in report)
        {
            // Arbitrary threshold so that we ignore boring data.
            if (r.Item1 > 2.0)
            {
                double percentage = r.Item1 / (double)traceDuration.TotalSeconds;
                Console.WriteLine("{0,9:P} of a core, {1,8:0.00} s CPU, {2}", percentage, r.Item1, r.Item2);
            }
        }

        // Record two dictionaries that map from modules to sample counts.
        // One is for samples that hit in that module, the other is for samples
        // that hit when an "interesting" module is on the stack.
        // Track by module name rather than IImage because some DLLs (ntdll.dll)
        // show up as many different IImage objects, which makes for a confusing
        // report.
        var samplesByImage = new Dictionary<string, ulong>();
        var stackSamplesByImage = new Dictionary<string, ulong>();
        foreach (var sample in samplingData.Samples)
        {
            {
                // Attribute samples to the module they hit in. This gives a
                // sense of where CPU time is being spent across all processes
                // on the system. In some cases this can give hints - perhaps a
                // lower bound - on the cost of modules which inject themselves
                // into all processes.
                if (sample.Image != null)
                {
                    samplesByImage.TryGetValue(sample.Image.FileName, out ulong count);
                    ++count;
                    samplesByImage[sample.Image.FileName] = count;
                }
            }

            if (moduleList != null && sample.Stack != null)
            {
                // Attributes samples to interesting modules that are on the stack.
                // In some cases an injected module may ask the OS or other modules
                // to do work on its behalf. If antivirus DLLs are on the
                // stack but not currently executing then the executing code *may*
                // be doing work on their behalf, or not. Thus, gives gives a
                // rough upper bound on the cost of these systems. Note that only the
                // first module hit is counted.
                foreach (var frame in sample.Stack.Frames)
                {
                    if (frame.Image != null && frame.Image.FileName != null)
                    {
                        string imageName = frame.Image.FileName;
                        if (moduleList.Contains(imageName))
                        {
                            stackSamplesByImage.TryGetValue(imageName, out ulong count);
                            ++count;
                            stackSamplesByImage[imageName] = count;
                            break;
                        }
                    }
                }
            }
        }

        Console.WriteLine();
        int totalSamples = samplingData.Samples.Count;
        Console.WriteLine("Exclusive samples by module (out of {0:#,#} samples total):", totalSamples);
        PrintSampleData(new List<KeyValuePair<string, ulong>>(samplesByImage),
            totalSamples, "{0,9:#,#} samples {1,6:P} in {2}");

        if (moduleList != null)
        {
            Console.WriteLine("");
            Console.WriteLine("Inclusive (stacks containing them) samples (out of {0:#,#} samples total):", totalSamples);
            PrintSampleData(new List<KeyValuePair<string, ulong>>(stackSamplesByImage),
                totalSamples, "{0,9:#,#} samples {1,6:P} with {2} on the call stack");
        }
    }

    static void PrintSampleData(List<KeyValuePair<string, ulong>> samples, int total, string formatString)
    {
        // Sort the results by sample count and print the top entries.
        samples.Sort((x, y) => y.Value.CompareTo(x.Value));
        for (int i = 0; i < 10 && i < samples.Count; ++i)
        {
            var count = samples[i].Value;
            double portion = count / (double)total;
            Console.WriteLine(formatString, count, portion, samples[i].Key);
        }
    }

    static void Main(string[] args)
    {
        string traceName = null;
        string[] moduleList = null;
        for (int i = 0; i < args.Length; /**/)
        {
            if (args[i] == "-modules")
            {
                ++i;
                if (i >= args.Length)
                {
                    Console.Error.WriteLine("Missing module list after -modules.");
                    return;
                }
                moduleList = args[i++].Split(';');
            }
            else
            {
                if (traceName != null)
                {
                    Console.Error.WriteLine("Unexpected argument '{0}'", args[i]);
                    return;
                }
                traceName = args[i++];
            }
        }

        if (traceName == null)
        {
            Console.Error.WriteLine("usage: gWindowsETLSummary.exe trace.etl [-modules module1.dll;module2.dll");
            Console.Error.WriteLine("error: too few arguments");
            Console.Error.WriteLine("The (case sensitive) -modules arguments are used to get inclusive CPU sampling data.");
            return;
        }

        if (!File.Exists(traceName))
        {
            // Print a more friendly error message for this case.
            Console.Error.WriteLine("File '{0}' does not exist.", traceName);
            return;
        }

        Console.WriteLine("Processing {0}...", traceName);
        var settings = new TraceProcessorSettings
        {
            // Don't print a setup message on first run.
            SuppressFirstTimeSetupMessage = true
        };
        try
        {
            using (ITraceProcessor trace = TraceProcessor.Create(traceName, settings))
                ProcessTrace(trace, moduleList);
        }
        catch (TraceLostEventsException e)
        {
            // Note that wpaexporter doesn't seem to have a way to handle this,
            // which is one advantage of TraceProcessing. Note that traces with
            // lost events are "corrupt" in some sense so the results will be
            // unpredictable.
            Console.WriteLine(e.Message);
            Console.WriteLine("Trying again with AllowLostEvents specified. Results may be less reliable.");
            Console.WriteLine();

            settings.AllowLostEvents = true;
            using (ITraceProcessor trace = TraceProcessor.Create(traceName, settings))
                ProcessTrace(trace, moduleList);
        }
    }
}
