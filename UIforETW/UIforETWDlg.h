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

#include "stdafx.h"
//#include <string>
//#include <vector>
#include <memory>
#include "KeyLoggerThread.h"
#include "DirectoryMonitor.h"

enum TracingMode
{
	kTracingToMemory,
	kTracingToFile,
	kHeapTracingToFile
};

class CUIforETWDlg final : public CDialogEx
{
public:
	CUIforETWDlg(_In_opt_ CWnd* pParent = NULL);	// standard constructor
	~CUIforETWDlg();

// Dialog Data
	enum { IDD = IDD_UIFORETW_DIALOG };

	void vprintf(PCWSTR pFormat, va_list marker);

private:
	virtual void DoDataExchange(CDataExchange* pDX) override;	// DDX/DDV support

	HICON m_hIcon;

	bool bIsTracing_ = false;

	CButton btStartTracing_;
	CButton btSaveTraceBuffers_;
	CButton btStopTracing_;

	void TransferSettings(bool saving);

	bool bCompress_      = true;
	bool bCswitchStacks_ = true;
	bool bSampledStacks_ = true;
	bool bFastSampling_  = false;
	bool bGPUTracing_    = false;
	bool bShowCommands_  = false;

	CButton btCompress_;
	CButton btCswitchStacks_;
	CButton btSampledStacks_;
	CButton btFastSampling_;
	CButton btGPUTracing_;
	CButton btShowCommands_;

	bool bHeapStacks_ = true;

	// Set this to true if _NT_SYMBOL_PATH is not set.
	bool bManageSymbolPath_ = false;

	CEdit btTraceNameEdit_;
	CRect traceNameEditRect_;
	
	std::wstring preRenameTraceName_;
	
	bool validRenameDate_ = false;
	
	// Typical trace names look like this:
	// 2015-03-21_08-52-11_Bruce
	// The first 19 characters are the date and time.
	// The remainder are eligible for editing.
	const size_t kPrefixLength = 19;
	void StartRenameTrace(bool fullRename);

	bool useChromeProviders_ = false;

	KeyLoggerState InputTracing_ = kKeyLoggerAnonymized;
	CComboBox btInputTracing_;
	CStatic btInputTracingLabel_;

	TracingMode tracingMode_ = kTracingToMemory;
	CComboBox btTracingMode_;
	// Hardcoded to chrome.exe for now.
	std::wstring heapTracingExe_ = L"chrome.exe";

	// Default is hardcoded to C:\\Temp\\ if it cannot be restored..
	std::wstring chromeDllPath_ = L"C:\\Temp\\";
	void SetHeapTracing(bool forceOff);

	std::vector<std::wstring> traces_;
	CListBox btTraces_;

	// This starts and stops a thread that watches for changes to the
	// trace directory and sends a message when one is detected.
	DirectoryMonitor monitorThread_;

	// This contains the notes for the selected trace, as loaded from disk.
	std::wstring traceNotes_;
	std::wstring traceNoteFilename_;
	CEdit btTraceNotes_;

	// After recording a trace put the name here so that the directory
	// notification handler will no to select it.
	std::wstring lastTraceFilename_;

	// Note that the DirectoryMonitorThread has a pointer to the contents of
	// this string object, so don't change it without adding synchronization.
	std::wstring traceDir_;
	std::wstring tempTraceDir_;
	std::wstring wptDir_;

	std::wstring output_;
	CEdit btOutput_;

	// General purpose keyboard accelerators.
	HACCEL hAccelTable_       = NULL;
	// Keyboard accelerators that are active only when renaming a trace.
	HACCEL hRenameAccelTable_ = NULL;
	// Keyboard accelerators that are active only when editing trace.
	HACCEL hNotesAccelTable_  = NULL;
	// Keyboard accelerators that are active only when the trace list is active.
	HACCEL hTracesAccelTable_ = NULL;

	void SetSamplingSpeed();

	// Stop tracing (if tracing to a file or if bSaveTrace is
	// false), saving the trace as well if bSaveTrace is true.
	void StopTracingAndMaybeRecord(bool bSaveTrace);

