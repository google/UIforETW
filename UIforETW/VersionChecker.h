#pragma once

// Use this for packaging up a float into a WPARAM in order to losslessly
// pass it in a windows message.
union PackagedFloatVersion
{
	unsigned u;
	float f;
};

class CVersionChecker
{
public:
	CVersionChecker() noexcept;
	~CVersionChecker();

	void StartVersionCheckerThread(CWnd* pWindow) noexcept;
	
private:
	static DWORD __stdcall StaticVersionCheckerThread(LPVOID);
	void VersionCheckerThread();

	HANDLE hThread_ = nullptr;
	CWnd* pWindow_ = nullptr;

	CVersionChecker(const CVersionChecker&) = delete;
	CVersionChecker(const CVersionChecker&&) = delete;
	CVersionChecker& operator=(const CVersionChecker&) = delete;
	CVersionChecker& operator=(const CVersionChecker&&) = delete;
};
