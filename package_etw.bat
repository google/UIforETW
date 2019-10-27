@setlocal

@echo Creating a package of files for easy use of ETW/WPT.

set UIforETW=%~dp0
set destdir=%UIforETW%etwpackage
set symboldir=%UIforETW%etwsymbols

rmdir %destdir% /s/q
rmdir %symboldir% /s/q
@if exist %destdir%\bin\UIforETW.exe goto pleasecloseUIforETW
@if exist %destdir%\bin\ETWProviders.dll goto pleasecloseSomething
mkdir %destdir%\bin
mkdir %destdir%\include
mkdir %destdir%\lib
mkdir %destdir%\third_party
mkdir %symboldir%

set wptredistmsi=Windows Performance Toolkit\Redistributables\WPTx64-x86_en-us.msi
set oldwptredistmsi=Windows Performance Toolkit\OldRedistributables\WPTx64-x86_en-us.msi

set wpt10=c:\Program Files (x86)\Windows Kits\10\
if not exist "%wpt10%%wptredistmsi%" goto nowpt10
if not exist "%wpt10%%oldwptredistmsi%" goto nooldwpt10
mkdir %destdir%\third_party\wpt10
mkdir %destdir%\third_party\oldwpt10
xcopy "%wpt10%%wptredistmsi%" %destdir%\third_party\wpt10
@if errorlevel 1 goto copyfailure
xcopy "%wpt10%%oldwptredistmsi%" %destdir%\third_party\oldwpt10
@if errorlevel 1 goto copyfailure
xcopy "%wpt10%Licenses\10.0.15063.0\sdk_license.rtf" %destdir%\third_party\wpt10
@if errorlevel 1 goto copyfailure
ren %destdir%\third_party\wpt10\sdk_license.rtf LICENSE.rtf

@rem Add VS tools to the path. Also adds signtool.exe to the path.
set vcvars32="C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars32.bat"
if exist %vcvars32% goto community_installed
set vcvars32="C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Auxiliary\Build\vcvars32.bat"
:community_installed
call %vcvars32%

@rem Build DelayedCreateProcess.exe to the bin directory
call DelayedCreateProcess\make.bat

cd /d %UIforETW%ETWInsights
devenv /rebuild "release|Win32" ETWInsights.sln
@if ERRORLEVEL 1 goto BuildFailure
xcopy Release\flame_graph.exe %UIforETW%\bin /y

cd /d %UIforETW%UIforETW
@rem Modify the UIforETW project to be statically linked and build that version
@rem so that it will run without any extra install requirements.
sed "s/UIforETW.vcxproj/UIforETWStatic.vcxproj/" <UIforETW.sln >UIforETWStatic.sln
@echo First do a test build with ETW marks disabled to test the inline functions.
sed "s/_WINDOWS/_WINDOWS;DISABLE_ETW_MARKS/" <UIforETW.vcxproj >UIforETWStatic.vcxproj
devenv /rebuild "release|Win32" UIforETWStatic.sln
@if ERRORLEVEL 1 goto BuildFailure

@rem Now prepare for the real builds
sed "s/UseOfMfc>Dynamic/UseOfMfc>Static/" <UIforETW.vcxproj >UIforETWStatic.vcxproj
if exist Release rmdir Release /s/q
if exist x64\Release rmdir x64\Release /s/q
devenv /rebuild "release|Win32" UIforETWStatic.sln
@if ERRORLEVEL 1 goto BuildFailure
devenv /rebuild "release|x64" UIforETWStatic.sln
@if ERRORLEVEL 1 goto BuildFailure
@rem Clean up after building the static version.
rmdir Release /s/q
rmdir x64\Release /s/q
del UIforETWStatic.vcxproj
del UIforETWStatic.sln

cd %UIforETW%\EventEmitter
devenv /rebuild "Release|x86" EventEmitter.sln
@if ERRORLEVEL 1 goto BuildFailure
devenv /rebuild "Release|x64" EventEmitter.sln
@if ERRORLEVEL 1 goto BuildFailure

