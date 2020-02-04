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

// This program summarizes one or more heap snapshots. If multiple heap
// snapshots are given as parameters then it tries to summarizes the
// differences. See this post for discussion of recording heap snapshots:
// https://randomascii.wordpress.com/2019/10/27/heap-snapshots-tracing-all-heap-allocations/

// See these blog posts for details of the Trace Processor package used to
// drive this:
// https://blogs.windows.com/windowsdeveloper/2019/05/09/announcing-traceprocessor-preview-0-1-0/
// https://blogs.windows.com/windowsdeveloper/2019/08/07/traceprocessor-0-2-0/#L2W90BVvLzJ8XwEY.97
// https://randomascii.wordpress.com/2020/01/05/bulk-etw-trace-analysis-in-c/
// This uses the Microsoft.Windows.EventTracing.Processing.All package from NuGet

using Microsoft.Windows.EventTracing;
using Microsoft.Windows.EventTracing.Memory;
using Microsoft.Windows.EventTracing.Symbols;
using System;
using System.Collections.Generic;
using System.IO;

namespace HeapSnapshotCompare
{
    // A summary of a particular allocation call stack including the outstanding
    // bytes allocated, the number of outstanding allocations, and the call
    // stack.
    struct AllocDetails
    {
        public DataSize Size;
        public long Count;
        public IReadOnlyList<IStackFrame> Stack;
    }

    // A summary of a heap snapshot. This includes AllocDetails by Stack Ref #,
    // which stack frames show up in the most allocation stacks, and the total
    // number of bytes and allocations outstanding.
    class SnapshotSummary
    {
        public SnapshotSummary(Dictionary<ulong, AllocDetails> allocsByStackId, int pid)
        {
            allocsByStackId_ = allocsByStackId;
            pid_ = pid;
        }
        // Dictionary of AllocDetails indexed by SnapshotUniqueStackId, aka the Stack Ref#
        // column.
        public Dictionary<ulong, AllocDetails> allocsByStackId_;
        public Dictionary<string, long> hotStackFrames_ = null;
        public int pid_;

        public DataSize totalBytes_;
        public long allocCount_;
    }

    class HeapSnapshotCompare
    {
        // Process a trace and create a summary.
        static SnapshotSummary GetAllocSummary(ITraceProcessor trace)
        {
            var pendingSnapshotData = trace.UseHeapSnapshots();
            var pendingSymbols = trace.UseSymbols();

            trace.Process();

            var snapshotData = pendingSnapshotData.Result;
            var symbols = pendingSymbols.Result;
            symbols.LoadSymbolsAsync(new SymCachePath(@"c:\symcache")).GetAwaiter().GetResult();

            if (snapshotData.Snapshots.Count != 1)
            {
                Console.Error.WriteLine("Trace must contain exactly one heap snapshot - actually contained {0}.",
                    snapshotData.Snapshots.Count);
                return new SnapshotSummary(null, 0);
            }

            // Scan through all of the allocations and collect them by
            // SnapshotUniqueStackId (which corresponds to Stack Ref#),
            // accumulating the bytes allocated, allocation count, and the
            // stack.
            var allocsByStackId = new Dictionary<ulong, AllocDetails>();
            foreach (IHeapAllocation row in snapshotData.Snapshots[0].Allocations)
            {
                allocsByStackId.TryGetValue(row.SnapshotUniqueStackId, out AllocDetails value);
                value.Stack = row.Stack;
                value.Size += row.Size;
                value.Count += 1;
                allocsByStackId[row.SnapshotUniqueStackId] = value;
            }

            // Count how many allocations each stack frame is part of.
            // RtlThreadStart will presumably be near the top, along with
            // RtlpAllocateHeapInternal, but some clues may be found.
            var hotStackFrames = new Dictionary<string, long>();
            foreach (var data in allocsByStackId.Values)
            {
                foreach (var entry in data.Stack)
                {
                    var analyzerString = entry.GetAnalyzerString();
                    hotStackFrames.TryGetValue(analyzerString, out long count);
                    count += data.Count;
                    hotStackFrames[analyzerString] = count;
                }
            }

            var result = new SnapshotSummary(allocsByStackId, snapshotData.Snapshots[0].ProcessId);

            // Create a summary of the alloc counts and byte counts.
            var totalAllocBytes = DataSize.Zero;
            long totalAllocCount = 0;
            foreach (var data in allocsByStackId.Values)
            {
                totalAllocBytes += data.Size;
                totalAllocCount += data.Count;
            }

            result.hotStackFrames_ = hotStackFrames;
            result.totalBytes_ = totalAllocBytes;
            result.allocCount_ = totalAllocCount;

            return result;
        }

