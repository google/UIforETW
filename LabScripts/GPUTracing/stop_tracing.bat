@setlocal

xperf -stop %SessionName% -stop
@rem Stop the event emitter process, if running.
%~dp0..\..\bin\EventEmitter.exe -kill

xperf -merge "%kernelfile%" "%userfile%" %FileAndCompressFlags%

@rem First print the per-process summary
@set CSVName=GPU_Utilization_Table_Randomascii_GPU_Summary_by_Process.csv
@if not exist %CSVName% goto skipDelete
@del %CSVName%
:skipDelete
@rem Ignore spurious warnings about upgrading field names that aren't even used
wpaexporter %FileName% -profile GPUUsageByProcess.wpaProfile 2>nul
@type %CSVName%


@rem Then print the details
@set CSVDetailsName=GPU_Utilization_Table_Randomascii_GPU_Utilization_Details.csv
@if not exist %CSVDetailsName% goto skipDelete
@del %CSVDetailsName%
:skipDelete
@rem Ignore spurious warnings about upgrading field names that aren't even used
wpaexporter %FileName% -profile GPUUsageDetails.wpaProfile 2>nul
@type %CSVDetailsName%
