/*
Copyright 2019 Google Inc. All Rights Reserved.

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

#include <Windows.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
  if (argc < 2) {
    printf("This program creates a process in a suspended state, prints its PID"
           " so that heap tracing can be enabled, and resumes the thread after a"
           " brief delay. This helps etwheapsnapshot.bat trace a process from"
           " birth. Only the PID is printed to stdout (for easy capture from a"
           " batch file) - the rest is printed to stderr.\n");
    printf("Usage: %s proc_name.exe [delay_ms]\n", argv[0]);
    return 0;
  }

  const char* name = argv[1];
  STARTUPINFOA startup_info = {sizeof(startup_info)};
  PROCESS_INFORMATION process_info = {};
  LARGE_INTEGER start;
  QueryPerformanceCounter(&start);
  BOOL result = CreateProcessA(name, nullptr, nullptr, nullptr, FALSE,
                               CREATE_SUSPENDED, nullptr, nullptr, &startup_info,
                               &process_info);

  if (!result) {
    fprintf(stderr, "CreateProcess failed.\n");
    return -1;
  }

  // Print the process ID to stdout for consumption by a batch file.
  printf("%d\n", process_info.dwProcessId);
  // Print the process to stderr so that the output can be seen.
  fprintf(stderr, "PID is %d.\n", process_info.dwProcessId);

  DWORD delay = 10000;
  if (argc >= 3)
    delay = atoi(argv[2]);
  fprintf(stderr, "Waiting %1.3f s before letting the process run.\n", delay / 1e3);
  Sleep(delay);
  // Resume the main thread, thus starting the process.
  ResumeThread(process_info.hThread);
  fprintf(stderr, "Process is now running.\n");

  // When this process is destroyed all its handles to the new process will be
  // cleaned up, so no cleanup is necessary.
}
