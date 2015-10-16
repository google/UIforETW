@setlocal

@echo Creating a package of files for easy use of ETW/WPT.

set UIforETW=%~dp0
set destdir=%~dp0etwpackage

rmdir %destdir% /s/q
@if exist %destdir%\bin\UIforETW.exe goto pleasecloseUIforETW
@if exist %destdir%\bin\ETWProviders.dll goto pleasecloseSomething
mkdir %destdir%\bin
mkdir %destdir%\include
mkdir %destdir%\lib
mkdir %destdir%\third_party

set wptredistmsi=Windows Performance Toolkit\Redistributables\WPTx64-x86_en-us.msi
set wpt81=c:\Program Files (x86)\Windows Kits\8.1\
set wpt10=c:\Program Files (x86)\Windows Kits\10\
if not exist "%wpt81%%wptredistmsi%" goto nowpt81
mkdir %destdir%\third_party\wpt81
xcopy "%wpt81%%wptredistmsi%" %destdir%\third_party\wpt81
xcopy "%wpt81%sdk_license.rtf" %destdir%\third_party\wpt81
ren %destdir%\third_party\wpt81\sdk_license.rtf LICENSE.rtf

if not exist "%wpt10%%wptredistmsi%" goto nowpt10
mkdir %destdir%\third_party\wpt10
xcopy "%wpt10%%wptredistmsi%" %destdir%\third_party\wpt10
xcopy "%wpt10%Licenses\10.0.10240.0\sdk_license.rtf" %destdir%\third_party\wpt10
ren %destdir%\third_party\wpt10\sdk_license.rtf LICENSE.rtf

@rem Add VS tools to the path
@call "%vs120comntools%vsvars32.bat"

cd /d %UIforETW%
cd UIforETW
@rem Modify the UIforETW project to be statically linked and build that version
@rem so that it will run without any extra install requirements.
sed "s/UIforETW.vcxproj/UIforETWStatic.vcxproj/" <UIforETW.sln >UIforETWStatic.sln
@echo First do a test build with ETW marks disabled to test the inline functions.
sed "s/_WINDOWS/_WINDOWS;DISABLE_ETW_MARKS/" <UIforETW.vcxproj >UIforETWStatic.vcxproj
devenv /rebuild "release|Win32" UIforETWStatic.sln
@if ERRORLEVEL 1 goto BuildFailure

@rem Now prepare for the real builds
sed "s/UseOfMfc>Dynamic/UseOfMfc>Static/" <UIforETW.vcxproj >UIforETWStatic.vcxproj
rmdir Release /s/q
rmdir x64\Release /s/q
devenv /rebuild "release|Win32" UIforETWStatic.sln
@if ERRORLEVEL 1 goto BuildFailure
devenv /rebuild "release|x64" UIforETWStatic.sln
@if ERRORLEVEL 1 goto BuildFailure
@rem Clean up after building the static version.
rmdir Release /s/q
rmdir x64\Release /s/q
del UIforETWStatic.vcxproj
del UIforETWStatic.sln

xcopy %UIforETW%CONTRIBUTING %destdir%
xcopy %UIforETW%CONTRIBUTORS %destdir%
xcopy %UIforETW%AUTHORS %destdir%
xcopy %UIforETW%LICENSE %destdir%
xcopy %UIforETW%README %destdir%

xcopy /exclude:%~dp0excludecopy.txt %UIforETW%bin %destdir%\bin /s
xcopy /exclude:%~dp0excludecopy.txt %UIforETW%include %destdir%\include /s
xcopy /exclude:%~dp0excludecopy.txt %UIforETW%lib %destdir%\lib /s
xcopy /exclude:%~dp0excludecopy.txt %UIforETW%third_party %destdir%\third_party /s
@rem Get the destinations to exist so that the xcopy proceeds without a question.
echo >%destdir%\bin\UIforETW.exe
echo >%destdir%\bin\UIforETW32.exe
xcopy %UIforETW%bin\UIforETWStatic_devrel32.exe %destdir%\bin\UIforETW32.exe /y
xcopy %UIforETW%bin\UIforETWStatic_devrel.exe %destdir%\bin\UIforETW.exe /y
xcopy /exclude:%~dp0excludecopy.txt %destdir%\bin\UIforETW*.exe %~dp0bin /y

cd /d %UIforETW%
python %UIforETW%make_zip_file.py %UIforETW%etwpackage.zip etwpackage

@echo Now upload the new etwpackage.zip
@exit /b

:pleasecloseUIforETW
@echo UIforETW is running. Please close it and try again.
@exit /b

:pleasecloseSomething
@echo Something is running with ETWProviders.dll loaded. Please close it and try again.
@exit /b

:nowpt81
@echo WPT 8.1 redistributables not found. Aborting.
@exit /b

:nowpt10
@echo WPT 10 redistributables not found. Aborting.
@exit /b

:BuildFailure
@echo Build failure of some sort. Aborting.
@exit /b
