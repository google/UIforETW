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

// https://software.intel.com/en-us/blogs/2012/12/13/using-the-intel-power-gadget-api-on-mac-os-x
typedef int(*IntelEnergyLibInitialize_t)();
typedef int(*GetNumMsrs_t)(int* nMsr);
typedef int(*GetMsrName_t)(int iMsr, wchar_t* szName);
typedef int(*GetMsrFunc_t)(int iMsr, int* pFuncID);
typedef int(*GetPowerData_t)(int iNode, int iMsr, double* pResult, int* nResult);
typedef int(*ReadSample_t)();
typedef int(*GetMaxTemperature_t)(int iNode, int* degreeC);

class CPowerStatusMonitor
{
public:
	enum class MonitorType
	{
		LightLoad,
		HeavyLoad
	};
	CPowerStatusMonitor();
	~CPowerStatusMonitor();

	// Tell the system which perf counters (if any) to monitor. Only call
	// this when the threads are stopped.
	void SetPerfCounters(std::wstring& perfCounters);

	// Start and stop the sampling threads so that they aren't running
	// when tracing is not running.
	void StartThreads(MonitorType monitorType);
	void StopThreads();

private:
	static DWORD __stdcall StaticPowerMonitorThread(LPVOID);
	void PowerMonitorThread();

	void SampleBatteryStat();
	void SampleCPUPowerState();
	void SampleTimerState();
	void ClearEnergyLibFunctionPointers();

	HANDLE hThread_ = nullptr;
	HANDLE hExitEvent_ = nullptr;
	MonitorType monitorType_ = MonitorType::HeavyLoad;

	HMODULE energyLib_ = nullptr;
	IntelEnergyLibInitialize_t IntelEnergyLibInitialize = nullptr;
	GetNumMsrs_t GetNumMsrs = nullptr;
	GetMsrName_t GetMsrName = nullptr;
	GetMsrFunc_t GetMsrFunc = nullptr;
	GetPowerData_t GetPowerData = nullptr;
	ReadSample_t ReadSample = nullptr;
	int maxTemperature_ = 0;

	std::wstring perfCounters_;

	CPowerStatusMonitor& operator=(const CPowerStatusMonitor&) = delete;
	CPowerStatusMonitor(const CPowerStatusMonitor&) = delete;
};
