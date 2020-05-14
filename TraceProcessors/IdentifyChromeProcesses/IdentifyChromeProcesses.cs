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

// This script processes an ETW trace and generates a summary of the Chrome
// processes found within it.
// See this blog post for details of the Trace Processor package used to
// drive this:
// https://blogs.windows.com/windowsdeveloper/2019/05/09/announcing-traceprocessor-preview-0-1-0/
// Note that this URL has changed once already, so caveat blog lector
// The algorithms were based on UIforETW\bin\IdentifyChromeProcesses.py and
// the results were compared to that script to ensure correctness.
// This uses the Microsoft.Windows.EventTracing.Processing.All package from NuGet

using Microsoft.Windows.EventTracing;
using Microsoft.Windows.EventTracing.Cpu;
using Microsoft.Windows.EventTracing.Processes;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq; // For Keys.First()
using System.Text.RegularExpressions;

class IdentifyChromeProcesses
{
    struct CPUUsageDetails
    {
        public string imageName;
        public long ns; // Nanoseconds of execution time.
        public long contextSwitches;
    }

    // Get a dictionary of Chrome process paths, indexed by the IProcess object
    // which serves as a unique identifier. Don't return a Dictionary keyed by
    // PID because that will lose records whenever there is PID reuse, which is
    // often.
    static Dictionary<IProcess, string> GetChromePaths(IProcessDataSource processData)
    {
        var chromePaths = new Dictionary<IProcess, string>();

        foreach (var process in processData.Processes)
        {
            if (process.ImageName != "chrome.exe")
                continue;

            // Extract the exePath from the command line. It is also possible to
            // grab it from the process.Images.Path attribute but that sometimes
            // gives weird results, is not clearly better, and makes comparing
            // with the Python script more difficult.
            // The exePath may or may not be quoted, and may or not have the .exe
            // suffix but if we strip quotes or split on spaces it works.
            string commandLine = process.CommandLine ?? "<Unknown>";
            string exePath;
            if (commandLine.StartsWith("\""))
                exePath = commandLine.Substring(1, commandLine.IndexOf('\"', 1) - 1);
            else
                exePath = commandLine.Split(' ')[0];

            chromePaths[process] = exePath;
        }

        return chromePaths;
    }

