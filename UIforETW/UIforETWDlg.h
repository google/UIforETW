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
#include <vector>
#include <memory>
#include "KeyLoggerThread.h"
#include "DirectoryMonitor.h"
#include "WorkingSet.h"
#include "PowerStatus.h"

enum TracingMode
{
	kTracingToMemory,
	kTracingToFile,
	kHeapTracingToFile
};

class CUIforETWDlg : public CDialogEx
{
public:
	CUIforETWDlg(CWnd* pParent = NULL);	// standard constructor
	~CUIforETWDlg();

// Dialog Data
	enum { IDD = IDD_UIFORETW_DIALOG };

	void vprintf(const wchar_t* pFormat, va_list marker);

private:
	virtual void DoDataExchange(CDataExchange* pDX) override;	// DDX/DDV support

	HICON m_hIcon;

	bool bIsTracing_ = false;
	ULONGLONG traceStartTime_ = 0;
	// Auto-save trace if tracing to a file runs for longer than this length of time.
	// Otherwise the trace files can fill hard drives and be unusably large.
	// This should be configurable. But it is not.
	const ULONGLONG kMaxFileTraceMs = 120000;

	CButton btStartTracing_;
	CButton btSaveTraceBuffers_;
	CButton btStopTracing_;

	void TransferSettings(bool saving);

	bool bCompress_ = true;
	bool bCswitchStacks_ = true;
	bool bSampledStacks_ = true;
	bool bFastSampling_ = false;
	bool bGPUTracing_ = false;
	bool bShowCommands_ = false;
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
	// Increase the buffer count by some proportion when tracing to a file
	// on a large-memory machine.
	int BufferCountBoost(int requestCount) const;
	CComboBox btTracingMode_;
	std::wstring heapTracingExes_ = L"chrome.exe";
	std::wstring chromeDllPath_ = L"C:\\Temp\\";
	void SetHeapTracing(bool forceOff);
	bool bVirtualAllocStacks_ = false;
	// ETW keywords, also known as flags, map to Chrome categories.
	// Confused yet?
	uint64_t chromeKeywords_ = 0;

	std::vector<std::wstring> traces_;
	CListBox btTraces_;

	// This starts and stops a thread that watches for changes to the
	// trace directory and sends a message when one is detected.
	DirectoryMonitor monitorThread_;
	// This starts and stops a thread that monitors process working sets.
	CWorkingSetMonitor workingSetThread_;
	std::wstring WSMonitoredProcesses_;
	bool bExpensiveWSMonitoring_ = false;

	// This starts and stops a thread that monitors battery status.
	CPowerStatusMonitor PowerMonitor_;

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
	std::wstring windowsKitsDir_; // C:\\Program Files (x86)\\Windows Kits
	std::wstring wptDir_;
	std::wstring wpt10Dir_;
	std::wstring wpaPath_;
	std::wstring gpuViewPath_;
	std::wstring wpa10Path_;
	std::wstring wpaDefaultPath_; // Default viewer.

	std::wstring windowsDir_; // C:\Windows\, or some-such.
	std::string systemDrive_; // C:\, or something like that, ANSI.

	std::wstring output_;
	CEdit btOutput_;

	// General purpose keyboard accelerators.
	HACCEL hAccelTable_ = NULL;
	// Keyboard accelerators that are active only when renaming a trace.
	HACCEL hRenameAccelTable_ = NULL;
	// Keyboard accelerators that are active only when editing trace.
	HACCEL hNotesAccelTable_ = NULL;
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

	int initialWidth_ = 0;
	int initialHeight_ = 0;
	int lastWidth_ = 0;
	int lastHeight_ = 0;

	void SetSymbolPath();
	// Call this to retrieve a directory from an environment variable, or use
	// a default, and make sure it exists.
	std::wstring GetDirectory(const wchar_t* env, const std::wstring& default);
	void CUIforETWDlg::UpdateTraceList();
	void RegisterProviders();
	void DisablePagingExecutive();

	CToolTipCtrl toolTip_;

	// Editable only by the settings dialog.
	bool bChromeDeveloper_ = true;
	bool bAutoViewTraces_ = false;

	std::pair<uint64_t, uint64_t> CompressTrace(const std::wstring& tracePath) const;
	void CompressAllTraces() const;
	// Update the enabled/disabled states of buttons.
	void UpdateEnabling();
	void UpdateNotesState();
	void StripChromeSymbols(const std::wstring& traceFilename);
	void PreprocessTrace(const std::wstring& traceFilename);
	void LaunchTraceViewer(const std::wstring traceFilename, const std::wstring viewerPath);
	void SaveNotesIfNeeded();
	void ShutdownTasks();
	bool bShutdownCompleted_ = false;

	// Generated message map functions
	virtual BOOL OnInitDialog() override;
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
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
	afx_msg void OnOpenTrace10WPA();
	afx_msg void OnOpenTraceGPUView();
	afx_msg void CopyTraceName();
	afx_msg void DeleteTrace();
	afx_msg void NotesSelectAll();
	afx_msg void NotesPaste();
	afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
public:
	afx_msg void OnBnClickedGPUtracing();
};
