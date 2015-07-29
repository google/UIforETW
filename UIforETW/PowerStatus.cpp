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

#include <string>
#include <stdlib.h>

#include "PowerStatus.h"
#include <ETWProviders\etwprof.h>
#include "Utility.h"

#include <devguid.h>
#include <Setupapi.h>
#include <devioctl.h>
#include <Poclass.h>
#include <Batclass.h>

#pragma comment(lib, "setupapi.lib")

const int kSamplingInterval = 200;

// These correspond to the funcID values returned by GetMsrFunc
const int MSR_FUNC_FREQ = 0;
const int MSR_FUNC_POWER = 1;
const int MSR_FUNC_TEMP = 2;
const int MSR_FUNC_MAX_POWER = 3; /* ????? */

namespace
{
	void markETWEvent(const BATTERY_STATUS& bs, const BATTERY_INFORMATION& bi)
	{
		char powerState[100];
		powerState[0] = 0;
		if (bs.PowerState & BATTERY_CHARGING)
		{
			strcat_s(powerState, "Charging");
		}
		if (bs.PowerState & BATTERY_DISCHARGING)
		{
			strcat_s(powerState, "Discharging");
		}
		if (bs.PowerState & BATTERY_POWER_ON_LINE)
		{
			if (powerState[0])
			{
				strcat_s(powerState, ", on AC power");
			}
			else
			{
				strcat_s(powerState, "On AC power");
			}
		}

		const float batPercentage = bs.Capacity * 100.f / bi.FullChargedCapacity;

		char rate[100];

		if (bs.Rate == BATTERY_UNKNOWN_RATE)
		{
			sprintf_s(rate, "Unknown rate");
		}
		else if (bi.Capabilities & BATTERY_CAPACITY_RELATIVE)
		{
			sprintf_s(rate, "%ld (unknown units)", bs.Rate);
		}
		else
		{
			sprintf_s(rate, "%1.3f watts", bs.Rate / 1000.0f);
		}

		ETWMarkBatteryStatus(powerState, batPercentage, rate);
		//outputPrintf(L"%S, %1.3f%%, %S\n", powerState, batPercentage, rate);
	}

