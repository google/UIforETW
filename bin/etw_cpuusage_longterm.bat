@rem Copyright 2015 Google Inc. All Rights Reserved.
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

@rem Set the etwtracedir environment variable if it
@rem isn't set already.
@if not "%etwtracedir%" == "" goto TraceDirSet
@set etwtracedir=%homedrive%%homepath%\documents\xperftraces
:TraceDirSet

@rem Make sure %etwtracedir% exists
@if exist %etwtracedir% goto TraceDirExists
@mkdir %etwtracedir%
:TraceDirExists

@rem %temp% should be a good location for temporary traces.
@rem Make sure this is a fast drive, preferably an SSD.
@set xperftemptracedir=%temp%

@rem Generate a file name based on the current date and time and put it in
@rem etwtracedir. This is compatible with UIforETW which looks for traces there.
@rem Note: this probably fails in some locales. Sorry.
@for /F "tokens=2-4 delims=/- " %%A in ('date/T') do @set datevar=%%C-%%A-%%B
@for /F "tokens=1-3 delims=:-. " %%A in ('echo %time%') do @set timevar=%%A-%%B-%%C&set hour=%%A
@rem Make sure that morning hours such as 9:00 are handled as 09 rather than 9
@if %hour% LSS 10 set timevar=0%timevar%
@set tracefile=%etwtracedir%\%datevar%_%timevar%_per_process_cpu_usage.etl
@set textfile=%etwtracedir%\%datevar%_%timevar%_per_process_cpu_usage.txt

@set kernelfile=%temp%\kernel_trace.etl

@echo This repords context switches, DPCs, and interrupts and nothing else, to allow for very long running tracing.
xperf.exe -start "NT Kernel Logger" -on PROC_THREAD+LOADER+DPC+INTERRUPT+CSWITCH -buffersize 1024 -minbuffers 60 -maxbuffers 60 -f "%kernelfile%"
@set starttime=%time%
@if not %errorlevel% equ 0 goto failure
@echo Tracing started at %starttime%

@echo Run the test you want to profile here
@pause
xperf.exe -stop "NT Kernel Logger"
xperf.exe -merge "%kernelfile%" "%tracefile%" -compress
@del "%kernelfile%"
@echo Tracing ran from %starttime% to %time%
@echo Tracing ran from %starttime% to %time% > %textfile%

@echo Trace data is in "%tracefile%"
@dir "%tracefile%" | find /i ".etl"
@exit /b

:failure
@echo Failure!
@exit /b
