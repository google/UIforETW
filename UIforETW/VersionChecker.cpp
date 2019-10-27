#include "stdafx.h"
#include "VersionChecker.h"
#include "Version.h"

#include <afxinet.h>
#include <memory>

void CVersionChecker::VersionCheckerThread()
{
	UINT totalBytesRead = 0;
	// The Version.h file is about 470 bytes.
	char buffer[1000];
	try
	{
		CInternetSession	MySession;
		const wchar_t* const url = L"https://raw.githubusercontent.com/google/UIforETW/master/UIforETW/VersionCopy.h";
		std::unique_ptr<CStdioFile> webFile(MySession.OpenURL(url));
		// Read into the buffer -- set the maximum to one less than the length
		// of the buffer.
		while (totalBytesRead < sizeof(buffer) - 1)
		{
			const UINT bytesRead = webFile->Read(buffer + totalBytesRead, sizeof(buffer) - 1 - totalBytesRead);
			if (!bytesRead)
				break;
			totalBytesRead += bytesRead;
		}
		webFile->Close();
	}
	catch (...)
	{
	}
	// Null terminate the buffer.
	buffer[totalBytesRead] = 0;
	const char* const marker = "const float kCurrentVersion = ";
	char* version_string = strstr(buffer, marker);
	if (version_string)
	{
		version_string += strlen(marker);
		if (strlen(version_string) > 4)
		{
			// String will be something like: "1.32f" and we want to cut off at 'f'.
			version_string[5] = 0;
			PackagedFloatVersion newVersion;
			newVersion.u = 0;
			if (sscanf_s(version_string, "%f", &newVersion.f) == 1)
			{
				if (newVersion.f > kCurrentVersion)
				{
					pWindow_->PostMessage(WM_NEWVERSIONAVAILABLE, newVersion.u);
				}
			}
		}
	}
}

DWORD __stdcall CVersionChecker::StaticVersionCheckerThread(LPVOID pParam)
{
	CVersionChecker* pThis = static_cast<CVersionChecker*>(pParam);
	pThis->VersionCheckerThread();
	return 0;
}

void CVersionChecker::StartVersionCheckerThread(CWnd* pWindow) noexcept
{
	pWindow_ = pWindow;
	hThread_ = CreateThread(nullptr, 0, StaticVersionCheckerThread, this, 0, nullptr);
}

CVersionChecker::CVersionChecker() noexcept
{
}

CVersionChecker::~CVersionChecker()
{
	if (hThread_)
	{
		WaitForSingleObject(hThread_, INFINITE);
		CloseHandle(hThread_);
	}
}
