@rem Copyright 2019 Google Inc. All Rights Reserved.
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem     http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@rem This is a convenience wrapper for recording heap snapshots for
@rem investigating heap memory leaks, as discussed here:
@rem https://docs.microsoft.com/en-us/windows-hardware/test/wpt/record-heap-snapshot
@rem You run this script and pass it the pid of the process of interest.
@rem You can start recording heap data anytime you want, but right at
@rem process startup is one obvious time. wpr will also let you specify the
@rem process of interest by name but that works poorly with Chrome
@rem and its dozens of processes and it is not supported by this script.
@rem When the script runs it tells the specified process to start recording
@rem call stacks for all outstanding memory allocations. When you hit
@rem enter a snapshot of this data is recorded into an ETW trace and then
@rem heap snapshots are disabled.

@rem Set the etwtracedir environment variable if it
@rem isn't set already.
@if not "%etwtracedir%" == "" goto TraceDirSet
@set etwtracedir=%homedrive%%homepath%\documents\etwtraces
:TraceDirSet

@rem Make sure %etwtracedir% exists
@if exist "%etwtracedir%" goto TraceDirExists
@mkdir "%etwtracedir%"
:TraceDirExists

@rem Generate a file name based on the current date and time and put it in
@rem etwtracedir. This is compatible with UIforETW which looks for traces there.
@rem Note: this probably fails in some locales. Sorry.
@for /F "tokens=2-4 delims=/- " %%A in ('date/T') do @set datevar=%%C-%%A-%%B
@for /F "tokens=1-3 delims=:-. " %%A in ('echo %time%') do @set timevar=%%A-%%B-%%C&set hour=%%A
@rem Make sure that morning hours such as 9:00 are handled as 09 rather than 9
@if %hour% LSS 10 set timevar=0%timevar%
@set FileName=%etwtracedir%\%datevar%_%timevar% %username% heapsnapshot %1.etl
@echo Trace will be saved to %FileName%

@rem Tell the process to start recording stacks on all subsequent outstanding
@rem allocations.
wpr -snapshotconfig heap -pid %1 enable
@echo Run your scenario and hit any key when you want a snapshot saved.
@echo You can stay in this state for as long as you want - days if desired -
@echo but performance of the target process may be worse during this time.
@pause
@rem Don't start tracing until we are ready to do the snapshot.
@rem This avoids filling the trace with GB of process-create
@rem events that we don't care about.
wpr -start heapsnapshot -filemode
wpr -singlesnapshot heap %1
wpr -stop "%temp%\UIforETW_heap_snapshot.etl"
wpr -snapshotconfig heap -pid %1 disable
xperf -merge "%temp%\UIforETW_heap_snapshot.etl" "%FileName%"
del "%temp%\UIforETW_heap_snapshot.etl"
@echo Trace data is in %FileName% -- load it with wpa or xperfview or gpuview.
@dir "%FileName%" | find /i ".etl"
