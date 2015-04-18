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
#include "DirectoryMonitor.h"
#include <assert.h>
#include "Utility.h"

DirectoryMonitor::DirectoryMonitor(CWnd* pMainWindow)
	: mainWindow_(pMainWindow)
{
}

// This function monitors the traceDir_ directory and sends a message to the main thread
// whenever anything changes. That's it. All UI work is done in the main thread.
DWORD WINAPI DirectoryMonitor::DirectoryMonitorThreadStatic(LPVOID pVoidThis)
{
	DirectoryMonitor* pThis = static_cast<DirectoryMonitor*>(pVoidThis);
	return pThis->DirectoryMonitorThread();
}

DWORD DirectoryMonitor::DirectoryMonitorThread()
{
	HANDLE hChangeHandle = FindFirstChangeNotification(traceDir_->c_str(), FALSE,
				FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);

	if (hChangeHandle == INVALID_HANDLE_VALUE)
	{
		assert(0);
		return 0;
	}

	HANDLE handles[] = { hChangeHandle, hShutdownRequest_ };
	for (;;)
	{
		DWORD dwWaitStatus = WaitForMultipleObjects(ARRAYSIZE(handles), &handles[0], FALSE, INFINITE);

		switch (dwWaitStatus)
		{
		case WAIT_OBJECT_0:
			mainWindow_->PostMessage(WM_UPDATETRACELIST, 0, 0);
			if (FindNextChangeNotification(hChangeHandle) == FALSE)
			{
				assert(0);
				return 0;
			}
			break;
		case WAIT_OBJECT_0 + 1:
			// Shutdown requested.
			return 0;

		default:
			assert(0);
			break;
		}
	}
  // Unreachable.
}

void DirectoryMonitor::StartThread(const std::wstring* traceDir)
{
	assert(hThread_ == 0);
	assert(hShutdownRequest_ == 0);
	traceDir_ = traceDir;
	// No error checking -- what could go wrong?
	hThread_ = CreateThread(nullptr, 0, DirectoryMonitorThreadStatic, this, 0, 0);
	hShutdownRequest_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);;
}

DirectoryMonitor::~DirectoryMonitor()
{
	if (hThread_)
	{
		SetEvent(hShutdownRequest_);
		WaitForSingleObject(hThread_, INFINITE);
		handle_close::closeHandle(hThread_);
		handle_close::closeHandle(hShutdownRequest_);
	}
}
