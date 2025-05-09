This directory contains xperf related scripts and binaries from a range of
different blog posts. I will try to mention where they come from in order to
make it easy to get more information.

https://randomascii.wordpress.com/2014/02/04/process-tree-from-an-xperf-trace/
- XperfProcessParentage.py
- XperfProcessParentage.wpaProfile

https://randomascii.wordpress.com/2011/08/18/xperf-basics-recording-a-trace/
- etwrecord.bat and many other batch files are related to this article and
various other articles.

https://randomascii.wordpress.com/2014/11/04/slow-symbol-loading-in-microsofts-profiler-take-two/
- StripChromeSymbols.py

https://randomascii.wordpress.com/2013/03/26/summarizing-xperf-cpu-usage-with-flame-graphs/
- xperf_to_collapsedstacks.py

https://randomascii.wordpress.com/2015/04/14/uiforetw-windows-performance-made-easier/
- UIforETW.exe

For a general overview of using UIforETW and ETW in general see
https://randomascii.wordpress.com/2015/09/24/etw-central/

All of these scripts assume that you have a recent copy of the Windows
Performance Toolkit, or WPT installed. Some of these scripts rely on Microsoft
DLLs or executables that come with the Debuggers package, which also comes with
the Windows SDK. The best way to get all of these is to download and unzip
etwpackage*.zip from https://github.com/randomascii/UIforETW/releases and run
bin\UIforETW.exeas as that will install WPT.
- pdbcopy.exe is used by StripChromeSymbols.py which is used by etwrecord.bat
if you are using the Chromium symbol server.
- x86 versions of dbghelp.dll and symsrv.dll are used by RetrieveSymbols.exe
which is used by StripChromeSymbols.py which is used by etwrecord.bat if you
are using the Chromium symbol server.
- x64 versions of dbghelp.dll and symsrv.dll can be copied to the WPT install
directory if they are found in an effort to alleviate the slow or failed
symbol processing issues described at:
https://randomascii.wordpress.com/2012/10/04/xperf-symbol-loading-pitfalls/
