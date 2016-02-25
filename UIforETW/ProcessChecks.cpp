#include "stdafx.h"

#include <TlHelp32.h>
#include <Wincrypt.h>

const size_t MD5LEN = 16;

void Hash(const wchar_t* const szExeFile)
{
	HCRYPTPROV hContext = 0;
	// Get handle to the crypto provider
	if (!CryptAcquireContext(&hContext, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		return;

	HCRYPTHASH hHash = 0;
	if (!CryptCreateHash(hContext, CALG_MD5, 0, 0, &hHash))
	{
		CryptReleaseContext(hContext, 0);
		return;
	}

	const wchar_t* pChars = szExeFile;
	while (*pChars)
	{
		wchar_t c = *pChars;
		// Apply simple lower-casing rules.
		if (c >= 'A' && c <= 'Z')
		{
			c = c + 'a' - 'A';
		}
		if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(&c), sizeof(c), 0))
		{
			CryptReleaseContext(hContext, 0);
			CryptDestroyHash(hHash);
			return;
		}
		++pChars;
	}

	// This executable breaks the ETW CPU sampler for some reason
	const BYTE badSamplingHash[MD5LEN] = { 0xfb, 0x69, 0xef, 0xd9, 0xea, 0x12, 0xdf, 0xcc, 0x22, 0xe9, 0x7a, 0x15, 0x01, 0x02, 0xb3, 0x3c };

	DWORD cbHash = MD5LEN;
	BYTE nameHash[MD5LEN];
	if (CryptGetHashParam(hHash, HP_HASHVAL, nameHash, &cbHash, 0))
	{
		if (memcmp(nameHash, badSamplingHash, sizeof(nameHash)) == 0)
		{
			const wchar_t* banner = L"------------------------------------------------------------";
			outputPrintf(L"\n%s\n", banner);
			outputPrintf(L"Warning!!! Process %s may cause the ETW CPU sampler to fail. Consider disabling %s.\n", szExeFile, szExeFile);
			outputPrintf(L"MD5 hash is: ");
			for (DWORD i = 0; i < cbHash; i++)
			{
				outputPrintf(L"%02x", nameHash[i]);
			}
			outputPrintf(L"\n");
			outputPrintf(L"%s\n", banner);
		}
	}

	CryptDestroyHash(hHash);
	CryptReleaseContext(hContext, 0);
}

void CheckProcesses()
{
	// CreateToolhelp32Snapshot runs faster than EnumProcesses and
	// it returns the process name as well, thus avoiding a call to
	// EnumProcessModules to get the name.
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, TH32CS_SNAPPROCESS);
	if (!hSnapshot)
		return;

	PROCESSENTRY32W peInfo;
	peInfo.dwSize = sizeof(peInfo);
	BOOL nextProcess = Process32First(hSnapshot, &peInfo);

	// Iterate through the processes.
	while (nextProcess)
	{
		Hash(peInfo.szExeFile);
		nextProcess = Process32Next(hSnapshot, &peInfo);
	}
	CloseHandle(hSnapshot);
}