xcopy %UIforETW%CONTRIBUTING %destdir%
xcopy %UIforETW%CONTRIBUTORS %destdir%
xcopy %UIforETW%AUTHORS %destdir%
xcopy %UIforETW%LICENSE %destdir%
xcopy %UIforETW%README %destdir%

xcopy /exclude:%UIforETW%excludecopy.txt %UIforETW%bin %destdir%\bin /s
xcopy %UIforETW%ETWProviders\etwproviders.man %destdir%\bin /y
xcopy /exclude:%UIforETW%excludecopy.txt %UIforETW%include %destdir%\include /s
xcopy /exclude:%UIforETW%excludecopy.txt %UIforETW%lib %destdir%\lib /s
xcopy /exclude:%UIforETW%excludecopy.txt %UIforETW%third_party %destdir%\third_party /s
@rem Get the destinations to exist so that the xcopy proceeds without a question.
echo >%destdir%\bin\UIforETW.exe
echo >%destdir%\bin\UIforETW32.exe
xcopy %UIforETW%bin\UIforETWStatic_devrel32.exe %destdir%\bin\UIforETW32.exe /y
xcopy %UIforETW%bin\UIforETWStatic_devrel.exe %destdir%\bin\UIforETW.exe /y
xcopy %UIforETW%bin\EventEmitter.exe %destdir%\bin /y
xcopy %UIforETW%bin\EventEmitter64.exe %destdir%\bin /y
xcopy %UIforETW%third_party\dbghelp.dll %destdir%\bin /y
xcopy %UIforETW%third_party\symsrv.dll %destdir%\bin /y

@rem Sign the important (requiring elevation) binaries
set bindir=%~dp0etwpackage\bin
signtool sign /d "UIforETW" /du "https://github.com/google/UIforETW/releases" /n "Bruce Dawson" /tr http://timestamp.digicert.com /td SHA256 /fd SHA256 %bindir%\UIforETW.exe %bindir%\UIforETW32.exe %bindir%\EventEmitter.exe %bindir%\EventEmitter64.exe %bindir%\flame_graph.exe %bindir%\RetrieveSymbols.exe %bindir%\DelayedCreateProcess.exe %bindir%\DummyChrome.dll %bindir%\ETWProviders.dll %bindir%\ETWProviders64.dll %bindir%\ETWProvidersARM64.dll
@if not %errorlevel% equ 0 goto signing_failure

@rem Copy the official binaries back to the local copy, for development purposes.
xcopy /exclude:%UIforETW%excludecopy.txt %destdir%\bin\UIforETW*.exe %UIforETW%bin /y
@if ERRORLEVEL 1 goto BuildFailure

@rem Grab all of the lab scripts
xcopy /exclude:%UIforETW%excludecopy.txt %UIforETW%LabScripts %destdir%\LabScripts /y /i /s
@if ERRORLEVEL 1 goto BuildFailure

xcopy %UIforETW%\bin\UIforETWStatic_devrel*.pdb %symboldir%

cd /d %UIforETW%