	std::wstring GetWPTDir() const { return wptDir_; }
	std::wstring GetXperfPath() const { return GetWPTDir() + L"xperf.exe"; }
	std::wstring GetTraceDir() const { return traceDir_; }
	std::wstring GetExeDir() const;
	// Note that GenerateResultFilename() gives a time-based name, so don't expect
	// the same result across multiple calls!
	std::wstring GenerateResultFilename() const;
	std::wstring GetTempTraceDir() const { return tempTraceDir_; }
	std::wstring GetKernelFile() const { return CUIforETWDlg::GetTempTraceDir() + L"kernel.etl"; }
	std::wstring GetUserFile() const { return GetTempTraceDir() + L"user.etl"; }
	std::wstring GetHeapFile() const { return GetTempTraceDir() + L"heap.etl"; }

	// Get session name for kernel logger
	const std::wstring kernelLogger_ = L"\"NT Kernel Logger\"";
	//const std::wstring logger_ = L"\"Circular Kernel Context Logger\"";
	std::wstring GetKernelLogger() const { return kernelLogger_; }

	int initialWidth_  = 0;
	int initialHeight_ = 0;
	int lastWidth_  = 0;
	int lastHeight_ = 0;

	_Success_( return )
	bool SetSymbolPath();
	// Call this to retrieve a directory from an environment variable, or use
	// a default, and make sure it exists.
	std::wstring GetDirectory(_In_z_ PCWSTR env, const std::wstring& default);
	void CUIforETWDlg::UpdateTraceList();
	void RegisterProviders();
	void DisablePagingExecutive();

	CToolTipCtrl toolTip_;

	// Editable only by the settings dialog.
	bool bChromeDeveloper_ = true;
	bool bAutoViewTraces_  = false;

	void CompressTrace(const std::wstring& tracePath);
	// Update the enabled/disabled states of buttons.
	void UpdateEnabling();
	void UpdateNotesState();
	void StripChromeSymbols(const std::wstring& traceFilename);
	void PreprocessTrace(const std::wstring& traceFilename);
	void LaunchTraceViewer(const std::wstring traceFilename, const std::wstring viewer = L"wpa.exe");
	void SaveNotesIfNeeded();
	void ShutdownTasks();
	bool bShutdownCompleted_ = false;

	// Generated message map functions
	virtual BOOL OnInitDialog() override;
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()

	_Pre_satisfies_( ( tracingMode_ == kTracingToMemory ) || ( tracingMode_ == kTracingToFile ) || ( tracingMode_ == kHeapTracingToFile ) )
	afx_msg void OnBnClickedStarttracing();
	afx_msg void OnBnClickedStoptracing();
	afx_msg void OnBnClickedCompresstrace();
	afx_msg void OnBnClickedCpusamplingcallstacks();
	afx_msg void OnBnClickedContextswitchcallstacks();
	afx_msg void OnBnClickedShowcommands();
	afx_msg void OnBnClickedFastsampling();
	afx_msg void OnCbnSelchangeInputtracing();
	afx_msg LRESULT UpdateTraceListHandler(WPARAM wParam, LPARAM lParam);
	afx_msg void OnLbnDblclkTracelist();
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnSize(UINT, int, int);
	afx_msg void OnLbnSelchangeTracelist();
	afx_msg void OnBnClickedAbout();
	afx_msg void OnBnClickedSavetracebuffers();
	afx_msg LRESULT OnHotKey(WPARAM wParam, LPARAM lParam);
	afx_msg BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg void OnClose(); 
	afx_msg void OnCancel();
	afx_msg void OnOK();
	afx_msg void OnCbnSelchangeTracingmode();
	afx_msg void OnBnClickedSettings();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnRenameKey();
	afx_msg void OnFullRenameKey();
	afx_msg void FinishTraceRename();
	afx_msg void CancelTraceRename();
	afx_msg void OnOpenTraceWPA();
	afx_msg void OnOpenTraceGPUView();
	afx_msg void CopyTraceName();
	afx_msg void DeleteTrace();
	afx_msg void SelectAll();
public:
	afx_msg void OnBnClickedGPUtracing();
};
