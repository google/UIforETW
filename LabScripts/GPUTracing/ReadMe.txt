The set of files in this directory serve as an example of how to measure GPU
usage with ETW in a lab scenario, such as for regression testing. Both batch
files must be run from an administrator command prompt. It is assumed that WPT
is in the path, either from being installed or from being copied to the test
machine.

start_tracing.bat starts tracing with the minimal flags needed to record
process information and GPU usage.

stop_tracing.bat stops tracing, merges the kernel and user traces, and then runs
wpaexporter twice: once to get a per-process summary, and once to get details.

The information that is printed is controlled by the .wpaProfile files, and the
generated .csv files contain headers that explain the columns, so there's not
much else to say, except to reflect on how cool it is that this level of detail
about what your GPU is doing is trivially* available.

The analysis of the traces could be done on a different machine.

These batch files should work on Windows 7 but have actually only been tested on
Windows 10. If somebody tests on other operating systems then they should update
this comment for easy PR points.
 
* For meanings of trivial that assume years of training in the dark arts.
