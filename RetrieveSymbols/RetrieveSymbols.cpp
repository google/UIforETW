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

// Symbol downloading demonstration code.
// For more information see ReadMe.txt and this blog post:
// https://randomascii.wordpress.com/2013/03/09/symbols-the-microsoft-way/

#include <stdio.h>
#include <Windows.h>
#include <DbgHelp.h>
#include <string>

// Link with the dbghelp import library
#pragma comment(lib, "dbghelp.lib")

// Uncomment this line to test with known-good parameters.
//#define TESTING

int main(int argc, _Pre_readable_size_(argc) char* argv[])
{
   // Tell dbghelp to print diagnostics to the debugger output.
   SymSetOptions(SYMOPT_DEBUG);

   // Initialize dbghelp
   const HANDLE fakeProcess = reinterpret_cast<const HANDLE>(1);
   const BOOL initResult = SymInitialize(fakeProcess, NULL, FALSE);
   if (initResult == FALSE)
   {
      printf("SymInitialize failed!! Error: %u\n", ::GetLastError());
      return -1;
   }

#ifdef TESTING
   // Set a search path and cache directory. If this isn't set
   // then _NT_SYMBOL_PATH will be used instead.
   // Force setting it here to make sure that the test succeeds.
   SymSetSearchPath(fakeProcess,
              "SRV*c:\\symbolstest*http://msdl.microsoft.com/download/symbols");

   // Valid PDB data to test the code.
   std::string gTextArg = "072FF0EB54D24DFAAE9D13885486EE09";
   const char* ageText = "2";
   const char* fileName = "kernel32.pdb";

   // Valid PE data to test the code
   fileName = "crypt32.dll";
   const char* dateStampText = "4802A0D7";
   const char* sizeText = "95000";
   //fileName = "chrome_child.dll";
   //const char* dateStampText = "5420D824";
   //const char* sizeText = "20a6000";
#else
   if (argc < 4)
   {
       printf("Error: insufficient arguments.\n");
       printf("Usage: %s guid age pdbname\n", argv[0]);
       printf("Usage: %s dateStamp size pename\n", argv[0]);
       printf("Example: %s 6720c31f4ac24f3ab0243e0641a4412f 1 "
              "chrome_child.dll.pdb\n", argv[0]);
       printf("Example: %s 4802A0D7 95000 crypt32.dll\n", argv[0]);
       return 0;
   }

   std::string gTextArg = argv[1];
   PCSTR const dateStampText = argv[1];
   PCSTR const ageText = argv[2];
   PCSTR const sizeText = argv[2];
   PCSTR const fileName = argv[3];
#endif

   // Parse the GUID and age from the text
   GUID g = {};
   DWORD age = 0;
   DWORD dateStamp = 0;
   DWORD size = 0;

   // Settings for SymFindFileInPath
   void* id = nullptr;
   DWORD flags = 0;
   DWORD two = 0;

   PCSTR const ext = strrchr(fileName, '.');
   if (!ext)
   {
     printf("No extension found on %s. Fatal error.\n", fileName);
     return 0;
   }

   if (_stricmp(ext, ".pdb") == 0)
   {
     std::string gText;
     // Scan the GUID argument and remove all non-hex characters. This allows
     // passing GUIDs with '-', '{', and '}' characters.
     for (auto c : gTextArg)
     {
       if (isxdigit(static_cast<unsigned char>(c)))
       {
         gText.push_back(c);
       }
     }
     if (gText.size() != 32)
     {
         printf("Error: PDB GUIDs must be exactly 32 characters"
                " (%s was stripped to %s).\n", gTextArg.c_str(), gText.c_str());
         return 10;
     }

     int count = sscanf_s(gText.substr(0, 8).c_str(), "%x", &g.Data1);
     DWORD temp;
     count += sscanf_s(gText.substr(8, 4).c_str(), "%x", &temp);
     g.Data2 = (unsigned short)temp;
     count += sscanf_s(gText.substr(12, 4).c_str(), "%x", &temp);
     g.Data3 = (unsigned short)temp;
     for (auto i = 0; i < ARRAYSIZE(g.Data4); ++i)
     {
         count += sscanf_s(gText.substr(16 + i * 2, 2).c_str(), "%x", &temp);
         g.Data4[i] = (unsigned char)temp;
     }
     count += sscanf_s(ageText, "%x", &age);

     if (count != 12)
     {
         printf("Error: couldn't parse the PDB GUID/age string. Sorry.\n");
         return 10;
     }
     flags = SSRVOPT_GUIDPTR;
     id = &g;
     two = age;
     printf("Looking for PDB file %s %s %s.\n", gText.c_str(), ageText, fileName);
   }
   else
   {
     if (strlen(dateStampText) != 8)
       printf("Warning!!! The datestamp (%s) is not eight characters long. "
              "This is usually wrong.\n", dateStampText);
     int count = sscanf_s(dateStampText, "%x", &dateStamp);
     count += sscanf_s(sizeText, "%x", &size);
     flags = SSRVOPT_DWORDPTR;
     id = &dateStamp;
     two = size;
     printf("Looking for PE file %s %x %x.\n", fileName, dateStamp, two);
   }

   //SymFindFileInPath is annotated_Out_writes_(MAX_PATH + 1)
   //thus, passing less than (MAX_PATH+1) is an overrun!
   //The documentation says the buffer needs to be MAX_PATH - hurray for
   //consistency - but better safe than owned.
   char filePath[MAX_PATH+1] = {};
   DWORD three = 0;

   if (SymFindFileInPath(fakeProcess, NULL, fileName, id, two, three,
               flags, filePath, NULL, NULL))
   {
       printf("Found file - placed it in %s.\n", filePath);
   }
   else
   {
       printf("Error: symbols not found - error %u. Are dbghelp.dll and "
               "symsrv.dll in the same directory as this executable?\n",
               GetLastError());
       printf("Note that symbol server lookups sometimes fail randomly. "
              "Try again?\n");
   }

   const BOOL cleanupResult = SymCleanup(fakeProcess);
   if (cleanupResult == FALSE)
   {
      printf("SymCleanup failed!! Error: %u\n", ::GetLastError());
   }

   return 0;
}
