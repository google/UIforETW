/*
Copyright 2017 Google Inc. All Rights Reserved.

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
#include "TraceLoggingSupport.h"
#include "Utility.h"

#include <bcrypt.h>

#include <vector>
#include <array>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#pragma comment(lib, "Bcrypt.lib")

static std::system_error MakeSystemErrorFromWin32Error(DWORD errorCode, const char* context)
{
	const std::error_code code(errorCode, std::system_category());
	return std::system_error(code, context);
}
namespace
{
class SHA1HashProvider
{
	struct secret_init
	{
	};
	// this is just to force the destructor to be called
	// after this constructor finishes, regardless of whether or not
	// the calling constructor throws an exception
	SHA1HashProvider(secret_init) noexcept
		: algHandle_(nullptr)
		, hashHandle_(nullptr)
	{
	}
public:
	SHA1HashProvider(const SHA1HashProvider&) = delete;
	SHA1HashProvider(const SHA1HashProvider&&) = delete;
	SHA1HashProvider& operator=(const SHA1HashProvider&) = delete;
	SHA1HashProvider& operator=(const SHA1HashProvider&&) = delete;

	// /analyze warns that this should be tagged as noexcept, but it throws an exception.
	SHA1HashProvider()
		: SHA1HashProvider(secret_init())
	{
		NTSTATUS error = BCryptOpenAlgorithmProvider(&algHandle_, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
		if (error != 0)
		{
			algHandle_ = nullptr;
			throw MakeSystemErrorFromWin32Error(error, "BCryptOpenAlgorithmProvider");
		}

		DWORD objectLength;
		DWORD cbData;

		error = BCryptGetProperty(algHandle_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &cbData, 0);
		if (error != 0)
		{
			hashHandle_ = nullptr;
			throw MakeSystemErrorFromWin32Error(error, "BCryptGetProperty(BCRYPT_OBJECT_LENGTH)");
		}

		hashObject_.resize(objectLength);
		error = BCryptCreateHash(algHandle_, &hashHandle_, hashObject_.data(), objectLength, nullptr, 0, 0);
		if (error != 0)
		{
			hashHandle_ = nullptr;
			throw MakeSystemErrorFromWin32Error(error, "BCryptCreateHash");
		}
	}
	~SHA1HashProvider()
	{
		if (hashHandle_ != nullptr)
		{
			ATLVERIFY(BCryptDestroyHash(hashHandle_) == 0);
		}

		if (nullptr != algHandle_)
		{
			ATLVERIFY(BCryptCloseAlgorithmProvider(algHandle_, 0) == 0);
		}
	}

	void AddBytesForHash(const unsigned char* blob, unsigned long bytes)
	{
		// ugh. how did an API written for an OS released in 2006 not consider constness?
		// if this cast isn't safe, we'll crash as the namespace bytes should be in
		// .rdata.
		const NTSTATUS error = BCryptHashData(hashHandle_, const_cast<unsigned char*>(blob), bytes, 0);
		if (error != 0)
		{
			throw MakeSystemErrorFromWin32Error(error, "CryptHashData");
		}
	}
	std::array<unsigned char, 20> FinishHash() const
	{
		std::array<unsigned char, 20> hashData;
		const NTSTATUS error = BCryptFinishHash(hashHandle_, hashData.data(), static_cast<ULONG>(hashData.size()), 0);
		if (error != 0)
		{
			throw MakeSystemErrorFromWin32Error(error, "CryptGetHashParam");
		}
		return hashData;
	}

private:
	BCRYPT_ALG_HANDLE algHandle_;
	BCRYPT_HASH_HANDLE hashHandle_;
	std::vector<unsigned char> hashObject_;
};
}


// Intended to match .NET's ToUpperInvariant. Presumably
// "LOCALE_NAME_INVARIANT" in Win32 is a close enough proxy.
static std::wstring ToUpperInvariant(const std::wstring& mixed)
{
	std::wstring uppered;
	int destLength = static_cast<int>(mixed.size());
	if (destLength == 0)
		return mixed;

	// destLength doesn't include the terminating NUL, so add it.
	destLength++;
	{
		uppered.resize(destLength);
		wchar_t* upperedbuf = &uppered[0];
		const int result = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, mixed.c_str(),
			-1, upperedbuf, destLength, nullptr, nullptr, 0);
		const DWORD lastError = ::GetLastError();
		if (result > 0)
		{
			uppered.resize(result - 1);
			return uppered;
		}
		uppered.resize(0);
		if (lastError != ERROR_INSUFFICIENT_BUFFER)
			throw MakeSystemErrorFromWin32Error(lastError, "LCMapStringEx");
	}

	destLength = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, mixed.c_str(), -1, nullptr,
		0, nullptr, nullptr, 0);
	if (destLength == 0)
	{
		const DWORD lastError = ::GetLastError();
		throw MakeSystemErrorFromWin32Error(lastError, "LCMapStringEx");
	}

	{
		uppered.resize(destLength);
		wchar_t* upperedbuf = &uppered[0];
		const int result = ::LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, mixed.c_str(),
			-1, upperedbuf, destLength, nullptr, nullptr, 0);
		const DWORD lastError = ::GetLastError();
		if (result > 0)
		{
			uppered.resize(result - 1);
			return uppered;
		}
		throw MakeSystemErrorFromWin32Error(lastError, "LCMapStringEx");
	}
}


static std::vector<unsigned char> GetBigEndianBytes(const std::wstring& str)
{
	std::vector<unsigned char> bytes(str.size() * 2);

	for (size_t i = 0; i < str.size(); i++)
	{
		wchar_t u = str[i];
		bytes[i * 2] = u >> 8;
		bytes[i * 2 + 1] = u & 0xFF;
	}

	return bytes;
}


// Adapted from the C# here,
// https://blogs.msdn.microsoft.com/dcook/2015/09/08/etw-provider-names-and-guids/
// Which just has
// /*
// This sample code is public - domain and may be used for any purpose.
// It is provided without warrantee, express or implied.
// */
// for licensing information.
std::wstring TraceLoggingProviderNameToGUID(const std::wstring& provider)
{
	const std::vector<unsigned char> providerNameBytes = GetBigEndianBytes(ToUpperInvariant(provider));

	static constexpr std::array<unsigned char, 16> namespaceBytes =
	{
		0x48, 0x2C, 0x2D, 0xB2, 0xC3, 0x90, 0x47, 0xC8,
		0x87, 0xF8, 0x1A, 0x15, 0xBF, 0xC1, 0x30, 0xFB,
	};

	SHA1HashProvider hasher;
	hasher.AddBytesForHash(namespaceBytes.data(), static_cast<unsigned long>(namespaceBytes.size()));
	hasher.AddBytesForHash(providerNameBytes.data(), static_cast<unsigned long>(providerNameBytes.size()));

	auto hashBytes = hasher.FinishHash();
	// Guid = Hash[0..15], with Hash[7] tweaked according to RFC 4122
	hashBytes[7] = ((hashBytes[7] & 0x0F) | 0x50);
	GUID guidBytes;
	memcpy(&guidBytes, hashBytes.data(), sizeof(guidBytes));

	return stringPrintf(L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		guidBytes.Data1, guidBytes.Data2, guidBytes.Data3,
		guidBytes.Data4[0], guidBytes.Data4[1], guidBytes.Data4[2],
		guidBytes.Data4[3], guidBytes.Data4[4], guidBytes.Data4[5],
		guidBytes.Data4[6], guidBytes.Data4[7]);

}




