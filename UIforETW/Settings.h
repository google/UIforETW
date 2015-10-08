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
#include "afxwin.h"

// CSettings dialog

class CSettings : public CDialogEx
{
	DECLARE_DYNAMIC(CSettings)

public:
	CSettings(CWnd* pParent, const std::wstring& exeDir, const std::wstring& wptDir, const std::wstring& wpt10Dir);   // standard constructor
	virtual ~CSettings();

// Dialog Data
	enum { IDD = IDD_SETTINGS };

	// These settings are written and read by the creator of this object.
	std::wstring heapTracingExes_;
	std::wstring WSMonitoredProcesses_;
	bool bExpensiveWSMonitoring_ = false;
	std::wstring extraKernelStacks_;
	std::wstring extraKernelFlags_;
	bool bChromeDeveloper_ = false;
	bool bAutoViewTraces_ = false;
	bool bHeapStacks_ = false;
	bool bVirtualAllocStacks_ = false;
	uint64_t chromeKeywords_ = 0;

protected:
	CEdit btHeapTracingExe_;
	CEdit btWSMonitoredProcesses_;
	CButton btExpensiveWSMonitoring_;
	CEdit btExtraKernelFlags_;
	CEdit btExtraStackwalks_;
	CComboBox btBufferSizes_;

	CButton btCopyStartupProfile_;
	CButton btCopySymbolDLLs_;

	CButton btChromeDeveloper_;
	CButton btAutoViewTraces_;
	CButton btHeapStacks_;
	CButton btVirtualAllocStacks_;
	CCheckListBox btChromeCategories_;

	CToolTipCtrl toolTip_;

	virtual void DoDataExchange(CDataExchange* pDX) override;    // DDX/DDV support
	virtual BOOL OnInitDialog() override;

	const std::wstring exeDir_;
	// Same meaning as in CUIforETWDlg
	const std::wstring wptDir_;
	const std::wstring wpt10Dir_;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnOK();
public:
	afx_msg void OnBnClickedCopystartupprofile();
	afx_msg void OnBnClickedCopysymboldlls();
	afx_msg BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnBnClickedChromedeveloper();
	afx_msg void OnBnClickedAutoviewtraces();
	afx_msg void OnBnClickedHeapstacks();
	afx_msg void OnBnClickedVirtualallocstacks();
	afx_msg void OnBnClickedExpensivews();
};