    // Scan through a trace and print a summary of the Chrome processes, optionally with
    // CPU Usage details for Chrome and other key processes.
    static void ProcessTrace(ITraceProcessor trace, bool showCPUUsage)
    {
        var pendingProcessData = trace.UseProcesses();
        // Only request CPU scheduling data when it is actually needed, to avoid
        // unecessary trace processing costs. Unfortunately this means that the
        // scheduling data sources can't be declared with 'var'.
        IPendingResult<ICpuSchedulingDataSource> pendingSchedulingData = null;
        if (showCPUUsage)
            pendingSchedulingData = trace.UseCpuSchedulingData();

        trace.Process();

        ICpuSchedulingDataSource schedulingData = null;
        if (showCPUUsage)
            schedulingData = pendingSchedulingData.Result;

        // Get a List<ProcessSummary> of all Chrome processes.
        var processSummaries = GetChromePaths(pendingProcessData.Result);

        // Group all of the chrome.exe processes by browser Pid, then by type.
        // pathByBrowserPid just maps from the browser Pid to the disk path to
        // chrome.exe
        var pathByBrowserPid = new Dictionary<int, string>();

        // processesByBrowserPid is a dictionary that is indexed by the browser Pid.
        // It contains a dictionary that is indexed by process type with each
        // entry's payload being a list of Pids (for example, a list of renderer
        // processes).
        var processesByBrowserPid = new Dictionary<int, Dictionary<string, List<int>>>();

        // parentPids is a dictionary that maps from Pids to parent Pids.
        var parentPids = new Dictionary<int, int>();

        // Dictionary of Pids and their types.
        var typesByPid = new Dictionary<int, string>();
        // Dictionary of Pids and their sub-types.
        var subTypesByPid = new Dictionary<int, string>();

        // Find the space-terminated word after 'type='.
        // Mark the first .* as lazy/ungreedy/reluctant so that if there are multiple
        // --type options (such as with the V8 Proxy Resolver utility process) the
        // first one will win. Or, at least, that's what the comments in the Python
        // version of this said.
        var r = new Regex(@".*? --type=(?<type>[^ ]*) .*");
        // Find the utility sub-type, if present.
        var r_sub = new Regex(@".*? --utility-sub-type=(?<subtype>[^ ]*) .*");
        foreach (var entry in processSummaries)
        {
            var process = entry.Key;
            var exePath = entry.Value;
            if (process.ImageName == "chrome.exe")
            {
                int pid = process.Id;
                parentPids[pid] = process.ParentId;

                // Look for the process type on the command-line in the
                // --type= option. If no type is found then assume it is the
                // browser process. I could have looked for chrome.dll, but
                // that may fail in the future. I could have looked for a chrome
                // process whose parent is not chrome, but I didn't. There are
                // many ways to do this.
                string type;
                int browserPid;
                var match = r.Match(process.CommandLine);
                if (match.Success)
                {
                    type = match.Groups["type"].ToString();
                    if (type == "crashpad-handler")
                        type = "crashpad"; // Shorten the tag for better formatting
                    if (type == "renderer" && process.CommandLine.Contains(" --extension-process "))
                    {
                        // Extension processes are renderers with --extension-process on the command line.
                        type = "extension";
                    }
                    browserPid = process.ParentId;
                }
                else
                {
                    type = "browser";
                    browserPid = pid;
                    pathByBrowserPid[browserPid] = exePath;
                }

                typesByPid[pid] = type;

                var match_sub = r_sub.Match(process.CommandLine);
                if (match_sub.Success)
                    subTypesByPid[pid] = match_sub.Groups["subtype"].ToString();

                // Retrieve or create the list of processes associated with this
                // browser (parent) pid.
                // This involves a lot of redundant dictionary lookups, but it is
                // the cleanest way to do it.
                if (!processesByBrowserPid.ContainsKey(browserPid))
                    processesByBrowserPid[browserPid] = new Dictionary<string, List<int>>();
                if (!processesByBrowserPid[browserPid].ContainsKey(type))
                    processesByBrowserPid[browserPid][type] = new List<int>();
                var pidList = processesByBrowserPid[browserPid][type];
                pidList.Add(pid);
            }
        }

        // Clean up the data, because process trees are never simple.
        // Iterate through a copy of the keys so that we can modify the dictionary.
        foreach (var browserPid in new List<int>(processesByBrowserPid.Keys))
        {
            var childPids = processesByBrowserPid[browserPid];
            if (childPids.Count == 1)
            {
                // Linq magic to get the one-and-only key.
                string childType = childPids.Keys.First();
                string destType = null;

                // In many cases there are two crash-handler processes and one is the
                // parent of the other. This seems to be related to --monitor-self.
                // This script will initially report the parent crashpad process as being a
                // browser process, leaving the child orphaned. This fixes that up by
                // looking for singleton crashpad browsers and then finding their real
                // crashpad parent. This will then cause two crashpad processes to be
                // listed, which is correct.
                if (childType == "crashpad")
                {
                    destType = childType;
                }

                // Also look for entries with parents in the list and no children. These
                // represent child processes whose --type= option was too far along in the
                // command line for ETW's 512-character capture to get. See crbug.com/614502
                // for how this happened.
                // Checking that there is only one entry (itself) in the list is important
                // to avoid problems caused by Pid reuse that could cause one browser process
                // to appear to be another browser process' parent.
                else if (childType == "browser")
                {
                    destType = "gpu???";
                }

                // The browserPid *should* always be present, but in a large enough corpus
                // of traces, all bets are off. This assumption failed in a 20-hour heap
                // snapshot trace.
                if (parentPids.ContainsKey(browserPid))
                {
                    // The childPids["browser"] entry needs to be appended to its
                    // parent/grandparent process since that is its browser process.
                    int parentPid = parentPids[browserPid];
                    // Create the destination type if necessary (needed for gpu???,
                    // not needed for crashpad). Handle missing data.
                    if (processesByBrowserPid.ContainsKey(parentPid))
                    {
                        if (!processesByBrowserPid[parentPid].ContainsKey(destType))
                            processesByBrowserPid[parentPid][destType] = new List<int>();
                        processesByBrowserPid[parentPid][destType].Add(childPids[childType][0]);

                        // Remove the fake 'browser' entry so that we don't try to print it.
                        processesByBrowserPid.Remove(browserPid);
                    }
                }
            }
        }

        // Map from PID to CPUUsageDetails.
        var execTimes = new Dictionary<int, CPUUsageDetails>();
        if (showCPUUsage)
        {
            var names = new string[] { "chrome.exe", "dwm.exe", "audiodg.exe", "System", "MsMpEng.exe", "software_reporter_tool.exe" };
            // Scan through the context-switch data to build up how much time
            // was spent by interesting process.
            foreach (var slice in schedulingData.CpuTimeSlices)
            {
                // Yep, this happens.
                if (slice.Process == null)
                    continue;
                // Ignore non-interesting names. Only accumulate for chrome.exe and
                // processes that are known to be related or interesting.
                // An existence check in an array is O(n) but because the array is
                // short this is probably faster than using a dictionary.
                if (!names.Contains(slice.Process.ImageName))
                    continue;
                execTimes.TryGetValue(slice.Process.Id, out CPUUsageDetails last);
                last.imageName = slice.Process.ImageName;
                last.ns += slice.Duration.Nanoseconds;
                last.contextSwitches += 1;
                execTimes[slice.Process.Id] = last;
            }

            foreach (var times in execTimes)
            {
                CPUUsageDetails details = times.Value;
                // Print details about other interesting processes:
                if (details.imageName != "chrome.exe")
                {
                    Console.WriteLine("{0,11} - {1,6} context switches, {2,8:0.00} ms CPU", details.imageName, details.contextSwitches, details.ns / 1e6);
                }
            }
            Console.WriteLine();
        }

        if (processesByBrowserPid.Count > 0)
            Console.WriteLine("Chrome PIDs by process type:");
        else
            Console.WriteLine("No Chrome processes found.");
        var browserPids = new List<int>(processesByBrowserPid.Keys);
        browserPids.Sort();
        foreach (var browserPid in browserPids)
        {
            // |processes| is a Dictionary<type, List<pid>>
            var processes = processesByBrowserPid[browserPid];

            // Total up how many processes there are in this instance.
            var detailsByType = new Dictionary<string, CPUUsageDetails>();

            int totalProcesses = 0;
            var detailsTotal = new CPUUsageDetails();
            foreach (var type in processes)
            {
                totalProcesses += type.Value.Count;
                if (showCPUUsage)
                {
                    var detailsSubTotal = new CPUUsageDetails();
                    foreach (int pid in type.Value)
                    {
                        execTimes.TryGetValue(pid, out CPUUsageDetails details);
                        detailsTotal.ns += details.ns;
                        detailsTotal.contextSwitches += details.contextSwitches;

                        detailsSubTotal.ns += details.ns;
                        detailsSubTotal.contextSwitches += details.contextSwitches;
                    }

                    detailsByType[type.Key] = detailsSubTotal;
                }
            }

            // Print the browser path.
            if (showCPUUsage)
            {
                Console.WriteLine("{0} ({1}) - {2} context switches, {3,8:0.00} ms CPU, {4} processes",
                    pathByBrowserPid[browserPid], browserPid, detailsTotal.contextSwitches,
                    detailsTotal.ns / 1e6, totalProcesses);
            }
            else
            {
                // See earlier note about how the browserPid may be missing.
                string browserPath = "Unknown parent";
                if (pathByBrowserPid.ContainsKey(browserPid))
                    browserPath = pathByBrowserPid[browserPid];
                Console.WriteLine("{0} ({1}) - {2} processes", browserPath, browserPid, totalProcesses);
            }

            // Sort the types alphabetically for consistent printing.
            var types = new List<KeyValuePair<string, List<int>>>(processes);
            types.Sort((x, y) => x.Key.CompareTo(y.Key));

            // Print all of the child processes, grouped by type.
            foreach (var type in types)
            {
                // |type| contains type and List<pid>
                // TODO: change this to ,12 for ppapi-broker
                if (showCPUUsage)
                {
                    CPUUsageDetails detailsSum = detailsByType[type.Key];
                    Console.Write("    {0,-11} : total - {1,6} context switches, {2,8:0.00} ms CPU", type.Key, detailsSum.contextSwitches, detailsSum.ns / 1e6);
                }
                else
                    Console.Write("    {0,-11} : ", type.Key);
                type.Value.Sort();
                foreach (var pid in type.Value)
                {
                    string subTypeText = "";
                    if (subTypesByPid.ContainsKey(pid))
                        subTypeText = " (" + subTypesByPid[pid] + ")";
                    if (showCPUUsage)
                    {
                        Console.Write("\n        ");
                        execTimes.TryGetValue(pid, out CPUUsageDetails details);
                        if (details.contextSwitches > 0)
                            Console.Write("{0,5} - {1,6} context switches, {2,8:0.00} ms CPU{3}", pid, details.contextSwitches, details.ns / 1e6, subTypeText);
                        else
                            Console.Write("{0,5}{1}", pid, subTypeText);
                    }
                    else
                        Console.Write("{0}{1} ", pid, subTypeText);
                }
                Console.WriteLine();
            }
            Console.WriteLine();
        }
    }

    static void Main(string[] args)
    {
        bool showCPUUsage = false;
        string traceName = "";
        foreach (string arg in args)
        {
            if (arg == "-c" || arg == "--cpuusage")
                showCPUUsage = true;
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
            Console.Error.WriteLine("usage: IdentifyChromeProcesses.exe [-c] trace");
            Console.Error.WriteLine("error: too few arguments");
            return;
        }

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
        try
        {
            using (ITraceProcessor trace = TraceProcessor.Create(traceName, settings))
                ProcessTrace(trace, showCPUUsage);
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
                ProcessTrace(trace, showCPUUsage);
        }
    }
}
