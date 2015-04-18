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

#include "stdafx.h"
#include <assert.h>
#include "KeyLoggerThread.h"
#include "ETWProviders\etwprof.h"
#include <atomic>
#include "Utility.h"

namespace
{

std::atomic<bool> g_LogKeyboardDetails = false;

LRESULT CALLBACK LowLevelKeyboardHook(int nCode, WPARAM wParam, LPARAM lParam)
{
	assert(nCode == HC_ACTION);
	// wParam is WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, or WM_SYSKEYUP

	KBDLLHOOKSTRUCT* pKbdLLHook = (KBDLLHOOKSTRUCT*)lParam;

	if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
	{
		// Translate some of the keys to character names to make them easier to read
		// in the trace. Note that having the character codes means that this is a
		// true key logger. Consider checking g_LogKeyboardDetails if this is a
		// concern.
		char buffer[20];
		const char* pLabel = buffer;
		DWORD code = pKbdLLHook->vkCode;
		if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9') || code == ' ')
		{
			if (!g_LogKeyboardDetails)
			{
				// Make letters and numbers generic, for privacy.
				if (code >= 'A' && code <= 'Z')
					code = 'A';
				else if (code >= '0' && code <= '9')
					code = '0';
			}
			sprintf_s(buffer, "%c", code);
		}
		else if (code >= VK_NUMPAD0 && code <= VK_NUMPAD9)
		{
			if (!g_LogKeyboardDetails)
				code = '0';
			sprintf_s(buffer, "%c", '0' + (code - VK_NUMPAD0));
		}
		else if (code >= VK_F1 && code <= VK_F12)
		{
			sprintf_s(buffer, "F%lu", code + 1 - VK_F1);
		}
		else
		{
			switch (code)
			{
			case VK_BACK:
				pLabel = "backspace";
				break;
			case VK_TAB:
				pLabel = "tab";
				break;
			case VK_RETURN:
				pLabel = "enter";
				break;
			case VK_LEFT:
				pLabel = "left";
				break;
			case VK_UP:
				pLabel = "up";
				break;
			case VK_RIGHT:
				pLabel = "right";
				break;
			case VK_DOWN:
				pLabel = "down";
				break;
			case VK_SHIFT:
			case VK_LSHIFT:
			case VK_RSHIFT:
				pLabel = "shift";
				break;
			case VK_CONTROL:
			case VK_LCONTROL:
			case VK_RCONTROL:
				pLabel = "control";
				break;
			case VK_MENU:
			case VK_LMENU:
			case VK_RMENU:
				pLabel = "alt";
				break;
			case VK_ESCAPE:
				pLabel = "esc";
				break;
			case VK_LWIN:
			case VK_RWIN:
				pLabel = "Win";
				break;
			case VK_OEM_PERIOD:
				pLabel = ".";
				break;
			default:
				pLabel = "<unknown key>";
				break;
			}
		}
		ETWKeyDown(code, pLabel, 0, 0);
	}

	return CallNextHookEx(0, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseHook(int nCode, WPARAM wParam, LPARAM lParam)
{
	assert(nCode == HC_ACTION);

	MSLLHOOKSTRUCT* pMouseLLHook = (MSLLHOOKSTRUCT*)lParam;

	int whichButton = -1;
	bool pressed = true;

	switch (wParam)
	{
	case WM_LBUTTONDOWN:
		whichButton = 0;
		break;
	case WM_MBUTTONDOWN:
		whichButton = 1;
		break;
	case WM_RBUTTONDOWN:
		whichButton = 2;
		break;
	case WM_LBUTTONUP:
		whichButton = 0;
		pressed = false;
		break;
	case WM_MBUTTONUP:
		whichButton = 1;
		pressed = false;
		break;
	case WM_RBUTTONUP:
		whichButton = 2;
		pressed = false;
		break;
	}

	if (whichButton >= 0)
	{
		if (pressed)
		{
			ETWMouseDown(whichButton, 0, pMouseLLHook->pt.x, pMouseLLHook->pt.y);
		}
		else
		{
			ETWMouseUp(whichButton, 0, pMouseLLHook->pt.x, pMouseLLHook->pt.y);
		}
	}
	else
	{
		if (wParam == WM_MOUSEWHEEL)
		{
			int wheelDelta = GET_WHEEL_DELTA_WPARAM(pMouseLLHook->mouseData);
			ETWMouseWheel(0, wheelDelta, pMouseLLHook->pt.x, pMouseLLHook->pt.y);
		}

		if (wParam == WM_MOUSEMOVE)
		{
			ETWMouseMove(0, pMouseLLHook->pt.x, pMouseLLHook->pt.y);
		}
	}

	return CallNextHookEx(0, nCode, wParam, lParam);
}



DWORD __stdcall InputThread(LPVOID)
{
	// Install a hook so that this thread will receive all keyboard messages. They must be
	// processed in a timely manner or else bad things will happen. Doing this on a
	// separate thread is a good idea, but even then bad things will happen to your system
	// if you halt in a debugger. Even simple things like calling printf() from the hook
	// can easily cause system deadlocks which render the mouse unable to move!
	HHOOK keyHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardHook, NULL, 0);
	HHOOK mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseHook, NULL, 0);

	if (!keyHook && !mouseHook)
		return 0;

	// Run a message pump -- necessary so that the hooks will be processed
	BOOL bRet;
	MSG msg;
	// Keeping pumping messages until WM_QUIT is received. If this is opened
	// in a child thread then you can terminate it by using PostThreadMessage
	// to send WM_QUIT.
	while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
	{
		if (bRet == -1)
		{
			// handle the error and possibly exit
			break;
		}
		else
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	// Unhook and exit.
	if (keyHook)
		UnhookWindowsHookEx(keyHook);
	if (mouseHook)
		UnhookWindowsHookEx(mouseHook);
	return 0;
}

}

void SetKeyloggingState(enum KeyLoggerState state)
{
	static HANDLE s_hThread = 0;
	static DWORD s_threadID;

	// If key logging should be off then terminate the thead
	// if it is running.
	if (state == kKeyLoggerOff)
	{
		if (s_hThread)
		{
			PostThreadMessage(s_threadID, WM_QUIT, 0, 0);
			WaitForSingleObject(s_hThread, INFINITE);
			handle_close::closeHandle(s_hThread);
			s_hThread = 0;
		}
		return;
	}

	// If key logging should be on then start the thread if
	// it isn't running.
	if (!s_hThread)
	{
		s_hThread = CreateThread(NULL, 0, InputThread, NULL, 0, &s_threadID);
	}

	switch (state)
	{
	case kKeyLoggerAnonymized:
		g_LogKeyboardDetails = false;
		break;
	case kKeyLoggerFull:
		g_LogKeyboardDetails = true;
		break;
	}
}
