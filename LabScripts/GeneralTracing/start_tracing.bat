@rem Copyright 2017 Google Inc. All Rights Reserved.
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

@echo off
setlocal

call %~dp0set_vars.bat

if not "%USE_EVENT_EMITTER%" == "1" goto SkipEventEmitting
rem Stop any previous event emitter binaries
%~dp0..\..\bin\EventEmitter.exe -kill
rem Start emitting ETW events for the tracing to record.
if not exist %~dp0..\..\bin\EventEmitter.exe goto SkipEventEmitting
start %~dp0..\..\bin\EventEmitter.exe
:SkipEventEmitting

rem Stop any previous tracing sessions that may have accidentally been left
rem running. Otherwise the start command will fail with incredibly cryptic
rem errors. Ignore all warnings because in most cases they are expected.
xperf -stop %SessionName% -stop 2>nul

if "%1" == "detailed" goto detailed
xperf.exe -start %logger% -on %KernelProviders% -buffersize 1024 -minbuffers 60 -maxbuffers 60 -f "%kernelfile%" -start %SessionName% -on %UserProviders% -f "%userfile%
goto skipdetailed
:detailed
rem These are some typical flags used by UIforETW to get more detailed traces,
rem which we optionally enable for lab tracing.
rem This extra information will distort timings. This is particularly true
rem of the CSwitch and ReadyThread stackwalk flags.
set KernelProviders=%KernelProviders%+CSWITCH+DISK_IO+HARD_FAULTS+DPC+INTERRUPT+PROFILE
set KernelProviders=%KernelProviders%+POWER+DISPATCHER+DISK_IO_INIT+FILE_IO+FILE_IO_INIT+VIRT_ALLOC+MEMINFO
set StackWalkFlags=Profile+CSwitch+ReadyThread
xperf.exe -start %logger% -on %KernelProviders% -stackwalk %StackWalkFlags% -buffersize 1024 -minbuffers 160 -maxbuffers 160 -f "%kernelfile%" -start %SessionName% -on %UserProviders% -f "%userfile%
:skipdetailed

rem You have to invoke -capturestate because otherwise the ETW gnomes will not
rem reliably record the user-mode data to your trace. Do not anger the gnomes.
xperf.exe -capturestate %SessionName% %UserProviders%

rem Insert a mark just after tracing starts
xperf -m LabScriptsStarting
