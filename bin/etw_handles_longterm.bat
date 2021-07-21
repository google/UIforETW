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

@set logger="Circular Kernel Context Logger"
@rem Can also use "NT Kernel Logger", but that conflicts with UIforETW
@rem Better to use a different kernel logger to let them run in parallel.

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
@set tracefile=%etwtracedir%\%datevar%_%timevar%_handle_tracking.etl
@set textfile=%etwtracedir%\%datevar%_%timevar%_handle_tracking.txt

@set kernelfile=%temp%\kernel_trace.etl

@rem Record minimal tracing information plus handle related events, and record
@rem call stacks on HandleCreate and HandleDuplicate.
@xperf.exe -start %logger% -on PROC_THREAD+LOADER+OB_HANDLE -stackwalk HandleCreate+HandleDuplicate -buffersize 1024 -minbuffers 60 -maxbuffers 60 -f "%kernelfile%"
@set starttime=%time%
@if not %errorlevel% equ 0 goto failure
@echo Low data-rate handle tracing started at %starttime%

@echo Run the leaky-handle test you want to profile here.
@rem Can replace "pause" with "timeout 3600" so that tracing automatically stops
@rem after an hour. But this will cause some process to wake up once a second
@rem to update the timeout status.
@pause
@xperf.exe -stop %logger%
@xperf.exe -merge "%kernelfile%" "%tracefile%" -compress
@del "%kernelfile%"
@echo Tracing ran from %starttime% to %time%
@echo Tracing ran from %starttime% to %time% > "%textfile%"

@echo Trace can be loaded using UIforETW or with:
@echo wpa "%tracefile%"
@echo To find the handle data look in WPA's Graph Explorer, Memory section, Handles, Outstanding Count by Process
@exit /b

:failure
@echo Failure! Stopping tracing to clear state. Try again.
@xperf.exe -stop %logger%
@exit /b
