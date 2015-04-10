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
@set tracedir=%batchdir%..\traces
@set temptracedir=%batchdir%..

@call etwcommonsettings.bat
@call etwregister.bat

@if "%1" == "" goto quit
@if "%2" == "" goto quit

@set Filename=%1

@rem     Kernel provider settings:
@set KernelFlags=latency+dispatcher+virt_alloc
@set KWalkFlags=VirtualAlloc
@set KBuffers=-buffersize 1024 -minbuffers 200

@rem     Heap provider settings:
@set HeapSessionName=HeapSession
@set HWalkFlags=HeapCreate+HeapDestroy+HeapAlloc+HeapRealloc
@rem Don't stack trace on HeapFree because we normally don't need that information.
@rem +HeapFree
@rem Heap tracing creates a lot of data so we need lots of buffers to avoid losing events.
@set HBuffers=-buffersize 1024 -minbuffers 1000

@rem     User provider settings:
@set SessionName=gamesession

@rem Stop the circular tracing if it is enabled.
@rem @call etwcirc stop

@rem Select locations for the temporary kernel and user trace files.
@rem These locations are chosen to be on the SSD and be in the directory
@rem that is excluded from virus scanning and bit9 hashing.
@set kernelfile=%temptracedir%\kernel.etl
@set userfile=%temptracedir%\user.etl
@set heapfile=%temptracedir%\heap.etl

@rem Enable heap tracing for your process as describe in:
@rem http://msdn.microsoft.com/en-us/library/ff190925(VS.85).aspx
@rem Note that this assumes that you pass the file name of your EXE.
reg add "HKLM\Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\%2" /v TracingFlags /t REG_DWORD /d 1 /f
@if not %errorlevel% == 0 goto failure

@echo Heap tracing is now enabled for future invocations of process %2. If profiling
@echo startup allocations, hit a key now and then start your application. If profiling
@echo allocations later in the application then start your application now and hit a
@echo key when you are ready to start tracing of allocations.
@pause

@rem Start kernel tracing
xperf -on %KernelFlags% -stackwalk %KWalkFlags% %KBuffers% -f %kernelfile%
@if not %errorlevel% equ 0 goto failure

@rem Start heap tracing
xperf -start %HeapSessionName% -heap -Pids 0 -stackwalk %HWalkFlags% %HBuffers% -f %heapfile%
@if not %errorlevel% equ 0 goto failure

@rem Start user-provider tracing
xperf -start %SessionName% -on %UserProviders%+%CustomProviders% -f %userfile%
@if not %errorlevel% equ -2147023892 goto NotInvalidFlags
@echo Trying again without the custom providers. Run ETWRegister.bat to register them.
xperf -start %SessionName% -on %UserProviders% -f %userfile%
:NotInvalidFlags
@if not %errorlevel% equ 0 goto failure

@echo Hit a key when you are ready to stop heap tracing.
@pause

@rem Rename c:\Windows\AppCompat\Programs\amcache.hve to avoid serious merge
@rem performance problems (up to six minutes!)
@set HVEDir=c:\Windows\AppCompat\Programs
@rename %HVEDir%\Amcache.hve Amcache_temp.hve 2>nul
@set RenameErrorCode=%errorlevel%

xperf -stop %SessionName% -stop %HeapSessionName% -stop "NT Kernel Logger"
@if "%NOETWCOMPRESS%" == "" goto compressTrace
@rem Don't compress trace, to allow loading them on Windows 7
xperf -merge %kernelfile% %userfile% %heapfile% %Filename%
goto nocompressTrace
:compressTrace
@rem New method -- allows requesting trace compression. This is a NOP on
@rem Windows 7 but on Windows 8 creates 5-7x smaller traces (that don't load on Windows 7)
@rem Default to trace compression.
xperf -merge %kernelfile% %userfile% %heapfile% %Filename% -compress
:nocompressTrace

@rem Rename the file back
@if not "%RenameErrorCode%" equ "0" goto SkipRename
@rename %HVEDir%\Amcache_temp.hve Amcache.hve
:SkipRename

@rem Delete heap tracing registry key
reg delete "HKLM\Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\%2" /v TracingFlags /f
@rem Delete the temporary ETL files
@del %kernelfile%
@del %userfile%
@del %heapfile%
@echo Trace data is in %FileName% -- load it with wpa or xperfview or gpuview.
@dir %FileName%
@echo Preprocessing symbols
@pushd %batchdir%
python StripChromeSymbols.py %FileName%
@popd
start wpa %Filename%
@rem Restart circular tracing.
@rem @call etwcirc StartSilent
@exit /b

:quit
@echo Specify an ETL file name and process name (not full path)
@echo Example: etwheap.bat allocdata.etl notepad.exe
@exit /b

:failure
@rem Check for Access Denied
@if %errorlevel% == %ACCESSISDENIED% goto NotAdmin
@echo Failed to start tracing. Make sure the custom providers are registered
@echo (using etwregister.bat) or remove the line that adds them to UserProviders.
@echo Make sure you are running from an elevated command prompt.
@echo Forcibly stopping the kernel and user session to correct possible
@echo "file already exists" errors.
xperf -stop %SessionName%
xperf -stop %HeapSessionName%
xperf -stop "NT Kernel Logger"
@del %kernelfile%
@del %userfile%
@del %heapfile%
@exit /b

:NotAdmin
@echo You must run this batch file as administrator.
@exit /b
