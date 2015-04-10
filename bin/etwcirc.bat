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

@rem Add this batch file's directory to the path.
@set batchdir=%~dp0
@set path=%path%;%batchdir%

@call etwcommonsettings.bat

@rem Set trace parameters. Latency is a good default group, and power adds
@rem CPU power management details
@rem Note that Latency is equal to PROC_THREAD+LOADER+DISK_IO+HARD_FAULTS+DPC+INTERRUPT+CSWITCH+PROFILE
@set CircKernelProviders=Latency+POWER+DISPATCHER+FILE_IO+FILE_IO_INIT

@set CircKernelStackWalk=-stackwalk PROFILE+CSWITCH+READYTHREAD
@rem Disable stack walking if you want to reduce the data rate.
@if not "%1" == "nostacks" goto RecordStacks
@set CircKernelStackWalk=
@echo Turning off call stacks.
:RecordStacks

@set CircSessionName=circgamesession

@rem See if the circgamesession logger is enabled. If it is then that means
@rem that our circular buffer logging is already running, and 'find'
@rem will set errorlevel to 0.
@xperf -loggers | find /i "%CircSessionName%" >nul
@if %errorlevel% equ 0 goto Running

@if "%1" == "stop" goto AlreadyStopped

@rem Buffer sizes are in KB. The ideal buffer size depends on how much physical memory is available
@rem and how long a trace you want. Normally the kernel buffer should be larger than the user
@rem buffer, but DX tracing does require a fairly large user buffer. CircBufSize is in KB so a buffer
@rem count of 400 and buffer size of 1024 gives a 400 MB buffer.
@set kCircBuffers=600
@set uCircBuffers=100
@set CircBufSize=1024
@set kCircBufferSettings=-buffering -minbuffers %kCircBuffers% -maxbuffers %kCircBuffers% -buffersize %CircBufSize%
@set uCircBufferSettings=-buffering -minbuffers %uCircBuffers% -maxbuffers %uCircBuffers% -buffersize %CircBufSize%

@rem Start the kernel provider and user-mode provider
@xperf -on %CircKernelProviders% %CircKernelStackWalk% %kCircBufferSettings% -start %CircSessionName% -on %UserProviders%+%CustomProviders% %uCircBufferSettings%
@if not %errorlevel% equ %INVALIDFLAGS% goto NotInvalidFlags
@echo Trying again without the custom providers. Run ETWRegister.bat to register them.
@xperf -on %CircKernelProviders% %CircKernelStackWalk% %kCircBufferSettings% -start %CircSessionName% -on %UserProviders% %uCircBufferSettings%
:NotInvalidFlags
@if not %errorlevel% equ 0 goto failure

@if "%1" == "nostacks" goto Argument2_OK
@if "%1" == "StartSilent" goto SkipInstructions
@if "%1" == "" goto Argument2_OK
@echo Ignoring parameter '%1'
:Argument2_OK
@echo Trace data is now being recorded to circular buffers.
@echo There should be %kCircBuffers% %CircBufSize%-KB buffers for NT Kernel Logger data and
@echo %uCircBuffers% %CircBufSize%-KB buffers for %CircSessionName% data. Use "xperf -loggers" to verify this.
@echo Use "etwcirc trace.etl" at anytime to save the buffers to disk.
@echo Use "etwcirc stop" or "etwcirc trace.etl stop" to stop tracing.
@echo When starting tracing you can use "etwcirc nostacks" to disable call stacks to reduce trace sizes.
:SkipInstructions
@exit /b

:failure
@rem Check for Access Denied
@if %errorlevel% == %ACCESSISDENIED% goto NotAdmin
@echo Failed to start tracing. Make sure the custom providers are registered
@echo (using etwregister.bat) or remove the line that adds them to UserProviders.
@echo Make sure you are running from an elevated command prompt.
@echo Forcibly stopping the kernel and user session to correct possible
@echo "file already exists" errors.
xperf -stop -stop %CircSessionName%
@echo Try again.
@exit /b



:FailureToRecord

@rem Rename the file back
@if not "%RenameErrorCode%" equ "0" goto SkipRename
@rename %HVEDir%\Amcache_temp.hve Amcache.hve
:SkipRename

@rem Delete the temporary ETL files
@del \*.etl
@echo Failed to record trace. Stopping circular tracing.
@echo Run 'etwcirc.bat' to restart circular tracing.
@goto stop



:Running
@if "%1" == "" goto nofile
@if "%1" == "StartSilent" goto nofile
@if "%1" == "stop" goto stop

@rem Rename c:\Windows\AppCompat\Programs\amcache.hve to avoid serious merge
@rem performance problems (up to six minutes!)
@set HVEDir=c:\Windows\AppCompat\Programs
@rename %HVEDir%\Amcache.hve Amcache_temp.hve 2>nul
@set RenameErrorCode=%errorlevel%

@rem Record the data but leaving tracing enabled.
@set CircKernelName=c:\kerneldata.etl
@set CircUserName=c:\userdata.etl
@echo Saving NT Kernel Logger and %CircSessionName% buffers to %1
@rem Latch necessary state from the user session.
xperf -capturestate %CircSessionName% %UserProviders%
@if not %errorlevel% equ 0 goto FailureToRecord

@rem Flush both sessions to disk.
xperf -flush "NT Kernel Logger" -f %CircKernelName% -flush %CircSessionName% -f %CircUserName%
@if not %errorlevel% equ 0 goto FailureToRecord
xperf -merge %CircKernelName% %CircUserName% %1
@if not %errorlevel% equ 0 goto FailureToRecord

@rem Rename the file back
@if not "%RenameErrorCode%" equ "0" goto SkipRename
@rename %HVEDir%\Amcache_temp.hve Amcache.hve
:SkipRename

@rem Delete the temporary ETL files
@del \*.etl
@echo Trace data is in %1 -- load it with wpa or xperfview or gpuview
start wpa %1
@if "%2" == "stop" goto stop
@exit /b

:stop
@xperf -stop %CircSessionName% -stop
@if not %errorlevel% equ 0 goto stopfailure
@echo NT Kernel Logger and %CircSessionName% stopped.
@exit /b

:stopfailure
@echo Failed to stop the circular logger. It may not have been running or you
@echo may be running from a non-admin command prompt.
@exit /b

:nofile
@echo Error: Tracing is already on and no ETW filename to save to was specified.
@echo For example: "etwcirc tracefile.etl"
@echo To stop tracing go: "etwcirc tracefile.etl stop"
@echo or "etwcirc stop"
@exit /b

:AlreadyStopped
@echo Circular tracing is already stopped.
@exit /b

:NotAdmin
@echo You must run this batch file as administrator.
@exit /b
