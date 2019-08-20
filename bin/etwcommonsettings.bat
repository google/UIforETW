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

@rem Customize 'CustomProviders' to your own ETW providers.

@set CustomProviders=Multi-MAIN+Multi-FrameRate+Multi-Input+Multi-Worker



@rem Set up startup profile if it is not already set
@set startupProfileDest=%USERPROFILE%\Documents\WPA Files
@if exist "%startupProfileDest%\Startup.wpaProfile" goto StartupDone
@if not exist %~dp0Startup.wpaProfile goto StartupDone
@mkdir "%startupProfileDest%"
@xcopy /y %~dp0Startup.wpaProfile "%startupProfileDest%"
:StartupDone

@rem Set _NT_SYMBOL_PATH if it is not already set

@if not "%_NT_SYMBOL_PATH%" == "" goto SymbolPathSet
set _NT_SYMBOL_PATH=SRV*c:\symbols*http://msdl.microsoft.com/download/symbols;SRV*c:\symbols*https://chromium-browser-symsrv.commondatastorage.googleapis.com
:SymbolPathSet


@rem Set some helpful environment variables
@set INVALIDFLAGS=-2147023892
@set ACCESSISDENIED=-2147024891

@rem Set the flag that tells the 64-bit kernel to keep stackwalking metadata locked in memory.
@rem Forcing users to do this step manually adds too much confusion. This won't take effect
@rem until a reboot
@REG ADD "HKLM\System\CurrentControlSet\Control\Session Manager\Memory Management" -v DisablePagingExecutive -d 0x1 -t REG_DWORD -f >nul




@rem Set up reasonable defaults for Microsoft user-mode ETW providers, based
@rem on the OS version. Don't detect Windows 7 specifically because I want
@rem this batch file to work on Windows 8 and above.
@rem http://en.wikipedia.org/wiki/Ver_(command)

@ver | find "5.1."
@if %errorlevel% == 0 goto WindowsXP

@ver | find "6.0."
@if %errorlevel% == 0 goto WindowsVista

@rem Windows 7+
@echo Windows 7+ settings
@rem Microsoft-Windows-Win32k adds Window focus events. This is available only
@rem on Windows 7 and above. The filtering is to avoid excessive traffic from
@rem the UserCrit events.
@set UserProviders=Microsoft-Windows-Win32k:0xfdffffffefffffff
@rem Memory and power monitoring, copied from UIforETW.
@set UserProviders=%UserProviders%+Microsoft-Windows-Kernel-Memory:0xE0+Microsoft-Windows-Kernel-Power

@ver | find "6.1."
@if %errorlevel% == 0 goto Windows7

:Windows8
@echo Windows 8+ settings
@rem This is the Windows 8 equivalent of the DX provider, for recording
@rem GPU utilization, etc.
@rem @set UserProviders=%UserProviders%+Microsoft-Windows-DxgKrnl
@rem Present events on Windows 8+ -- non-server SKUs only. Disabled for now.
@rem @set UserProviders=%UserProviders%+Microsoft-Windows-MediaEngine
@goto UserProvidersAreSet

:Windows7
@echo Windows 7 settings
@rem The :0x2F mask for the DX provider enables a subset of events. See xperf\gpuview\log.cmd for examples.
@rem DX providers can be useful for viewing GPU utilization, and more.
@rem Uncomment the following line to enable them.
@rem @set UserProviders=%UserProviders%+DX:0x2F
@goto UserProvidersAreSet

:WindowsVista
@echo Windows Vista settings
@echo Vista detected. Xperf works better on Windows 7.
@rem This provider isn't very useful but I need something non-blank here to
@rem avoid changing the syntax too much.
@set UserProviders=Microsoft-Windows-LUA
@goto UserProvidersAreSet

:WindowsXP
@echo User providers are not supported on Windows XP. Xperf works better on Windows 7.
@goto Done

:UserProvidersAreSet

@rem DWM providers are occasionally helpful. Uncomment the following line to enable them.
@rem @set UserProviders=%UserProviders%+Microsoft-Windows-Dwm-Dwm

:Done