@rem Source indexing as described here:
@rem http://hamishgraham.net/post/GitHub-Source-Symbol-Indexer.aspx
@rem The next steps assume that you have cloned https://github.com/Haemoglobin/GitHub-Source-Indexer.git
@rem into the parent directory of the UIforETW repo.
@rem You also have to enable execution of power-shell scripts, by running something
@rem like this from an administrator command prompt:
@rem powershell Set-ExecutionPolicy Unrestricted
@rem Note that -dbgToolsPath and -verifyLocalRepo had to be added to the command line
@rem to get the script to behave. -dbgToolsPath is tricky because the default path has
@rem spaces and the script can't handle that, so I copy it to %temp%. God help you if
@rem that has spaces.
@rem verifyLocalRepo comes with its own challenges since github-sourceindexer.ps1 does
@rem a weak job of looking for git.exe. I had to edit FindGitExe to tell it exactly where
@rem to look - something like this:
@rem function FindGitExe {
@rem  	$gitexe = "c:\src\chromium\depot_tools\git-2.8.3-64_bin\cmd\git.exe"
@rem     if (Test-Path $gitexe) {
@rem         return $gitexe
@rem     }
if not exist ..\GitHub-Source-Indexer\github-sourceindexer.ps1 goto NoSourceIndexing
if exist %temp%\srcsrv rmdir %temp%\srcsrv /s/q
mkdir %temp%\srcsrv
xcopy /s /y "c:\Program Files (x86)\Windows Kits\10\Debuggers\x64\srcsrv" %temp%\srcsrv

@rem Crazy gymnastics to get the current git hash into an environment variable:
echo|set /p="set hash=">sethash.bat
call git log -1 --pretty=%%%%H >>sethash.bat
call sethash.bat
del sethash.bat

@rem We need to create this sort of URL:
@rem https://raw.githubusercontent.com/google/UIforETW/ea1129d25ba58efd03ef649829348ca553f82383/.gitignore
@rem From this template:
@rem HTTP_EXTRACT_TARGET=%HTTP_ALIAS%/%var2%/%var3%/raw/%var4%/%var5%
@rem With these sort of arguments:
@rem c:\github\uiforetw\uiforetw\utility.h*google*UIforETW*master*UIforETW/utility.h

powershell ..\GitHub-Source-Indexer\github-sourceindexer.ps1 -symbolsFolder etwsymbols -userID "google" ^
    -repository UIforETW -branch %hash% -sourcesRoot %UIforETW% -dbgToolsPath %temp%\srcsrv ^
    -verifyLocalRepo -gitHubUrl https://raw.githubusercontent.com -serverIsRaw
@echo Run these commands if you want to verify what has been indexed:
@echo %temp%\srcsrv\pdbstr -r -p:etwsymbols\UIforETWStatic_devrel.pdb -s:srcsrv
@echo %temp%\srcsrv\pdbstr -r -p:etwsymbols\UIforETWStatic_devrel32.pdb -s:srcsrv
:NoSourceIndexing

@rem Put the ETW symbols in symbol server format and .zip that.
@rem First, add them to a local symbol server directory
if exist etwsymserver rmdir /s/q etwsymserver 
"c:\Program Files (x86)\Windows Kits\10\Debuggers\x64\symstore.exe" add /r /f etwsymbols /s etwsymserver /t uiforetw
@rem Delete the excess files.
rmdir etwsymserver\000Admin /s/q
del etwsymserver\pingme.txt
del etwsymserver\refs.ptr /s

del *.zip 2>nul
call python make_zip_file.py etwpackage.zip etwpackage
@echo on
cd etwsymserver
call python ..\make_zip_file.py ..\etwsymbols.zip .
cd ..
@echo on
call python rename_to_version.py UIforETW\Version.h
@echo on

@echo Now upload the new etwpackage*.zip and etwsymbols*.zip
@echo But make sure that the PersistedPresets section from startup10.wpaProfile
@echo been deleted to avoid shipping modified presets and bloating the file.
@exit /b

:pleasecloseUIforETW
@echo UIforETW is running. Please close it and try again.
@exit /b

:pleasecloseSomething
@echo Something is running with ETWProviders.dll loaded. Please close it and try again.
@exit /b

:nowpt10
@echo WPT 10 redistributables not found. Aborting.
@exit /b

:nooldwpt10
@echo Old WPT 10 redistributables not found. Aborting.
@exit /b

:BuildFailure
@echo Build failure of some sort. Aborting.
@exit /b

:copyfailure
@echo Failed to copy file. Aborting.
@exit /b

:signing_failure
@echo Failed to sign files. Aborting.
@exit /b
