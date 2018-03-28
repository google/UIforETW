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

#include <string>

class DirectoryMonitor
{
public:
	DirectoryMonitor(CWnd* pMainWindow) noexcept;
	~DirectoryMonitor();


	_Pre_satisfies_(this->hThread_ == 0)
	_Pre_satisfies_(this->hShutdownRequest_ == 0)
	void StartThread(const std::wstring* traceDir) noexcept;

private:
	static DWORD WINAPI DirectoryMonitorThreadStatic(LPVOID);
	DWORD DirectoryMonitorThread();

	HANDLE hThread_ = 0;
	HANDLE hShutdownRequest_ = 0;

	CWnd* mainWindow_ = nullptr;
	const std::wstring* traceDir_ = nullptr;

	DirectoryMonitor(const DirectoryMonitor&) = delete;
	DirectoryMonitor(const DirectoryMonitor&&) = delete;
	DirectoryMonitor& operator=(const DirectoryMonitor&) = delete;
	DirectoryMonitor& operator=(const DirectoryMonitor&&) = delete;
};
