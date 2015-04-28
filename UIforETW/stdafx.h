/*
Copyright 2015 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN            // Exclude rarely-used stuff from Windows headers
#endif

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS      // some CString constructors will be explicit

// turns off MFC's hiding of some common and often safely ignored warning messages
#define _AFX_ALL_WARNINGS


#define _ATL_ENABLE_PTM_WARNING                 //force the use of ANSI C++ standard-compliant syntax for pointer to member functions.
                                                //Using this macro will cause the C4867 compiler error to be generated when non-standard syntax is used to initialize a pointer to a member function.

#define _ATL_NO_AUTOMATIC_NAMESPACE 1

#include <afxwin.h>         // MFC core and standard components. I get ~1 billion error messages if this isn't first.
#include "targetver.h"
#include <afxext.h>         // MFC extensions

#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>             // MFC support for Windows Common Controls
#endif // _AFX_NO_AFXCMN_SUPPORT

#include <afxcontrolbars.h>     // MFC support for ribbons and control bars

#include <atlwin.h>             //ATL stuff
#include <sal.h>

// Global function for printing to the dialog output window.
void outputPrintf(_Printf_format_string_ PCWSTR pFormat, ...);
// Needed for int64_t and friends
#include <inttypes.h>

#include <VersionHelpers.h>
#include <exception>
#include <string>
#include <vector>
// Using #define NOMINMAX would be nice but gdiplustypes.h *depends*
// on min/max macros, so the best I can do is to undefine them here.
#undef min
#undef max

// Send this when the list of traces needs to be updated.
const int WM_UPDATETRACELIST = WM_USER + 10;


#ifdef _UNICODE
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif
