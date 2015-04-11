@rem Use this batch file to profile the ETW profiler.
@rem If the save or merge steps of UIforETW or wprui
@rem are very slow then this batch file can profile them.
@rem Save this batch file for special occasions.

@setlocal
@set kernel=-start "Circular Kernel Context Logger" -on Latency
@set kernel=%kernel% -stackwalk PROFILE+CSWITCH+READYTHREAD
@set kernel=%kernel% -minbuffers 300 -maxbuffers 300 -buffersize 1024
@set kernel=%kernel% -f "%temp%\circkerneldata.etl"
xperf %kernel%
@echo Hit enter when enough data has been recorded.
@pause
xperf -stop "Circular Kernel Context Logger"

@rem Rename c:\Windows\AppCompat\Programs\amcache.hve to avoid serious merge
@rem performance problems (up to six minutes!)
@set HVEDir=c:\Windows\AppCompat\Programs
@rename %HVEDir%\Amcache.hve Amcache_temp.hve 2>nul
@set RenameErrorCode=%errorlevel%

xperf -merge "%temp%\circkerneldata.etl" "%temp%\metatrace.etl"

@rem Rename the file back
@if not "%RenameErrorCode%" equ "0" goto SkipRename
@rename %HVEDir%\Amcache_temp.hve Amcache.hve
:SkipRename
