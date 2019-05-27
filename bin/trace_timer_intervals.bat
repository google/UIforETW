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

@rem Use this batch file to record timer interrupt interval/frequency changes.
@rem It relies on xperf, a Python script, wpaexporter, a .wpaProfile script,
@rem and IdentifyChromeProcesses.py. The result will be a summary, per-process,
@rem of what processes raised the timer frequency, to what level, how often, and
@rem for how long.
@rem The ETW traces also contain call stacks for all of the events recorded,
@rem making it possible to find out where in ones code the timer interval
@rem changes are coming from.

@setlocal

@set user_providers=Microsoft-Windows-Kernel-Power:0x4000000000000004
xperf.exe -start "NT Kernel Logger" -on PROC_THREAD+LOADER -f "%temp%\TimerDetailskernel.etl" -start TimerSession -on %user_providers%::'stack' -f "%temp%\TimerDetailsuser.etl"
@echo Run your scenario and then press enter.
pause
@xperf.exe -capturestate TimerSession %user_providers%
xperf.exe -stop TimerSession -stop "NT Kernel Logger"
xperf.exe -merge "%temp%\TimerDetailskernel.etl" "%temp%\TimerDetailsuser.etl" "%temp%\TimerDetails.etl" -compress

call python summarize_timer_intervals.py "%temp%\TimerDetails.etl"
