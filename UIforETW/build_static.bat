@rem Copyright 2015 Google Inc. All Rights Reserved.
@rem
@rem Licensed under the Apache License, Version 2.0 (the "License");
@rem you may not use this file except in compliance with the License.
@rem You may obtain a copy of the License at
@rem
@rem     http://www.apache.org/licenses/LICENSE-2.0
@rem
@rem Unless required by applicable law or agreed to in writing, software
@rem distributed under the License is distributed on an "AS IS" BASIS,
@rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
@rem See the License for the specific language governing permissions and
@rem limitations under the License.

@rem Modify the UIforETW project to be statically linked and build that version
@rem so that it will run without any extra install requirements.

@setlocal

pushd %~dp0

call "%VS120COMNTOOLS%..\..\VC\vcvarsall.bat"

sed "s/UseOfMfc>Dynamic/UseOfMfc>Static/" <UIforETW.vcxproj >UIforETWStatic.vcxproj
sed "s/UIforETW.vcxproj/UIforETWStatic.vcxproj/" <UIforETW.sln >UIforETWStatic.sln
if exist Release rmdir Release /s/q
if exist x64\Release rmdir x64\Release /s/q
devenv /rebuild "release|Win32" UIforETWStatic.sln
devenv /rebuild "release|x64" UIforETWStatic.sln
del UIforETWStatic.vcxproj
del UIforETWStatic.sln

@rem Clean up the build directories at the end to avoid subsequent build warnings.
rmdir Release /s/q
rmdir x64\Release /s/q

@rem Copy the results to the release executables
pushd %~dp0..\bin
echo >UIforETW32.exe
xcopy /y UIforETWStatic_devrel32.exe UIforETW32.exe
echo >UIforETW.exe
xcopy /y UIforETWStatic_devrel.exe UIforETW.exe
popd
