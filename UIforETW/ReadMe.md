This is the to-do list for UIforETW, a UI to wrap recording ETW traces with xperf.exe.

Done:
- [X] Get appropriate default for tracedir.
- [X] Create tracedir as needed.
- [X] Launch wpa after a trace finishes recording.
- [X] Disable start/stop/snap buttons as appropriate.
- [X] Show commands and command output.
- [X] Add verbose option (default is off) to control whether commands are displayed.
- [X] Print readable, well-spaced information about the state of tracing.
- [X] Respect tracedir and temptracedir.
- [X] Generate good trace names, with built-in keywords.
- [X] Input recording.
- [X] Move more initialization like _NT_SYMBOL_PATH to startup.
- [X] Process Chrome symbols.
- [X] Finish input recording options and give warning about full input recording.
- [X] List all traces in tracedir and allow viewing them.
- [X] Register providers.
- [X] set DisablePagingExecutive to 1
- [X] Vertically resizable window to show more traces.
- [X] Editing and auto-saving of trace notes.
- [X] Stop tracing on shutdown!!!
- [X] Hot key to stop/record traces.
- [X] Make sure trace notes are saved when closing.
- [X] Tool-tips
- [X] Add circular-buffer support.
- [X] Heap tracing.
- [X] Show ProcessChromeSymbols.py status as it runs so it doesn't look like it's hung forever.
- [X] Rename output executables and check in a renamed copy so people don't have to build it.
- [X] StripChromeSymbols.py needs to pass the appropriate command-line to xperf to ignored dropped events and time inversions.
- [X] Unicode support

Trace list should let the user:
- [X] Delete traces
- [X] Compress traces
- [X] Explore to the trace directory
- [X] Copy the traces name/path to the clipboard
- [X] Run StripChromeSymbols.py

- [X] Remove references to _T macro.
- [X] Remove .etl extension from list of traces -- it adds no value and wastes space.
- [X] Make sure trace notes are disabled when no trace is selected, including when
- [X] traces are added or deleted.
- [X] Move focus away from buttons before they are disabled, as when starting a trace with Alt+T.
- [X] Error checking -- checking for failures to start or stop tracing.
- [X] Renaming of traces and associated files.
- [X] Need keyboard accelerators for F2 (start renaming), enter (stop renaming),
- [X] and probably ESC (to stop it from closing the dialog).
- [X] Add DX provider.
- [X] Copy over startup profile on first-run, and subsequent runs?
- [X] Optionally copy over 64-bit dbghelp.dll and symsrv.dll?
- [X] Remember all settings.
- [X] Support for 32-bit operating systems.
- [X] Add compatibility manifest up to Windows 8.1
- [X] Add OS specific checks for what user providers to enable, compression options, etc.
- [X] Increase the user-provider buffer counts when doing DX profiling.
- [X] Disable compress options (checkbox and menu) for Windows 7 and below.
- [X] Move GetPython function to utility.cpp
- [X] Don't allow having two copies of UIforETW to run simultaneously.
- [X] Ignore Ctrl+Win+C when tracing is halted.
- [X] Need more keyboard accelerators, to delete traces, view them, copy names to the clipboard, select-all in the notes field, etc.
- [X] Detect and handle trace files that lack the date component - still allow renaming.
- [X] Only have the delete key active when the trace list is active - otherwise it interferes with renaming.
- [X] Make sure edits to notes files are loaded on directory change notifications.
- [X] Set focus to the trace list after renaming a trace.
- [X] Select the newly added trace after recording a trace.
- [X] Place the trace-name editing box appropriately.
- [X] Change path splitting functions to return wstring instead of wchar_t pointers.
- [X] Fix StripChromeSymbols.py so that it can find RetrieveSymbols.exe - copy Microsoft DLLs over?
- [X] Should have a Chrome developer checkbox.
- [X] Have an option to not auto-view traces immediately after they are recorded.
- [X] Remove deprecated file/extension usage, and have functions to return file part, extension, or stripped file part.
- [X] Fix IdentifyChromeProcesses.py to print the heading after processing the trace.
- [X] Remove this line from RetrieveSymbols: Parsing symbol data for a PDB file.
- [X] Support Ctrl+Shift+C to copy just the trace file name.
- [X] Have an option (Shift+F2?) to allow renaming of the entire trace name
- [X] Move ETWEventDemo out of bin directory.
- [X] Display how long a trace took to be recorded, save versus merge?
- [X] Ship batch file to record trace of tracing, and detect when to use it (slow save or merge).
- [X] Ship batch file to create non-DLL version of UIforETW



Most important tasks:
- [ ] Implement more settings - configure trace directories, buffer sizes, option for stacks on user events.
- [ ] Handle the duplicate copies of etwproviders.man.
- [ ] Add some unit tests.
- [ ] Translate the error codes on starting tracing into English, and give advice.
- [X] Remember window height

To-do eventually:
- [ ] Should have the option to run arbitrary scripts after each trace is recorded.
- [ ] Should have the option to run arbitrary scripts on every trace in the list.
- [ ] PreprocessTrace should append to the trace text file.
- [ ] Should have an option to put an entire process tree in the trace text file.
- [ ] ChildProcess or PreprocessTrace should convert from LF to CRLF for the edit control.
- [ ] Try using the -cancel option for more efficient stopping of traces without recording. See xperf -help stop
- [X] Give the user a chance to rename trace before launching viewer
- [ ] Transparent compression/decompression into .zip files. When unzipping use the .zip file name as the base. See https://ixmx.wordpress.com/2009/09/16/how-to-create-a-compress-folder-in-windows-using-win32-api/ or http://stackoverflow.com/questions/118547/creating-a-zip-file-on-windows-xp-2003-in-c-c.
- [ ] Have the option to copy different startup profiles, for different situations.
- [ ] Resize output window as well when sizing the window, just a bit.
Code cleanup:
- [ ] getenv wrapper
- [ ] ordering code sanely
- [ ] moving more code to separate functions/files
- [ ] Remove usage of bool as function parameters to choose behaviors
- [ ] Perhaps use LBS_WANTKEYBOARDINPUT and WM_VKEYTOITEM to implement the list box keyboard shortcuts?
- [ ] Detect when UIforETW's startup profile is newer than the WPA Files copy and ask user if it should be copied over.

Unimportant:
- [ ] Allow configuring which symbols should be stripped.
- [ ] Configure a maximum time to trace for to avoid infinitely long traces that fill the hard drive.
- [ ] CPU frequency monitoring to look for thermal throttling.
