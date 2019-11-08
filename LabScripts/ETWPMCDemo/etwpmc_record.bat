@rem Copyright 2016 Google Inc. All Rights Reserved.
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

if exist "C:\Program Files (x86)\Windows Kits\10\bin\x64\tracelog.exe" goto tracelog_exists
@echo Can't find tracelog.exe
@exit /b
:tracelog_exists

@set path=%path%;C:\Program Files (x86)\Windows Kits\10\bin\x64

@set batchdir=%~dp0
@set demo_app=C:\Windows\System32\wbem\WinMgmt.exe
@set demo_app_name=WinMgmt

@if exist "%demo_app%" goto demo_exists
@echo Please build the release configuration of ConditionalCount.exe. It needs
@echo to be here: %demo_app%
@exit /b

:demo_exists

@rem Start tracing with context switches and with a few CPU performance
@rem counters being recorded for each context switch.
tracelog.exe -start pmc_counters -f pmc_counter_test.etl -eflag CSWITCH+PROC_THREAD+LOADER -PMC BranchMispredictions,BranchInstructions:CSWITCH
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

@rem @echo Run your scenario and then press Enter when finished

"%demo_app%" /verifyrepository

xperf -stop pmc_counters >nul
xperf -merge pmc_counter_test.etl pmc_counters_test_merged.etl
xperf -i pmc_counters_test_merged.etl -o pmc_counters_test.txt
python %batchdir%etwpmc_parser.py pmc_counters_test.txt %demo_app_name%
