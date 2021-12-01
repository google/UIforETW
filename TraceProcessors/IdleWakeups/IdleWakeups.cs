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
class IdleWakeups
{
    static void Main(string[] args)
    {
        foreach (string traceName in args)
        {
            Console.WriteLine("Processing trace '{0}'", traceName);
            var settings = new TraceProcessorSettings
            {
                // Don't print a setup message on first run.
                SuppressFirstTimeSetupMessage = true
            };
            using (ITraceProcessor trace = TraceProcessor.Create(traceName, settings))
            {
                // Specify what data we want, process the trace, then get the data.
                var pendingContextSwitchData = trace.UseContextSwitchData();
                trace.Process();
                var csData = pendingContextSwitchData.Result;

                long chromeSwitches = 0;
                long chromeIdleSwitches = 0;
                // Iterate through all context switches in the trace.
                foreach (var contextSwitch in csData.ContextSwitches)
                {
                    var imageName = contextSwitch.SwitchIn.Process.ImageName;
                    var oldImageName = contextSwitch.SwitchOut.Process.ImageName;
                    if (imageName == "chrome.exe")
                    {
                        chromeSwitches++;
                        if (oldImageName == "Idle")
                            chromeIdleSwitches++;
                    }
                }
                Console.WriteLine("{0} idlewakeups out of {1} context switches ({2:P}).",
                    chromeIdleSwitches, chromeSwitches,
                    chromeIdleSwitches / (double)chromeSwitches);
            }
        }
    }
}
