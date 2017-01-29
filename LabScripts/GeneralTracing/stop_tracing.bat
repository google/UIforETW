@setlocal

@rem Insert a mark just before tracing starts
xperf -m Stopping

xperf -stop %SessionName% -stop
@rem Stop the event emitter process, if running.
%~dp0..\..\bin\EventEmitter.exe -kill

xperf -merge "%kernelfile%" "%userfile%" %FileAndCompressFlags%

@rem Clean up any previous results
@del *.csv

@rem Export the marks that indicate the valid length of the trace. Put this
@rem .csv into the 'special' folder.
wpaexporter %FileName% -outputfolder special -profile special\Marks.wpaProfile 2>nul
@rem 'call' is needed for those systems where python is python.bat. Sigh...
call python CreateRangeBatch.py special\Marks_Summary_Table_Randomascii_Marks.csv >RangeBatch.bat
call RangeBatch.bat

@for %%P in (*.wpaProfile) do (
    @echo wpaexporter %FileName% %RANGE% -profile %%P 2>nul
    @rem Ignore spurious warnings about upgrading field names that aren't even used
    wpaexporter %FileName% %RANGE% -profile %%P 2>nul
)
