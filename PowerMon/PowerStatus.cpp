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

#include "PowerStatus.h"

// These correspond to the funcID values returned by GetMsrFunc
// They are documented here:
// https://software.intel.com/en-us/blogs/2014/01/07/using-the-intel-power-gadget-30-api-on-windows
// Sample code from there was used to help create the Power Gadget API
// code.
const int MSR_FUNC_FREQ = 0;
const int MSR_FUNC_POWER = 1;
const int MSR_FUNC_TEMP = 2;
const int MSR_FUNC_MAX_POWER = 3; /* ????? */

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
			wprintf(L"%s = %4.0f MHz\n", MSRName, data[0]);
		}
		else if (funcID == MSR_FUNC_POWER)
		{
			// Round to nearest .0001 to avoid distracting excess precision.
			data[0] = round(data[0] * 10000) / 10000;
			data[2] = round(data[2] * 10000) / 10000;
			wprintf(L"%s Power (W) = %3.2f\n", MSRName, data[0]);
			wprintf(L"%s Energy(J) = %3.2f\n", MSRName, data[1]);
			wprintf(L"%s Energy(mWh)=%3.2f\n", MSRName, data[2]);
		}
		else if (funcID == MSR_FUNC_TEMP)
		{
			// The 3.02 version of Intel Power Gadget seems to report the temperature
			// in F instead of C.
			wprintf(L"%s Temp (C) = %3.0f (max is %3.0f)\n", MSRName, data[0], (double)maxTemperature_);
		}
		else if (funcID == MSR_FUNC_MAX_POWER)
		{
			//wprintf(L"%s Max Power (W) = %3.0f\n", MSRName, data[0]);
		}
		else
		{
			//wprintf(L"Unused funcID %d\n", funcID);
		}
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
		if (IntelEnergyLibInitialize && ReadSample)
		{
			if (IntelEnergyLibInitialize())
			{
				if (GetMaxTemperature)
					GetMaxTemperature(0, &maxTemperature_);
				ReadSample();
			}
			else
			{
				// Mark the library as unavailable if Initialize fails
				ClearEnergyLibFunctionPointers();
			}
		}
	}
}

void CPowerStatusMonitor::ClearEnergyLibFunctionPointers()
{
	IntelEnergyLibInitialize = nullptr;
	GetNumMsrs = nullptr;
	GetMsrName = nullptr;
	GetMsrFunc = nullptr;
	GetPowerData = nullptr;
	ReadSample = nullptr;
}
