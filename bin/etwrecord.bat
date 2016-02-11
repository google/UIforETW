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

@rem Set the etwtracedir environment variable if it
@rem isn't set already.
@if not "%etwtracedir%" == "" goto TraceDirSet
@set etwtracedir=%homedrive%%homepath%\documents\etwtraces
:TraceDirSet

@rem Make sure %etwtracedir% exists
@if exist "%etwtracedir%" goto TraceDirExists
@mkdir "%etwtracedir%"
:TraceDirExists

@rem %temp% should be a good location for temporary traces.
@rem Make sure this is a fast drive, preferably an SSD.
@set xperftemptracedir=%temp%

@call etwcommonsettings.bat
@call etwregister.bat

@rem Generate a file name based on the current date and time and put it in
@rem etwtracedir. This is compatible with UIforETW which looks for traces there.
@rem Note: this probably fails in some locales. Sorry.
@for /F "tokens=2-4 delims=/- " %%A in ('date/T') do @set datevar=%%C-%%A-%%B
@for /F "tokens=1-3 delims=:-. " %%A in ('echo %time%') do @set timevar=%%A-%%B-%%C&set hour=%%A
@rem Make sure that morning hours such as 9:00 are handled as 09 rather than 9
@if %hour% LSS 10 set timevar=0%timevar%
@set FileName=%etwtracedir%\%datevar%_%timevar%_%username%

@if "%1" == "" goto NoFileSpecified
@set ext=%~x1
@if "%ext%" == ".etl" goto fullFileSpecified
@rem Must be just a sub-component -- add it to the end.
@set Filename=%FileName%_%1.etl
@goto FileSpecified

:fullFileSpecified
@set FileName=%1
@goto FileSpecified

:NoFileSpecified
@set FileName=%FileName%.etl
:FileSpecified
@echo Trace will be saved to %FileName%

@rem Set trace parameters. Latency is a good default group, and Power adds
@rem CPU power management details. Dispatcher allows wait classification and
@rem file IO is useful for seeing what disk reads are requested.
@rem Note that Latency is equal to PROC_THREAD+LOADER+DISK_IO+HARD_FAULTS+DPC+INTERRUPT+CSWITCH+PROFILE
@set KernelProviders=Latency+POWER+DISPATCHER+FILE_IO+FILE_IO_INIT

@set KernelStackWalk=-stackwalk PROFILE
@if "%2" == "nocswitchstacks" goto NoCSwitchStacks
@rem Recording call stacks on context switches is very powerful and useful
@rem but does make context switching more expensive.
@set KernelStackWalk=%KernelStackWalk%+CSWITCH+READYTHREAD
:NoCSwitchStacks

@rem Add recording of VirtAlloc data and stacks
@set KernelProviders=%KernelProviders%+VIRT_ALLOC
@set KernelStackWalk=%KernelStackWalk%+VirtualAlloc

@rem Modified to reduce context switching overhead
@rem @set KernelStackWalk=-stackwalk PROFILE
@rem Disable stack walking if you want to reduce the data rate.
@if "%2" == "nostacks" set KernelStackWalk=
@set SessionName=gamesession

@rem Set the buffer size to 1024 KB (default is 64-KB) and a minimum of 300 buffers.
@rem This helps to avoid losing events. Increase minbuffers if events are still lost,
@rem but be aware that the current setting locks up 300 MB of RAM.
@set KBuffers=-buffersize 1024 -minbuffers 1200

@rem Select locations for the temporary kernel and user trace files.
@set kernelfile=%xperftemptracedir%\kernel.etl
@set userfile=%xperftemptracedir%\user.etl

@rem Start the kernel provider and user-mode provider
xperf -on %KernelProviders% %KernelStackWalk% %KBuffers% -f "%kernelfile%" -start %SessionName% -on %UserProviders%+%CustomProviders% -f "%userfile%"
@if not %errorlevel% equ -2147023892 goto NotInvalidFlags
@echo Trying again without the custom providers. Run ETWRegister.bat to register them.
xperf -on %KernelProviders% %KernelStackWalk% %KBuffers% -f "%kernelfile%"  -start %SessionName% -on %UserProviders% -f "%userfile%"
:NotInvalidFlags
@if not %errorlevel% equ 0 goto failure

@echo Run the test you want to profile here
@pause

@rem Record the data and stop tracing
xperf -stop %SessionName% -stop
@set FileAndCompressFlags="%FileName%" -compress
@if "%NOETWCOMPRESS%" == "" goto compressTrace
@set FileAndCompressFlags="%FileName%"
:compressTrace

@rem New method -- allows requesting trace compression. This is a NOP on
@rem Windows 7 but on Windows 8 creates 5-7x smaller traces (that don't load on Windows 7)

@rem Rename c:\Windows\AppCompat\Programs\amcache.hve to avoid serious merge
@rem performance problems (up to six minutes!)
@set HVEDir=c:\Windows\AppCompat\Programs
@rename %HVEDir%\Amcache.hve Amcache_temp.hve 2>nul
@set RenameErrorCode=%errorlevel%

xperf -merge "%kernelfile%" "%userfile%" %FileAndCompressFlags%

@rem Rename the file back
@if not "%RenameErrorCode%" equ "0" goto SkipRename
@rename %HVEDir%\Amcache_temp.hve Amcache.hve
:SkipRename

@if not %errorlevel% equ 0 goto FailureToRecord
@rem Delete the temporary ETL files
@del "%kernelfile%""
@del "%userfile%"
@echo Trace data is in %FileName% -- load it with wpa or xperfview or gpuview.
@dir "%FileName%" | find /i ".etl"
@rem Preprocessing symbols to avoid delays with Chrome's huge symbols
@pushd "%batchdir%"
python StripChromeSymbols.py "%FileName%"
@popd
start wpa "%FileName%"
@exit /b

:FailureToRecord
@rem Delete the temporary ETL files
@del "%kernelfile%"
@del "%userfile%"
@echo Failed to record trace.
@exit /b

:failure
@rem Check for Access Denied
@if %errorlevel% == %ACCESSISDENIED% goto NotAdmin
@echo Failed to start tracing. Make sure the custom providers are registered
@echo (using etwregister.bat) or remove the line that adds them to UserProviders.
@echo Make sure you are running from an elevated command prompt.
@echo Forcibly stopping the kernel and user session to correct possible
@echo "file already exists" errors.
xperf -stop %SessionName% -stop
@exit /b

:NotAdmin
@echo You must run this batch file as administrator.
@exit /b
