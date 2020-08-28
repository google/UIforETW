@setlocal
@echo off

@for %%f in (%userprofile%\documents\etwtraces\*.etl) DO (
        @echo Processing %%f
        call python %~dp0IdentifyChromeProcesses.py "%%f"
        call python %~dp0IdentifyChromeProcesses.py "%%f" -c
        call python %~dp0IdentifyChromeProcesses.py "%%f" -c -t
  )
