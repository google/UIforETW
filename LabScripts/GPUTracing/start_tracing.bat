@ver | find "6.1."
@if %errorlevel% == 0 goto Windows7
@rem Windows 8.0+ stuff goes here
@rem Note that +Microsoft-Windows-MediaEngine, which gives us present events,
@rem only works on non-server SKUs
set DX_Flags=Microsoft-Windows-DxgKrnl:0xFFFF:5+Microsoft-Windows-MediaEngine
goto Windows8

:Windows7
@rem Windows 7 stuff goes here
set DX_Flags=DX:0x2F

:Windows8

@rem %temp% should be a good location for temporary traces.
@rem Make sure this is a fast drive, preferably an SSD.
@set xperftemptracedir=%temp%
@rem Select locations for the temporary kernel and user trace files.
@set kernelfile=%xperftemptracedir%\kernel.etl
@set userfile=%xperftemptracedir%\user.etl
@set SessionName=usersession
@set FileName=trace.etl
@set FileAndCompressFlags="%FileName%" -compress
@set UserProviders=%DX_Flags%

xperf.exe -start %logger% -on PROC_THREAD+LOADER -buffersize 1024 -minbuffers 60 -maxbuffers 60 -f "%kernelfile%" -start %SessionName% -on %UserProviders% -f "%userfile%
@rem You have to invoke -capturestate because otherwise the ETW gnomes will not
@rem reliably record the user-mode data to your trace. Do not anger the gnomes.
xperf.exe -capturestate %SessionName% %UserProviders%
