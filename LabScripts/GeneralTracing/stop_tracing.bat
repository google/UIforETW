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

rem Insert a mark just before tracing starts
xperf -m LabScriptsStopping

xperf -stop %SessionName% -stop

if not "%USE_EVENT_EMITTER%" == "1" goto SkipEventEmitting
rem Stop the event emitter process, if running.
%~dp0..\..\bin\EventEmitter.exe -kill
:SkipEventEmitting

xperf -merge "%kernelfile%" "%userfile%" %FileAndCompressFlags%

rem Clean up any previous results
del *.csv

rem Generate an exporter config file based on marks in the trace and all the
rem .wpaProfile files found.
rem 'call' is needed for those systems where python is python.bat. Sigh...
call python CreateExporterConfig.py %FileName% >exporterconfig.json
rem Export multiple sets of data for the specified time range
wpaexporter -exporterconfig exporterconfig.json 2>nul

call python SummarizeData.py
