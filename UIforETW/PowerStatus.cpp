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

#include "PowerStatus.h"
#include <ETWProviders\etwprof.h>

#include <devguid.h>
#include <Setupapi.h>
#include <devioctl.h>
#include <Poclass.h>
#include <Batclass.h>

#pragma comment(lib, "setupapi.lib")

const int kSamplingInterval = 200;

void CPowerStatusMonitor::SampleBatteryStat()
{
	HDEVINFO hdev = SetupDiGetClassDevs(&GUID_DEVCLASS_BATTERY,
							0,
							0,
							DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (hdev == INVALID_HANDLE_VALUE)
		return;

	// Avoid infinite loops.
	const int maxBatteries = 5;
	for (int deviceNum = 0; deviceNum < maxBatteries; deviceNum++)
	{
		SP_DEVICE_INTERFACE_DATA did = { sizeof(did) };

		if (SetupDiEnumDeviceInterfaces(hdev,
										0,
										&GUID_DEVCLASS_BATTERY,
										deviceNum,
										&did))
		{
			DWORD bytesNeeded = 0;
			SetupDiGetDeviceInterfaceDetail(hdev,
											&did,
											0,
											0,
											&bytesNeeded,
											0);

			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				auto pdidd = (PSP_DEVICE_INTERFACE_DETAIL_DATA)LocalAlloc(LPTR, bytesNeeded);
				pdidd->cbSize = sizeof(*pdidd);
				if (SetupDiGetDeviceInterfaceDetail(hdev,
													&did,
													pdidd,
													bytesNeeded,
													&bytesNeeded,
													0))
				{
					// Found a battery. Query it.
					HANDLE hBattery = CreateFile(pdidd->DevicePath,
									GENERIC_READ | GENERIC_WRITE,
									FILE_SHARE_READ | FILE_SHARE_WRITE,
									NULL,
									OPEN_EXISTING,
									FILE_ATTRIBUTE_NORMAL,
									NULL);
					if (hBattery != INVALID_HANDLE_VALUE)
					{
						BATTERY_QUERY_INFORMATION bqi = {};

						DWORD dwWait = 0;
						DWORD dwOut;
						if (DeviceIoControl(hBattery,
											IOCTL_BATTERY_QUERY_TAG,
											&dwWait,
											sizeof(dwWait),
											&bqi.BatteryTag,
											sizeof(bqi.BatteryTag),
											&dwOut,
											NULL)
											&& bqi.BatteryTag)
						{
							// With the tag, you can query the battery info.
							BATTERY_INFORMATION bi = {};
							bqi.InformationLevel = BatteryInformation;

							if (DeviceIoControl(hBattery,
												IOCTL_BATTERY_QUERY_INFORMATION,
												&bqi,
												sizeof(bqi),
												&bi,
												sizeof(bi),
												&dwOut,
												NULL))
							{
								// Only non-UPS system batteries count
								if (bi.Capabilities & BATTERY_SYSTEM_BATTERY)
								{
									// Query the battery status.
									BATTERY_WAIT_STATUS bws = {};
									bws.BatteryTag = bqi.BatteryTag;

									BATTERY_STATUS bs;
									if (DeviceIoControl(hBattery,
														IOCTL_BATTERY_QUERY_STATUS,
														&bws,
														sizeof(bws),
														&bs,
														sizeof(bs),
														&dwOut,
														NULL))
									{
										char powerState[100];
										powerState[0] = 0;
										if (bs.PowerState & BATTERY_CHARGING)
											strcat_s(powerState, "Charging");
										if (bs.PowerState & BATTERY_DISCHARGING)
											strcat_s(powerState, "Discharging");
										if (bs.PowerState & BATTERY_POWER_ON_LINE)
										{
											if (powerState[0])
												strcat_s(powerState, ", on AC power");
											else
												strcat_s(powerState, "On AC power");
										}

										float batPercentage = bs.Capacity * 100.f / bi.FullChargedCapacity;

										char rate[100];
										if (bs.Rate == BATTERY_UNKNOWN_RATE)
											sprintf_s(rate, "Unknown rate");
										else if (bi.Capabilities & BATTERY_CAPACITY_RELATIVE)
											sprintf_s(rate, "%ld (unknown units)", bs.Rate);
										else
											sprintf_s(rate, "%1.3f watts", bs.Rate / 1000.0f);
										ETWMarkBatteryStatus(powerState, batPercentage, rate);
										//outputPrintf(L"%S, %1.3f%%, %S\n", powerState, batPercentage, rate);
									}
								}
							}
						}
						CloseHandle(hBattery);
					}
				}
				LocalFree(pdidd);
			}
		}
		else  if (ERROR_NO_MORE_ITEMS == GetLastError())
		{
			break;  // Enumeration failed - perhaps we're out of items
		}
	}
	SetupDiDestroyDeviceInfoList(hdev);
}

DWORD __stdcall CPowerStatusMonitor::StaticBatteryMonitorThread(LPVOID param)
{
	CPowerStatusMonitor* pThis = reinterpret_cast<CPowerStatusMonitor*>(param);
	pThis->BatteryMonitorThread();
	return 0;
}

void CPowerStatusMonitor::BatteryMonitorThread()
{

	for (;;)
	{
		DWORD result = WaitForSingleObject(hExitEvent_, kSamplingInterval);
		if (result == WAIT_OBJECT_0)
			break;

		SampleBatteryStat();
	}
}

CPowerStatusMonitor::CPowerStatusMonitor()
{
	hExitEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	hThread_ = CreateThread(NULL, 0, StaticBatteryMonitorThread, this, 0, NULL);
}

CPowerStatusMonitor::~CPowerStatusMonitor()
{
	// Shut down the child thread.
	SetEvent(hExitEvent_);
	WaitForSingleObject(hThread_, INFINITE);
	CloseHandle(hThread_);
	CloseHandle(hExitEvent_);
}
