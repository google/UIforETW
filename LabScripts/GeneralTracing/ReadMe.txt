The set of files in this directory serve as an example of how to measure CPU
and GPU usage with ETW in a lab scenario, such as for regression testing. Both
start_tracing.bat and stop_tracing.bat must be run from an administrator command
prompt. It is assumed that WPT is in the path, either from being installed or
from being copied to the test machine.

start_tracing.bat starts tracing with the minimal flags needed to record
process information, CPU usage, context switches, and GPU usage.

stop_tracing.bat stops tracing, merges the kernel and user traces. It then runs
wpaexporter once to find the marks inserted into the trace by "xperf -m" in
order to tightly bound the relevant section of the trace. These bounds are put
into the RANGE environment variable through a Python script and batch file.

Then all of the *.wpaProfile files are processed to created a set of .csv files
that summarize the data in the trace.

The analysis of the traces could be done on a different machine.

These batch files should work on Windows 7 but have actually only been tested on
Windows 10. If somebody tests on other operating systems then they should update
this comment for easy PR points.

Adding additional types of analysis is 'easily' done by making sure that the
appropriate data is being recorded (being sure not to increase the data rate too
much as we wouldn't want to affect the results we are measuring) and then adding
.wpaProfile files to define the data to be exported.

Details on how the export works have previously been written up at:
https://randomascii.wordpress.com/2013/11/04/exporting-arbitrary-data-from-xperf-etl-files/
