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

// Include this first so that the requested version is known.
#include "targetver.h"

// disable the MFC "feature pack" controls
// they end up bringing dependencies on DLLs that are not 
// documented as available on the Server Core SKUs.
#define _AFX_NO_MFC_CONTROLS_IN_DIALOGS

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN            // Exclude rarely-used stuff from Windows headers
#endif

#define _ATL_NO_AUTOMATIC_NAMESPACE 1
#define _ATL_NO_HOSTING // Avoid mshtml.h and its string-constant abuse

#include <afxwin.h>         // MFC core and standard components //THIS NEEDS TO BE THE VERY FIRST INCLUDE!

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS      // some CString constructors will be explicit

// turns off MFC's hiding of some common and often safely ignored warning messages
#define _AFX_ALL_WARNINGS

#include <afxext.h>         // MFC extensions

#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>             // MFC support for Windows Common Controls
#endif // _AFX_NO_AFXCMN_SUPPORT

#include <afxmt.h>

#include <atlwin.h>
#include <sal.h>
#include <VersionHelpers.h>

// Global function for printing to the dialog output window.
void outputPrintf(_Printf_format_string_ const wchar_t* pFormat, ...);
// Needed for int64_t and friends
#include <inttypes.h>

// Using #define NOMINMAX would be nice but gdiplustypes.h *depends*
// on min/max macros, so the best I can do is to undefine them here.
#undef min
#undef max

// Send this when the list of traces needs to be updated.
const int WM_UPDATETRACELIST = WM_USER + 10;


#define UIETWASSERT( x ) ATLASSERT( x )


#ifdef _UNICODE
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif
