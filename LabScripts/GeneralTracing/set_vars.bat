@rem Copyright 2017 Google Inc. All Rights Reserved.
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

ver | find "6.1."
if %errorlevel% == 0 goto Windows7
rem Windows 8.0+ stuff goes here
rem Note that +Microsoft-Windows-MediaEngine, which gives us present events,
rem only works on non-server SKUs
set DX_Flags=Microsoft-Windows-DxgKrnl:0xFFFF:5+Microsoft-Windows-MediaEngine
goto Windows8

:Windows7
rem Windows 7 stuff goes here
set DX_Flags=DX:0x2F

:Windows8

rem %temp% should be a good location for temporary traces.
rem Make sure this is a fast drive, preferably an SSD.
set xperftemptracedir=%temp%
rem Select locations for the temporary kernel and user trace files.
set kernelfile=%xperftemptracedir%\kernel.etl
set userfile=%xperftemptracedir%\user.etl
set SessionName=usersession

rem PROC_THREAD+LOADER are required in order to know what binaries are loaded
rem and what threads are running.
rem CSWITCH records context switch data so that precise CPU usage and context
rem switch counts can be recorded. So, these three flags are our minimum set
rem of kernel providers. I then add in +POWER so that we can see CPU frequency
rem and power-state changes.
set KernelProviders=PROC_THREAD+LOADER+CSWITCH+POWER

rem These user providers record GPU usage, window-in-focus, eventemitter events
rem (if emitted), and process working-set samples.
set UserProviders=%DX_Flags%+Microsoft-Windows-Win32k:0xfdffffffefffffff+Multi-MAIN+Multi-FrameRate+Multi-Input+Multi-Worker+Microsoft-Windows-Kernel-Memory:0xE0
