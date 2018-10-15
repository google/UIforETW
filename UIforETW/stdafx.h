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

#ifdef _DEBUG
#define OUTPUT_DEBUG_STRINGS
#endif

// disable the MFC "feature pack" controls
// they end up bringing dependencies on DLLs that are not 
// documented as available on the Server Core SKUs.
#define _AFX_NO_MFC_CONTROLS_IN_DIALOGS

// disable turning old-style unqualified names to pointers to members
// when used in BEGIN_MESSAGE_MAP
#define _ATL_ENABLE_PTM_WARNING

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

void CheckProcesses();

// Using #define NOMINMAX would be nice but gdiplustypes.h *depends*
// on min/max macros, so the best I can do is to undefine them here.
#undef min
#undef max

// Send this when the list of traces needs to be updated.
const int WM_UPDATETRACELIST = WM_USER + 10;
// Send this when a new version is detected.
const int WM_NEWVERSIONAVAILABLE = WM_USER + 11;

// Disable "warning C6054: String 'buffer' might not be zero-terminated." because
// these warnings are spurious. They are caused by incorrect annotations on
// _vsnwprintf_s and swprintf_s. See this bug for details:
// https://connect.microsoft.com/VisualStudio/feedback/details/1570481/vs-2015-analyze-warns-that-vsprintf-s-doesnt-null-terminate-buffers
#pragma warning(disable : 6054)

// Disable these CppCoreCheck warnings:

// This seems to require a lot of restructuring for little benefit:
// warning C26485 : No array to pointer decay
// warning C26482 : Only index into arrays using constant expressions
#pragma warning(disable : 26485)
#pragma warning(disable : 26482)

// This seems to happen every time I (legitimately) pass the address of a local to a
// function, such as the address of an HKEY to RegOpenKeyExW
// warning C26486 : Don't pass a pointer that may be invalid to a function.
#pragma warning(disable : 26486)

// These fire whenever I use va_start:
// warning C26492 : Don't use const_cast to cast away const
// warning C26481 : Don't use pointer arithmetic. Use span instead
#pragma warning(disable : 26492)
#pragma warning(disable : 26481)

// I'm not convinced of the solution yet:
// warning C26429: Symbol is never tested for nullness, it can be marked as not_null
#pragma warning(disable : 26429)

// I disagree, for now anyway:
// warning C26494: Variable is uninitialized. Always initialize an object
#pragma warning(disable : 26494)

// This warning appears to be spurious in many cases:
// warning C26489 : Don't dereference a pointer that may be invalid
#pragma warning(disable : 26489)

// Maybe some other day. Disabling now to reduce the noise level.
// warning C26472 : Don't use a static_cast for arithmetic conversions. Use brace initialization, gsl::narrow_cast or gsl::narow
// warning C26490 : Don't use reinterpret_cast
#pragma warning(disable : 26472)
#pragma warning(disable : 26490)

// This warning fires when it says that it shouldn't, on unnamed objects passed
// as function parameters. This makes it useless.
// warning C26444 : Avoid unnamed objects with custom construction and destruction
#pragma warning(disable : 26444)

// This is basically how MFC *works*, so this warning is useless to me.
// warning C26434: Function '*::*' hides a non-virtual function 'CWnd::*'
#pragma warning(disable : 26434)

// I'm not convinced I want to buy into more annotations
// warning C26135: Missing annotation _Acquires_lock_(this->cs_) at function 'CriticalSection::Lock'.
#pragma warning(disable : 26135)

// This is a bad warning when the cast is between two typedefs that may or may not match
// warning C26473: Don't cast between pointer types where the source type and the target type are the same
#pragma warning(disable : 26473)

// This warning fires when multiplying two constants whose product can fit in a BYTE,
// so this warning is annoying and not helpful.
//  warning C26451: Arithmetic overflow: Using operator '*' on a 4 byte value and then casting the result to a 8 byte value.
#pragma warning(disable : 26451)

#define UIETWASSERT( x ) ATLASSERT( x )


#define IS_MFC_APP
