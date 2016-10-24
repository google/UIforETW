// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

// Needed just for CCriticalSection and CSingleLock. Uggh.
#include <afxmt.h>

// Windows Header Files:
//#include <windows.h>
#include <VersionHelpers.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <assert.h>

#define UIETWASSERT( x ) assert( x )

// Global function for printing to the dialog output window.
inline void outputPrintf(_Printf_format_string_ const wchar_t* pFormat, ...) {}
// Needed for int64_t and friends
#include <inttypes.h>

// Using #define NOMINMAX would be nice but gdiplustypes.h *depends*
// on min/max macros, so the best I can do is to undefine them here.
#undef min
#undef max
