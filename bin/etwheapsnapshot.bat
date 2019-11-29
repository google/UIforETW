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

@setlocal
@echo off

@rem This is a convenience wrapper for recording heap snapshots for
@rem investigating heap memory leaks, as discussed here:
@rem https://docs.microsoft.com/en-us/windows-hardware/test/wpt/record-heap-snapshot
@rem You run this script and pass it the pid of the process of interest,
@rem or a path to a process that you want started. Note that if this batch file
@rem starts a process then it will, necessarily, be run as administrator, and
@rem some initial allocations will be missed.
@rem You can start recording heap data anytime you want, but right at
@rem process startup is one obvious time. wpr will also let you specify the
@rem process of interest by name but that works poorly with Chrome
@rem and its dozens of processes and it is not supported by this script.
@rem When the script runs it tells the specified process to start recording
@rem call stacks for all outstanding memory allocations. When you hit
@rem enter a snapshot of this data is recorded into an ETW trace. You then have
@rem the option to record subsequent traces (one snapshot each) or exit and
@rem disable heap snapshots. Note that it is quite possible to put multiple
@rem snapshots in one file, but this script does not support this.

@rem Set the etwtracedir environment variable if it isn't set already.
@if not "%etwtracedir%" == "" goto TraceDirSet
@set etwtracedir=%homedrive%%homepath%\documents\etwtraces
:TraceDirSet

@rem Make sure %etwtracedir% exists
@if exist "%etwtracedir%" goto TraceDirExists
@mkdir "%etwtracedir%"
:TraceDirExists

if not "%1" == "" goto hasarg
@echo Required argument is missing. Specify a PID or a path to an executable
@echo to launch.
@exit /b
:hasarg

@rem See if %1 is a PID or a process path, start a process if needed, put the
@rem pid in %pid%. Note that if we start the process here then the first few
@rem allocations will be missed. Start the process with DelayedCreateProcess.exe
@rem in another command prompt and then (quickly) run this batch file with the
@rem pid if you want to avoid this.
set pid=none
set "var="&for /f "delims=0123456789" %%i in ("%1") do @set var=%%i
if defined var (
    FOR /f "usebackq tokens=*" %%a in (`DelayedCreateProcess.exe "%*" 0`) do set pid=%%a
    ) else (
    set pid=%1
    )

@rem Exit if CreateProcess failed
if %pid%==none exit /b

@echo Starting at %date%, %time%

@rem Enable echo so that users can see the commands that are being run.
@echo on

@rem Tell the process to start recording stacks on all subsequent outstanding
@rem allocations.
wpr -snapshotconfig heap -pid %pid% enable

@echo Run your scenario and hit any non-'q' key when you want a snapshot saved.
@echo You can stay in this state for as long as you want - days if desired -
@echo but performance of the target process may be worse during this time.
:WaitAgain
@set /p quit=Type 'q' and enter if you want to quit, or enter to save a snapshot:
@if "%quit%"=="q" goto leave

@rem Generate a file name based on the current date and time and put it in
@rem etwtracedir. This is compatible with UIforETW which looks for traces there.
@rem Adapted from https://superuser.com/a/1287820
@rem get the date in locale independent way
@rem use findstr to strip blank lines from wmic output
@for /f "usebackq skip=1 tokens=1-3" %%g in (`wmic Path Win32_LocalTime Get Day^,Month^,Year ^| findstr /r /v "^$"`) do @set _day=00%%g&set _month=00%%h&set _year=%%i
@rem pad day and month with leading zeros
@set _month=%_month:~-2%
@set _day=%_day:~-2%
@set datevar=%_year%-%_month%-%_day%

@for /F "tokens=1-3 delims=:-. " %%A in ('echo %time%') do @set timevar=%%A-%%B-%%C&set hour=%%A
@rem Make sure that morning hours such as 9:00 are handled as 09 rather than 9
@if %hour% LSS 10 set timevar=0%timevar%
@set FileName=%etwtracedir%\%datevar%_%timevar% %username% heapsnapshot %pid%.etl
@echo Trace will be saved to %FileName%

@rem Don't start tracing until we are ready to do the snapshot.
@rem This avoids filling the trace with GB of process-create
@rem events that we don't care about.
wpr -start heapsnapshot -filemode
wpr -singlesnapshot heap %pid%
wpr -stop "%temp%\UIforETW_heap_snapshot.etl"
@rem Merging the trace ensures that machine-specific data is pulled in, and
@rem gives an opportunity to compress the trace.
xperf -merge "%temp%\UIforETW_heap_snapshot.etl" "%FileName%" -compress
@if not %errorlevel% equ 0 goto noxperf
del "%temp%\UIforETW_heap_snapshot.etl"
@goto xperf_worked
:noxperf
@echo xperf.exe failed (or wasn't found). Skipping merge step. Trace may not work.
move "%temp%\UIforETW_heap_snapshot.etl" "%FileName%"
:xperf_worked
@echo Trace data is in %FileName% -- load it with wpa or xperfview or gpuview.
@dir "%FileName%" | find /i ".etl"

@goto WaitAgain

:leave
@rem When we are done, turn off heap tracing for this process.
wpr -snapshotconfig heap -pid %pid% disable