        static void Main(string[] args)
        {
            if (args.Length == 0)
            {
                Console.WriteLine("Use this to summarize a heap snapshot or compare multiple heap snapshots");
                Console.WriteLine("from one run of a program.");
                return;
            }

            SnapshotSummary lastAllocs = null;
            string lastTracename = "";
            foreach (var arg in args)
            {
                if (!File.Exists(arg))
                {
                    Console.Error.WriteLine("File '{0}' does not exist.", arg);
                    continue;
                }
                using (ITraceProcessor trace = TraceProcessor.Create(arg))
                {
                    Console.WriteLine("Summarizing '{0}'", Path.GetFileName(arg));
                    var allocs = GetAllocSummary(trace);
                    if (allocs.allocsByStackId_ == null)
                    {
                        Console.WriteLine("Ignoring trace {0}.", arg);
                        continue;
                    }
                    Console.WriteLine("{0,7:F2} MB from {1,9:#,#} allocations on {2,7:#,#} stacks",
                        allocs.totalBytes_.TotalMegabytes, allocs.allocCount_, allocs.allocsByStackId_.Count);

                    const int maxPrinted = 40;

                    Console.WriteLine("Hottest stack frames:");
                    // Display a summary of the first (possibly only) heap snapshot trace.
                    var sortedHotStackEntries = new List<KeyValuePair<string, long>>(allocs.hotStackFrames_);
                    sortedHotStackEntries.Sort((x, y) => y.Value.CompareTo(x.Value));
                    for (int i = 0; i < sortedHotStackEntries.Count && i < maxPrinted; ++i)
                    {
                        var data = sortedHotStackEntries[i];
                        Console.WriteLine("{0,5} allocs cross {1}", data.Value, data.Key);
                    }

                    if (lastAllocs != null)
                    {
                        Console.WriteLine("Comparing old ({0}) to new ({1}) snapshots.", Path.GetFileName(lastTracename), Path.GetFileName(arg));
                        if (allocs.pid_ != lastAllocs.pid_)
                        {
                            Console.WriteLine("WARNING: process IDs are different ({0} and {1}) so stack IDs may not be comparable.", lastAllocs.pid_, allocs.pid_);
                        }

                        var hotStackFramesDelta = new Dictionary<string, long>(allocs.hotStackFrames_);
                        // Subtract the lastAllocs stack frame counts fomr the current stack frame counts.
                        foreach (var entry in lastAllocs.hotStackFrames_)
                        {
                            hotStackFramesDelta.TryGetValue(entry.Key, out long count);
                            count -= entry.Value;
                            hotStackFramesDelta[entry.Key] = count;
                        }

                        Console.WriteLine("Hottest stack frame deltas:");
                        // Print the biggest deltas, positive then negative.
                        var sortedHotStackFramesDelta = new List<KeyValuePair<string, long>>(hotStackFramesDelta);
                        sortedHotStackFramesDelta.Sort((x, y) => y.Value.CompareTo(x.Value));
                        // Print the first half...
                        for (int i = 0; i < sortedHotStackFramesDelta.Count && i < maxPrinted / 2; ++i)
                        {
                            var data = sortedHotStackFramesDelta[i];
                            Console.WriteLine("{0,5} allocs cross {1}", data.Value, data.Key);
                        }
                        Console.WriteLine("...");
                        int start = sortedHotStackFramesDelta.Count - maxPrinted / 2;
                        if (start < 0)
                            start = 0;
                        for (int i = start; i <  sortedHotStackFramesDelta.Count - 1; ++i)
                        {
                            var data = sortedHotStackFramesDelta[i];
                            Console.WriteLine("{0,5} allocs cross {1}", data.Value, data.Key);
                        }

                        ulong newOnlyStacks = 0;
                        ulong oldOnlyStacks = 0;
                        foreach (var tag in allocs.allocsByStackId_.Keys)
                        {
                            if (!lastAllocs.allocsByStackId_.ContainsKey(tag))
                            {
                                newOnlyStacks++;
                            }
                        }
                        foreach (var tag in lastAllocs.allocsByStackId_.Keys)
                        {
                            if (!allocs.allocsByStackId_.ContainsKey(tag))
                            {
                                oldOnlyStacks++;
                            }
                        }
                        Console.WriteLine("  Old snapshot had {0} unique-to-it stacks, new trace had {1} unique-to-it stacks.",
                            oldOnlyStacks, newOnlyStacks);
                    }

                    lastAllocs = allocs;
                    lastTracename = arg;
                }
            }
        }
    }
}
