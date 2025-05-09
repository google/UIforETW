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

@rem Prerequisite for the WPT installer
set wptredistmsi1=Windows Performance Toolkit\Redistributables\WPTx64 (OnecoreUAP)-x64_en-us.msi
@rem The WPT installer
set wptredistmsi2=Windows Performance Toolkit\Redistributables\WPTx64 (DesktopEditions)-x64_en-us.msi

set wpt10=c:\Program Files (x86)\Windows Kits\10\
if not exist "%wpt10%%wptredistmsi1%" goto nowpt10
if not exist "%wpt10%%wptredistmsi2%" goto nowpt10
mkdir %destdir%\third_party\wpt10
xcopy "%wpt10%%wptredistmsi1%" %destdir%\third_party\wpt10
xcopy "%wpt10%%wptredistmsi2%" %destdir%\third_party\wpt10
@if errorlevel 1 goto copyfailure
xcopy "%wpt10%Licenses\10.0.22000.0\sdk_license.rtf" %destdir%\third_party\wpt10
@if errorlevel 1 goto copyfailure
ren %destdir%\third_party\wpt10\sdk_license.rtf LICENSE.rtf

@rem Add VS tools to the path. Also adds signtool.exe to the path.
set vcvars32="c:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist %vcvars32% goto community_installed
set vcvars32="C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
:community_installed
call %vcvars32% amd64

@rem Build DelayedCreateProcess.exe to the bin directory
@echo Building DelayedCreateProcess.exe
call DelayedCreateProcess\make.bat
del DelayedCreateProcess.obj

cd /d %UIforETW%ETWInsights
@echo Building ETWInsights
devenv /rebuild "release|Win32" ETWInsights.sln
@if ERRORLEVEL 1 goto BuildFailure
xcopy Release\flame_graph.exe %UIforETW%\bin /y
cd /d %UIforETW%

@echo Building DummyChrome (DLL for registering Chrome ETW events)
devenv /rebuild "release|Win32" DummyChrome\DummyChrome.sln
@if ERRORLEVEL 1 goto BuildFailure

@echo Building ETWProviders (three versions)
devenv /rebuild "release|Win32" ETWProviders\ETWProviders.sln
@if ERRORLEVEL 1 goto BuildFailure
devenv /rebuild "release|x64" ETWProviders\ETWProviders.sln
@if ERRORLEVEL 1 goto BuildFailure
devenv /rebuild "release|arm64" ETWProviders\ETWProviders.sln
@if ERRORLEVEL 1 goto BuildFailure

@echo Building RetrieveSymbols
devenv /rebuild "release|Win32" RetrieveSymbols\RetrieveSymbols.sln
@if ERRORLEVEL 1 goto BuildFailure

cd /d %UIforETW%UIforETW
@rem Modify the UIforETW project to be statically linked and build that version
@rem so that it will run without any extra install requirements.
sed "s/UIforETW.vcxproj/UIforETWStatic.vcxproj/" <UIforETW.sln >UIforETWStatic.sln
@echo First do a test build with ETW marks disabled to test the inline functions.
sed "s/_WINDOWS/_WINDOWS;DISABLE_ETW_MARKS/" <UIforETW.vcxproj >UIforETWStatic.vcxproj
@echo Building UIforETW 64-bit test build
devenv /rebuild "release|x64" UIforETWStatic.sln
@if ERRORLEVEL 1 goto BuildFailure

@rem Now prepare for the real builds
sed "s/UseOfMfc>Dynamic/UseOfMfc>Static/" <UIforETW.vcxproj >UIforETWStatic.vcxproj
if exist Release rmdir Release /s/q
if exist x64\Release rmdir x64\Release /s/q
@echo Building UIforETW 64-bit official build
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
xcopy %UIforETW%bin\UIforETWStatic_devrel.exe %destdir%\bin\UIforETW.exe /y
xcopy %UIforETW%bin\EventEmitter.exe %destdir%\bin /y
xcopy %UIforETW%bin\EventEmitter64.exe %destdir%\bin /y
xcopy %UIforETW%third_party\dbghelp.dll %destdir%\bin /y
xcopy %UIforETW%third_party\symsrv.dll %destdir%\bin /y

@rem Sign the important (requiring elevation) binaries
set bindir=%~dp0etwpackage\bin
signtool sign /a /d "UIforETW" /du "https://github.com/randomascii/UIforETW/releases" /n "Bruce Dawson" /tr http://timestamp.digicert.com /td SHA256 /fd SHA256 %bindir%\UIforETW.exe %bindir%\EventEmitter.exe %bindir%\EventEmitter64.exe %bindir%\flame_graph.exe %bindir%\RetrieveSymbols.exe %bindir%\DelayedCreateProcess.exe %bindir%\DummyChrome.dll %bindir%\ETWProviders.dll %bindir%\ETWProviders64.dll %bindir%\ETWProvidersARM64.dll
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
@rem https://raw.githubusercontent.com/randomascii/UIforETW/ea1129d25ba58efd03ef649829348ca553f82383/.gitignore
@rem From this template:
@rem HTTP_EXTRACT_TARGET=%HTTP_ALIAS%/%var2%/%var3%/raw/%var4%/%var5%
@rem With these sort of arguments:
@rem c:\github\uiforetw\uiforetw\utility.h*google*UIforETW*master*UIforETW/utility.h

powershell ..\GitHub-Source-Indexer\github-sourceindexer.ps1 -symbolsFolder etwsymbols -userID "google" ^
    -repository UIforETW -branch %hash% -sourcesRoot %UIforETW% -dbgToolsPath %temp%\srcsrv ^
    -verifyLocalRepo -gitHubUrl https://raw.githubusercontent.com -serverIsRaw
@echo Run these commands if you want to verify what has been indexed:
@echo %temp%\srcsrv\pdbstr -r -p:etwsymbols\UIforETWStatic_devrel.pdb -s:srcsrv
:NoSourceIndexing

@rem Copy the critical PE files to the symbol server as well, to demonstrate best practices.
copy etwpackage\bin\UIforETW*.exe etwsymbols

@rem Add the PE and PDB files to a local symbol server directory, to get the
@rem required directory structure:
if exist etwsymserver rmdir /s/q etwsymserver 
"c:\Program Files (x86)\Windows Kits\10\Debuggers\x64\symstore.exe" add /r /f etwsymbols /s etwsymserver /t uiforetw
@rem Delete the excess files.
rmdir etwsymserver\000Admin /s/q
del etwsymserver\pingme.txt
del etwsymserver\refs.ptr /s

@rem Make the redistributable .zip file
del *.zip 2>nul
call python3 make_zip_file.py etwpackage.zip etwpackage
@echo on

@rem Rename to the current version.
call python3 rename_to_version.py UIforETW\Version.h
@echo on

@echo Now upload the new etwpackage*.zip
@echo But make sure that the PersistedPresets section from startup10.wpaProfile
@echo been deleted to avoid shipping modified presets and bloating the file.
@echo After releasing a version with an updated version number be sure to trigger
@echo the new version checking by running:
@echo xcopy UIforETW\Version.h UIforETW\VersionCopy.h /y
@echo git add UIforETW\VersionCopy.h
@echo git commit -m "Updating VersionCopy.h for updated version checking"
@echo git push

cd etwsymserver
@rem Upload to the randomascii-symbols public symbol server.
@echo Final step - ready to upload the symbols?
@pause
call python3 c:\src\depot_tools\gsutil.py cp -Z -R  . gs://randomascii-symbols
cd ..
@echo on

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