	struct HDEVINFO_battery final
	{
		HDEVINFO hdev;
		HDEVINFO_battery()
		{
			hdev = SetupDiGetClassDevs(&GUID_DEVCLASS_BATTERY,
									   0,
									   0,
									   (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
		}

		_At_(this->hdev, _Post_ptr_invalid_)
		~HDEVINFO_battery( )
		{
			if (hdev == INVALID_HANDLE_VALUE)
			{
				return;
			}
			const BOOL destroyed = SetupDiDestroyDeviceInfoList( hdev );
			if (!destroyed)
			{
				std::terminate( );
			}
		}
		HDEVINFO_battery(const HDEVINFO_battery&) = delete;
		HDEVINFO_battery& operator=(const HDEVINFO_battery&) = delete;
	};

	struct Battery final
	{
		HANDLE hBattery;
		Battery(PCTSTR DevicePath)
		{
			hBattery = CreateFile(DevicePath,
								   (GENERIC_READ | GENERIC_WRITE),
								   (FILE_SHARE_READ | FILE_SHARE_WRITE),
								   NULL,
								   OPEN_EXISTING,
								   FILE_ATTRIBUTE_NORMAL,
								   NULL);
		}

		_At_(this->hBattery, _Post_ptr_invalid_)
		~Battery( )
		{
			if (hBattery == INVALID_HANDLE_VALUE)
			{
				return;
			}
			CloseValidHandle( hBattery );
		}
		Battery(const Battery&) = delete;
		Battery& operator=(const Battery&) = delete;
	};

} //namespace

void CPowerStatusMonitor::SampleCPUPowerState()
{
	if (!IntelEnergyLibInitialize || !GetNumMsrs || !GetMsrName || !GetMsrFunc ||
		!GetPowerData || !ReadSample)
	{
		return;
	}

	int numMSRs = 0;
	GetNumMsrs(&numMSRs);
	ReadSample();
	for (int i = 0; i < numMSRs; ++i)
	{
		int funcID;
		wchar_t MSRName[1024];
		GetMsrFunc(i, &funcID);
		GetMsrName(i, MSRName);

		int nData;
		double data[3] = {};
		GetPowerData(0, i, data, &nData);

		if (funcID == MSR_FUNC_FREQ)
		{
			ETWMarkCPUFrequency(MSRName, (float)data[0]);
			//outputPrintf(L"%s = %4.0f MHz\n", MSRName, data[0]);
		}
		else if (funcID == MSR_FUNC_POWER)
		{
			// Round to nearest .0001 to avoid distracting excess precision.
			data[0] = round(data[0] * 10000) / 10000;
			data[2] = round(data[2] * 10000) / 10000;
			ETWMarkCPUPower(MSRName, data[0], data[2]);
			//outputPrintf(L"%s Power (W) = %3.2f\n", MSRName, data[0]);
			//outputPrintf(L"%s Energy(J) = %3.2f\n", MSRName, data[1]);
			//outputPrintf(L"%s Energy(mWh)=%3.2f\n", MSRName, data[2]);
		}
		else if (funcID == MSR_FUNC_TEMP)
		{
			// The 3.02 version of Intel Power Gadget seems to report the temperature
			// in F instead of C.
			ETWMarkCPUTemp(MSRName, data[0], maxTemperature_);
			//outputPrintf(L"%s Temp (C) = %3.0f (max is %3.0f)\n", MSRName, data[0], (double)maxTemperature_);
		}
		else if (funcID == MSR_FUNC_MAX_POWER)
		{
			//outputPrintf(L"%s Max Power (W) = %3.0f\n", MSRName, data[0]);
		}
		else
		{
			//outputPrintf(L"Unused funcID %d\n", funcID);
		}
	}
}

void CPowerStatusMonitor::SampleBatteryStat()
{
	HDEVINFO_battery hDev;
	if (hDev.hdev == INVALID_HANDLE_VALUE)
	{
		return;
	}

	// Avoid infinite loops.
	const int maxBatteries = 5;
	for (int deviceNum = 0; deviceNum < maxBatteries; deviceNum++)
	{
		SP_DEVICE_INTERFACE_DATA did = { sizeof(did) };

		if(!SetupDiEnumDeviceInterfaces(hDev.hdev, 0, &GUID_DEVCLASS_BATTERY,
										deviceNum, &did))
		{
			const DWORD lastErr = ::GetLastError( );
			if (lastErr == ERROR_NO_MORE_ITEMS)
			{
				break; // Enumeration failed - perhaps we're out of items
			}
			continue;
		}

		DWORD bytesNeeded = 0;
		const BOOL getDetailResult = SetupDiGetDeviceInterfaceDetail(hDev.hdev,
			&did, 0, 0, &bytesNeeded, 0);
		UIETWASSERT(!getDetailResult);
		if (getDetailResult)
		{
			// Expected SetupDiGetDeviceInterfaceDetail failure. Success is confusing.
			std::terminate();
		}

		// Make sure the function behaves as expected.
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			continue;
		}

		// Suppress bogus /analyze warning.
		// warning C6102: Using 'bytesNeeded' from failed function call
		#pragma warning(suppress : 6102)
		std::vector<char> detailDataMemory(bytesNeeded);

		auto const pDeviceIfaceData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(detailDataMemory.data());
		pDeviceIfaceData->cbSize = sizeof(*pDeviceIfaceData);

		if (!SetupDiGetDeviceInterfaceDetail(hDev.hdev, &did, pDeviceIfaceData,
											bytesNeeded, NULL, 0))
		{
			//Battery NOT found
			continue;
		}

		// Found a battery. Query it.
		Battery hBat(pDeviceIfaceData->DevicePath);

		if (hBat.hBattery == INVALID_HANDLE_VALUE)
		{
			continue;
		}

		BATTERY_QUERY_INFORMATION bqi = {};

		DWORD dwWait = 0;
		DWORD dwOut;

		const BOOL tagQueried = DeviceIoControl(hBat.hBattery,
												IOCTL_BATTERY_QUERY_TAG,
												&dwWait, sizeof(dwWait),
												&bqi.BatteryTag,
												sizeof(bqi.BatteryTag), &dwOut,
												NULL);

		if (!(tagQueried && bqi.BatteryTag))
		{
			continue;
		}

		// With the tag, you can query the battery info.
		BATTERY_INFORMATION bi = {};
		bqi.InformationLevel = BatteryInformation;

		if(!DeviceIoControl(hBat.hBattery, IOCTL_BATTERY_QUERY_INFORMATION,
						   &bqi, sizeof(bqi), &bi, sizeof(bi), &dwOut, NULL))
		{
			continue;
		}

		// Only non-UPS system batteries count
		if (!(bi.Capabilities & BATTERY_SYSTEM_BATTERY))
		{
			continue;
		}


		// Query the battery status.
		BATTERY_WAIT_STATUS bws = {};
		bws.BatteryTag = bqi.BatteryTag;

		BATTERY_STATUS bs;

		if(!DeviceIoControl(hBat.hBattery, IOCTL_BATTERY_QUERY_STATUS,
							&bws, sizeof(bws), &bs, sizeof(bs), &dwOut, NULL))
		{
			continue;
		}

		markETWEvent( bs, bi );
	}
}

// NTSTATUS definition copied from bcrypt.h.
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
// Other definitions copied from tribal knowledge on the Internet because
// NtQueryTimerResolution is not officially documented.
const NTSTATUS STATUS_SUCCESS = 0;
extern "C" NTSYSAPI NTSTATUS NTAPI NtQueryTimerResolution(
	_Out_ PULONG minimumResolution,
	_Out_ PULONG maximumResolution,
	_Out_ PULONG currentResolution);
// Link to ntdll.lib to allow calling of NtQueryTimerResolution
#pragma comment(lib, "ntdll")
void CPowerStatusMonitor::SampleTimerState()
{
	ULONG minResolution, maxResolution, curResolution;
	NTSTATUS status = NtQueryTimerResolution(&minResolution, &maxResolution, &curResolution);
	if (status == STATUS_SUCCESS) {
		// Convert from 100 ns (0.1 microsecond) units to milliseconds.
		ETWMarkTimerInterval(curResolution * 1e-4);
	}
}

DWORD __stdcall CPowerStatusMonitor::StaticPowerMonitorThread(LPVOID param)
{
	SetCurrentThreadName("Power monitor thread");

	CPowerStatusMonitor* pThis = reinterpret_cast<CPowerStatusMonitor*>(param);
	pThis->PowerMonitorThread();
	return 0;
}

void CPowerStatusMonitor::PowerMonitorThread()
{

	for (;;)
	{
		DWORD result = WaitForSingleObject(hExitEvent_, kSamplingInterval);
		if (result == WAIT_OBJECT_0)
			break;

		SampleBatteryStat();
		SampleCPUPowerState();
		SampleTimerState();
	}
}

CPowerStatusMonitor::CPowerStatusMonitor()
{
	// If Intel Power Gadget is installed then use it to get CPU power data.
#if _M_X64
	PCWSTR dllName = L"\\EnergyLib64.dll";
#else
	PCWSTR dllName = L"\\EnergyLib32.dll";
#endif
#pragma warning(disable : 4996)
	PCWSTR powerGadgetDir = _wgetenv(L"IPG_Dir");
	if (powerGadgetDir)
		energyLib_ = LoadLibrary((std::wstring(powerGadgetDir) + dllName).c_str());
	if (energyLib_)
	{
		IntelEnergyLibInitialize = (IntelEnergyLibInitialize_t)GetProcAddress(energyLib_, "IntelEnergyLibInitialize");
		GetNumMsrs = (GetNumMsrs_t)GetProcAddress(energyLib_, "GetNumMsrs");
		GetMsrName = (GetMsrName_t)GetProcAddress(energyLib_, "GetMsrName");
		GetMsrFunc = (GetMsrFunc_t)GetProcAddress(energyLib_, "GetMsrFunc");
		GetPowerData = (GetPowerData_t)GetProcAddress(energyLib_, "GetPowerData");
		ReadSample = (ReadSample_t)GetProcAddress(energyLib_, "ReadSample");
		auto GetMaxTemperature = (GetMaxTemperature_t)GetProcAddress(energyLib_, "GetMaxTemperature");
		if (GetMaxTemperature)
			GetMaxTemperature(0, &maxTemperature_);
		if (IntelEnergyLibInitialize && ReadSample)
		{
			IntelEnergyLibInitialize();
			ReadSample();
		}
	}

	hExitEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	hThread_ = CreateThread(NULL, 0, StaticPowerMonitorThread, this, 0, nullptr);
}

CPowerStatusMonitor::~CPowerStatusMonitor()
{
	// Shut down the child thread.
	SetEvent(hExitEvent_);
	WaitForSingleObject(hThread_, INFINITE);
	CloseHandle(hThread_);
	CloseHandle(hExitEvent_);
}
