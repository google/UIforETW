@echo off
set local
rem Signing pattern taken from:
rem https://textslashplain.com/2016/01/10/authenticode-in-2016/

rem Add path for signtool
set path=%path%;C:\Program Files (x86)\Windows Kits\10\bin\x64

rem So far I see no indications that SHA1 signing is actually needed.
rem signtool sign /d "UIforETW" /du "https://github.com/google/UIforETW/releases" /n "Bruce Dawson" /tr http://timestamp.digicert.com /fd SHA1 %~dp0bin\UIforETW.exe %~dp0bin\UIforETW32.exe
rem @if not %errorlevel% equ 0 goto failure

rem Sign both 64-bit and 32-bit versions of UIforETW with the same description.
signtool sign /d "UIforETW" /du "https://github.com/google/UIforETW/releases" /n "Bruce Dawson" /tr http://timestamp.digicert.com /td SHA256 /fd SHA256 %~dp0bin\UIforETW.exe %~dp0bin\UIforETW32.exe
@if not %errorlevel% equ 0 goto failure
exit /b

:failure
echo Signing failed!
