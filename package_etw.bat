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

@rem Add VS tools to the path
@call "%vs120comntools%vsvars32.bat"

cd /d %UIforETW%
cd UIforETW
@rem Modify the UIforETW project to be statically linked and build that version
@rem so that it will run without any extra install requirements.
sed "s/UseOfMfc>Dynamic/UseOfMfc>Static/" <UIforETW.vcxproj >UIforETWStatic.vcxproj
sed "s/UIforETW.vcxproj/UIforETWStatic.vcxproj/" <UIforETW.sln >UIforETWStatic.sln
rmdir Release /s/q
rmdir x64\Release /s/q
devenv /rebuild "release|Win32" UIforETWStatic.sln
devenv /rebuild "release|x64" UIforETWStatic.sln
@rem Clean up after building the static version.
rmdir Release /s/q
rmdir x64\Release /s/q
del UIforETWStatic.vcxproj
del UIforETWStatic.sln

xcopy %UIforETW%\bin %destdir%\bin /s
del %destdir%\bin\*.lastcodeanalysissucceeded
del %destdir%\bin\*.pdb
xcopy %UIforETW%\include %destdir%\include /s
xcopy %UIforETW%\lib %destdir%\lib /s
xcopy %UIforETW%\third_party %destdir%\third_party /s
del %destdir%\bin\.gitignore
del %destdir%\bin\UIforETW*_dev*.*
@rem Get the destinations to exist so that the xcopy proceeds without a question.
echo >%destdir%\bin\UIforETW.exe
echo >%destdir%\bin\UIforETW32.exe
xcopy %UIforETW%\bin\UIforETWStatic_devrel32.exe %destdir%\bin\UIforETW32.exe /y
xcopy %UIforETW%\bin\UIforETWStatic_devrel.exe %destdir%\bin\UIforETW.exe /y

python %UIforETW%make_zip_file.py %UIforETW%etwpackage.zip %destdir%

@echo Now upload the new etwpackage.zip
@exit /b

:pleasecloseUIforETW
@echo UIforETW is running. Please close it and try again.
@exit /b

:pleasecloseSomething
@echo Something is running with ETWProviders.dll loaded. Please close it and try again.
@exit /b
