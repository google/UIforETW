@rem Copyright 2021 Google Inc. All Rights Reserved.
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

@set tracelogdir=C:\Program Files (x86)\Windows Kits\10\bin\x64
@for /d %%f in ("C:\Program Files (x86)\Windows Kits\10\bin\10.*") do @if exist "%%f\x64\tracelog.exe" set tracelogdir=%%f\x64

@if exist "%tracelogdir%\tracelog.exe" goto tracelog_exists
@echo Can't find tracelog.exe
@exit /b
:tracelog_exists

@set path=%path%;%tracelogdir%

@set batchdir=%~dp0

@rem Start tracing with context switches and with a few CPU performance
@rem counters being recorded for each context switch.
tracelog.exe -start pmc_counters -f pmc_counter_temp.etl -eflag CSWITCH+PROC_THREAD+LOADER -PMC InstructionRetired,TotalCycles,CacheMisses,LLCMisses:CSWITCH
@if %errorlevel% equ 0 goto started
@echo Make sure you are running from an administrator command prompt
@exit /b
:started

@rem Counters can also be associated with other events such as PROFILE - just
@rem change both occurrences of CSWITCH to PROFILE. But the parsing code will have
@rem to be updated to make it meaningful.

@rem Other available counters can be found by running "tracelog -profilesources Help"
@rem and on my machine I see:
@rem Timer
@rem TotalIssues
@rem BranchInstructions
@rem CacheMisses
@rem BranchMispredictions
@rem TotalCycles
@rem UnhaltedCoreCycles
@rem InstructionRetired - shows same results as TotalIssues
@rem UnhaltedReferenceCycles
@rem LLCReference
@rem LLCMisses
@rem BranchInstructionRetired - shows same results as BranchInstructions
@rem BranchMispredictsRetired - not significantly different from BranchMispredictions

@echo Run your scenario now. Press any key when you are finished.
pause

xperf -stop pmc_counters >nul
xperf -merge pmc_counter_temp.etl pmc_counters_test.etl -compress
@del pmc_counter_temp.etl
xperf -i pmc_counters_test.etl -o pmc_counters_test.txt
call python %batchdir%etwpmc_parser.py pmc_counters_test.txt
@echo Rerun etwpmc_parser.py with a filter argument if you want certain
@echo processes to be summarized in more detail. For example:
@echo   python %batchdir%etwpmc_parser.py pmc_counters_test.txt chrome
