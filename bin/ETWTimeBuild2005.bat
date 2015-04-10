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

@if "%1" == "" goto nofile
@if "%2" == "" goto nofile

@setlocal
@call "%VS80COMNTOOLS%..\..\VC\vcvarsall.bat"

@rem To conserve space all build profiling is done with stack walks disabled.

@rem These settings give sampled profiling plus full file I/O details.
@rem @set KernelProviders=Latency+POWER+DISPATCHER+file_io+FILE_IO_INIT

@rem These settings disable the sampled profiler, reducing file size by ~30%.
@rem @set KernelProviders=DiagEasy+POWER+file_io+FILE_IO_INIT

@rem These settings use a compact form of the CSwitch event and disable file I/O
@rem events. Disk I/O events are still enabled. File size is reduced by ~90%.
@rem Very little investigation will be possible, but overall CPU usage and disk
@rem usage is available.
@set KernelProviders=Diag

@set KBuffers=-buffersize 1024 -minbuffers 300
xperf -on %KernelProviders% %KBuffers%
@if not %errorlevel% equ 0 goto failure

@set starttime=%time%
@xperf -m "Starting build"

devenv %2 /rebuild release

@xperf -m "Finishing build"
@echo Build went from %starttime% to %time%
@xperf -stop -d %1
@exit /b

:nofile
@echo Don't forget to specify an ETW filename to save to and
@echo a solution file to build.
@echo For example: "TimeBuild2005 tracefile.etl" dotadlls.sln
@exit /b

:failure
@echo Failed to start tracing.
@echo Make sure you are running from an elevated command prompt.
@echo Force-stopping any previous sessions that may be interfering.
xperf -stop -d %1
@exit /b
