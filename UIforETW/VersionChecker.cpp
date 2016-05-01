#include "stdafx.h"
#include "VersionChecker.h"
#include "Version.h"

#include <afxinet.h>
#include <memory>

void CVersionChecker::VersionCheckerThread()
{
	UINT totalBytesRead = 0;
	// The version tag usually shows up about 20,000 bytes in.
	char buffer[40000];
	try
	{
		CInternetSession	MySession;
		const wchar_t* const url = L"https://github.com/google/UIforETW/releases/";
		std::unique_ptr<CStdioFile> webFile(MySession.OpenURL(url));
		// Read into the buffer -- set the maximum to one less than the length
		// of the buffer.
		while (totalBytesRead < sizeof(buffer) - 1)
		{
			UINT bytesRead = webFile->Read(buffer + totalBytesRead, sizeof(buffer) - 1 - totalBytesRead);
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
	const char* const marker = "<a href=\"/google/UIforETW/tree/";
	char* version_string = strstr(buffer, marker);
	if (version_string)
	{
		version_string += strlen(marker);
		if (strlen(version_string) > 4)
		{
			version_string[4] = 0;
			PackagedFloatVersion newVersion;
			newVersion.u = 0;
			if (sscanf_s(version_string, "v%f", &newVersion.f) == 1)
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
	CVersionChecker* pThis = (CVersionChecker*)pParam;
	pThis->VersionCheckerThread();
	return 0;
}

void CVersionChecker::StartVersionCheckerThread(CWnd* pWindow)
{
	pWindow_ = pWindow;
	hThread_ = CreateThread(nullptr, 0, StaticVersionCheckerThread, this, 0, nullptr);
}

CVersionChecker::CVersionChecker()
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
