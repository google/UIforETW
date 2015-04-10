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

@rem Customize to specify the location of your provider DLL or EXE

@set DLLFileMain=%~dp0ETWProviders.dll
@set ManifestFileMain=%~dp0etwproviders.man



@echo This will register the custom ETW providers.

@set DLLFile=%DLLFileMain%
@if not exist %DLLFile% goto NoDLL

@set ManifestFile=%ManifestFileMain%
@if not exist %ManifestFile% goto NoManifest

xcopy /y %DLLFile% %temp%
wevtutil um %ManifestFile%
@if %errorlevel% == 5 goto NotElevated
wevtutil im %ManifestFile%
@:Done
@exit /b

@:NotElevated
@echo ETW providers must be registered from an elevated (administrator) command
@echo prompt. Try again from an elevated prompt.
@exit /b

@:NoDLL
@echo Can't find %DLLFileMain%
@exit /b

@:NoManifest
@echo Can't find %ManifestFileMain%
@exit /b
